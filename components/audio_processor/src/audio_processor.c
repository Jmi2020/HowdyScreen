#include <string.h>
#include "audio_processor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "tts_jitter_buffer.h"

static const char *TAG = "AudioProcessor";

// Audio configuration
static audio_processor_config_t s_config;
static bool s_initialized = false;
static bool s_capture_active = false;
static bool s_playback_active = false;

// I2S handles
static i2s_chan_handle_t s_tx_handle = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;

// Task handles
static TaskHandle_t s_capture_task_handle = NULL;
static TaskHandle_t s_playback_task_handle = NULL;

// Ring buffer for audio data
static RingbufHandle_t s_ringbuf_handle = NULL;

// Callback function
static audio_event_callback_t s_event_callback = NULL;

// Audio buffer
static uint8_t *s_audio_buffer = NULL;
static size_t s_buffer_size = 0;

// HowdyTTS Integration Variables
static audio_howdytts_config_t s_howdytts_config = {0};
static bool s_howdytts_enabled = false;
static bool s_dual_protocol_mode = false;
static bool s_websocket_active = true;  // Default to WebSocket
static uint32_t s_frames_processed = 0;
static uint64_t s_total_process_time_us = 0;

// TTS jitter buffer for playback
static tts_jitter_buffer_t *s_tts_jb = NULL;
static size_t s_frame_samples = 0; // e.g., 320 for 20 ms at 16 kHz

// GPIO definitions for ESP32-P4 + ES8311
#define I2S_MCLK_GPIO    GPIO_NUM_13
#define I2S_BCLK_GPIO    GPIO_NUM_12
#define I2S_WS_GPIO      GPIO_NUM_10
#define I2S_DO_GPIO      GPIO_NUM_11  // Speaker output
#define I2S_DI_GPIO      GPIO_NUM_9   // Microphone input

static void audio_capture_task(void *pvParameters)
{
    uint8_t *buffer = (uint8_t *)malloc(s_config.dma_buf_len * s_config.bits_per_sample / 8);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate capture buffer");
        vTaskDelete(NULL);
        return;
    }
    
    size_t bytes_read = 0;
    
    ESP_LOGI(TAG, "Audio capture task started");
    
    while (s_capture_active) {
        // Read audio data from I2S
        esp_err_t ret = i2s_channel_read(s_rx_handle, buffer, 
                                       s_config.dma_buf_len * s_config.bits_per_sample / 8, 
                                       &bytes_read, pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK && bytes_read > 0) {
            uint64_t start_time = esp_timer_get_time();
            
            // Send to ring buffer
            if (xRingbufferSend(s_ringbuf_handle, buffer, bytes_read, 0) != pdTRUE) {
                ESP_LOGW(TAG, "Ring buffer full, dropping audio data");
            }
            
            // HowdyTTS Integration: Process audio for streaming
            if (s_howdytts_enabled && s_howdytts_config.howdytts_audio_callback) {
                // Convert bytes to 16-bit samples
                size_t sample_count = bytes_read / 2;  // Assuming 16-bit samples
                int16_t *samples = (int16_t*)buffer;
                
                // Call HowdyTTS callback for UDP/WebSocket streaming
                s_howdytts_config.howdytts_audio_callback(samples, sample_count, s_howdytts_config.howdytts_user_data);
            }
            
            // Notify callback if set
            if (s_event_callback) {
                s_event_callback(AUDIO_EVENT_DATA_READY, buffer, bytes_read);
            }
            
            // Update processing statistics
            uint64_t end_time = esp_timer_get_time();
            s_total_process_time_us += (end_time - start_time);
            s_frames_processed++;
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
            if (s_event_callback) {
                s_event_callback(AUDIO_EVENT_ERROR, NULL, 0);
            }
            break;
        }
    }
    
    free(buffer);
    ESP_LOGI(TAG, "Audio capture task stopped");
    vTaskDelete(NULL);
}

static void audio_playback_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio playback task started");
    const TickType_t frame_period = pdMS_TO_TICKS(20); // 20ms cadence
    TickType_t last_wake = xTaskGetTickCount();

    int16_t *frame = (int16_t *)malloc(s_frame_samples * sizeof(int16_t));
    if (!frame) {
        ESP_LOGE(TAG, "Failed to allocate playback frame buffer");
        vTaskDelete(NULL);
        return;
    }

    while (s_playback_active) {
        vTaskDelayUntil(&last_wake, frame_period);

        bool underrun = false;
        if (!s_tts_jb) {
            memset(frame, 0, s_frame_samples * sizeof(int16_t));
            underrun = true;
        } else {
            (void)tts_jb_pop_frame(s_tts_jb, frame, &underrun);
        }

        size_t bytes_written = 0;
        esp_err_t ret = i2s_channel_write(s_tx_handle, frame,
                                          s_frame_samples * sizeof(int16_t),
                                          &bytes_written, pdMS_TO_TICKS(5));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S write error: %s", esp_err_to_name(ret));
        }

        if (underrun) {
            ESP_LOGD(TAG, "Playback underrun: wrote silence frame");
        }
    }

    free(frame);
    
    ESP_LOGI(TAG, "Audio playback task stopped");
    vTaskDelete(NULL);
}

static esp_err_t setup_i2s_channels(void)
{
    ESP_LOGI(TAG, "Setting up I2S channels...");
    
    // I2S channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = s_config.dma_buf_count;
    chan_cfg.dma_frame_num = s_config.dma_buf_len;
    
    // Create I2S channels
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle), TAG, "Failed to create I2S channels");
    
    // I2S standard configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_config.sample_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
            (i2s_data_bit_width_t)s_config.bits_per_sample,
            (i2s_slot_mode_t)s_config.channels
        ),
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws = I2S_WS_GPIO,
            .dout = I2S_DO_GPIO,
            .din = I2S_DI_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    
    // Initialize channels
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_handle, &std_cfg), TAG, "Failed to init TX channel");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_handle, &std_cfg), TAG, "Failed to init RX channel");
    
    ESP_LOGI(TAG, "I2S channels configured successfully");
    return ESP_OK;
}

esp_err_t audio_processor_init(const audio_processor_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Audio processor already initialized");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing audio processor...");
    
    // Copy configuration
    memcpy(&s_config, config, sizeof(audio_processor_config_t));
    
    // Setup I2S channels
    ESP_RETURN_ON_ERROR(setup_i2s_channels(), TAG, "Failed to setup I2S channels");
    
    // Create ring buffer for audio data
    s_ringbuf_handle = xRingbufferCreate(s_config.dma_buf_len * s_config.dma_buf_count * 4, RINGBUF_TYPE_BYTEBUF);
    if (!s_ringbuf_handle) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate audio buffer
    s_buffer_size = s_config.dma_buf_len * s_config.bits_per_sample / 8;
    s_audio_buffer = (uint8_t *)malloc(s_buffer_size);
    if (!s_audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return ESP_ERR_NO_MEM;
    }
    
    // Derive frame size for 20 ms playout
    s_frame_samples = s_config.sample_rate / 50; // 20 ms
    if (s_frame_samples == 0) {
        s_frame_samples = 320; // default safety for 16kHz
    }

    // Create jitter buffer for playback: target 6, capacity 12 frames
    s_tts_jb = tts_jb_create(s_frame_samples, 6, 12);
    if (!s_tts_jb) {
        ESP_LOGE(TAG, "Failed to create TTS jitter buffer");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Audio processor initialized successfully");
    
    return ESP_OK;
}

esp_err_t audio_processor_start_capture(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_capture_active) {
        ESP_LOGW(TAG, "Audio capture already active");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting audio capture...");
    
    // Enable RX channel
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx_handle), TAG, "Failed to enable RX channel");
    
    s_capture_active = true;
    
    // Create capture task
    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_capture_task,
        "audio_capture",
        4096,
        NULL,
        s_config.task_priority,
        &s_capture_task_handle,
        s_config.task_core
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio capture task");
        s_capture_active = false;
        i2s_channel_disable(s_rx_handle);
        return ESP_FAIL;
    }
    
    if (s_event_callback) {
        s_event_callback(AUDIO_EVENT_STARTED, NULL, 0);
    }
    
    ESP_LOGI(TAG, "Audio capture started");
    return ESP_OK;
}

esp_err_t audio_processor_stop_capture(void)
{
    if (!s_capture_active) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping audio capture...");
    
    s_capture_active = false;
    
    // Wait for task to finish
    if (s_capture_task_handle) {
        // Task will delete itself
        s_capture_task_handle = NULL;
    }
    
    // Disable RX channel
    i2s_channel_disable(s_rx_handle);
    
    if (s_event_callback) {
        s_event_callback(AUDIO_EVENT_STOPPED, NULL, 0);
    }
    
    ESP_LOGI(TAG, "Audio capture stopped");
    return ESP_OK;
}

esp_err_t audio_processor_start_playback(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Audio processor not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_playback_active) {
        ESP_LOGW(TAG, "Audio playback already active");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting audio playback...");
    
    // Enable TX channel
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "Failed to enable TX channel");
    
    s_playback_active = true;
    
    // Create playback task
    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_playback_task,
        "audio_playback",
        4096,
        NULL,
        s_config.task_priority - 1,
        &s_playback_task_handle,
        s_config.task_core
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio playback task");
        s_playback_active = false;
        i2s_channel_disable(s_tx_handle);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio playback started");
    return ESP_OK;
}

esp_err_t audio_processor_stop_playback(void)
{
    if (!s_playback_active) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping audio playback...");
    
    s_playback_active = false;
    
    // Wait for task to finish
    if (s_playback_task_handle) {
        s_playback_task_handle = NULL;
    }
    
    // Disable TX channel
    i2s_channel_disable(s_tx_handle);
    
    ESP_LOGI(TAG, "Audio playback stopped");
    return ESP_OK;
}

esp_err_t audio_processor_set_callback(audio_event_callback_t callback)
{
    s_event_callback = callback;
    return ESP_OK;
}

esp_err_t audio_processor_get_buffer(uint8_t **buffer, size_t *length)
{
    if (!buffer || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get data from ring buffer
    size_t item_size;
    uint8_t *item = (uint8_t *)xRingbufferReceive(s_ringbuf_handle, &item_size, 0);
    
    if (item) {
        *buffer = item;
        *length = item_size;
        return ESP_OK;
    } else {
        *buffer = NULL;
        *length = 0;
        return ESP_ERR_NOT_FOUND;
    }
}

esp_err_t audio_processor_release_buffer(void)
{
    // This would be called after processing the buffer
    // Implementation depends on ring buffer usage
    return ESP_OK;
}

esp_err_t audio_processor_write_data(const uint8_t *data, size_t length)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_tts_jb) {
        return ESP_ERR_INVALID_STATE;
    }

    // Enqueue into jitter buffer; ensure 16-bit alignment
    if (length % 2 != 0) {
        length -= 1; // drop odd byte if any
    }
    size_t samples = length / sizeof(int16_t);
    size_t accepted = tts_jb_push(s_tts_jb, (const int16_t *)data, samples);
    if (accepted == 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

//=============================================================================
// HOWDYTTS INTEGRATION FUNCTIONS
//=============================================================================

esp_err_t audio_processor_configure_howdytts(const audio_howdytts_config_t *howdy_config)
{
    if (!howdy_config) {
        ESP_LOGE(TAG, "Invalid HowdyTTS configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Configuring HowdyTTS integration...");
    ESP_LOGI(TAG, "  UDP Streaming: %s", howdy_config->enable_howdytts_streaming ? "enabled" : "disabled");
    ESP_LOGI(TAG, "  OPUS Encoding: %s (level %d)", howdy_config->enable_opus_encoding ? "enabled" : "disabled", 
             howdy_config->opus_compression_level);
    ESP_LOGI(TAG, "  WebSocket Fallback: %s", howdy_config->enable_websocket_fallback ? "enabled" : "disabled");
    
    // Copy configuration
    memcpy(&s_howdytts_config, howdy_config, sizeof(audio_howdytts_config_t));
    s_howdytts_enabled = howdy_config->enable_howdytts_streaming;
    s_dual_protocol_mode = howdy_config->enable_websocket_fallback;
    
    // Reset statistics
    s_frames_processed = 0;
    s_total_process_time_us = 0;
    
    ESP_LOGI(TAG, "HowdyTTS integration configured successfully");
    return ESP_OK;
}

esp_err_t audio_processor_set_dual_protocol(bool enable_dual_mode)
{
    s_dual_protocol_mode = enable_dual_mode;
    
    ESP_LOGI(TAG, "Dual protocol mode %s", enable_dual_mode ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t audio_processor_switch_protocol(bool use_websocket)
{
    if (!s_dual_protocol_mode) {
        ESP_LOGW(TAG, "Cannot switch protocol - dual mode not enabled");
        return ESP_ERR_INVALID_STATE;
    }
    
    const char* old_protocol = s_websocket_active ? "WebSocket" : "UDP";
    const char* new_protocol = use_websocket ? "WebSocket" : "UDP";
    
    if (s_websocket_active != use_websocket) {
        ESP_LOGI(TAG, "Switching audio protocol: %s -> %s", old_protocol, new_protocol);
        s_websocket_active = use_websocket;
        
        // Note: Actual protocol switching would be handled by the HowdyTTS network integration
        // This just tracks the current mode for statistics and coordination
    } else {
        ESP_LOGD(TAG, "Protocol switch requested but already using %s", new_protocol);
    }
    
    return ESP_OK;
}

esp_err_t audio_processor_get_stats(uint32_t *frames_processed, float *avg_latency_ms, uint8_t *protocol_active)
{
    if (!frames_processed || !avg_latency_ms || !protocol_active) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *frames_processed = s_frames_processed;
    
    if (s_frames_processed > 0) {
        *avg_latency_ms = (float)(s_total_process_time_us / s_frames_processed) / 1000.0f;
    } else {
        *avg_latency_ms = 0.0f;
    }
    
    *protocol_active = s_websocket_active ? 1 : 0;
    
    return ESP_OK;
}

esp_err_t audio_processor_get_playback_depth(size_t *out_frames)
{
    if (!out_frames) return ESP_ERR_INVALID_ARG;
    if (!s_tts_jb) {
        *out_frames = 0;
        return ESP_OK;
    }
    *out_frames = tts_jb_depth(s_tts_jb);
    return ESP_OK;
}
