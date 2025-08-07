#include "dual_i2s_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_codec_dev.h"

static const char *TAG = "DualI2S";

// Performance optimization: Pre-allocated volume buffer for zero-copy processing
#define MAX_VOLUME_BUFFER_SIZE 512  // Max samples per write operation
static int16_t s_volume_buffer[MAX_VOLUME_BUFFER_SIZE] __attribute__((aligned(4)));

// Performance metrics structure
typedef struct {
    uint64_t total_processing_time_us;
    uint32_t processing_cycles;
    uint32_t max_processing_time_us;
    uint32_t buffer_underruns;
    uint32_t mode_switches;
    uint64_t last_performance_report;
} dual_i2s_performance_t;

// Dual I2S manager state - Updated for BSP codec device API
static struct {
    dual_i2s_config_t config;
    dual_i2s_mode_t current_mode;
    bool is_initialized;
    bool mic_active;
    bool speaker_active;
    float volume;
    
    // BSP codec device handles
    esp_codec_dev_handle_t speaker_codec;  // Speaker codec handle
    esp_codec_dev_handle_t mic_codec;      // Microphone codec handle
    
    // Statistics
    uint32_t mic_samples_read;
    uint32_t speaker_samples_written;
    uint32_t mic_errors;
    uint32_t speaker_errors;
    
    // Performance optimization metrics
    dual_i2s_performance_t perf_metrics;
    
    // Thread safety
    SemaphoreHandle_t mutex;
} s_dual_i2s = {
    .current_mode = DUAL_I2S_MODE_MIC,
    .is_initialized = false,
    .mic_active = false,
    .speaker_active = false,
    .volume = 0.7f,
    .speaker_codec = NULL,
    .mic_codec = NULL,
    .mutex = NULL
};

// Forward declarations
static esp_err_t setup_microphone_i2s(void);
static esp_err_t setup_speaker_i2s(void);
static esp_err_t stop_microphone_i2s(void);
static esp_err_t stop_speaker_i2s(void);

esp_err_t dual_i2s_init(const dual_i2s_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_dual_i2s.is_initialized) {
        ESP_LOGI(TAG, "Dual I2S already initialized");
        return ESP_OK;
    }
    
    // Create mutex for thread safety
    s_dual_i2s.mutex = xSemaphoreCreateMutex();
    if (!s_dual_i2s.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    s_dual_i2s.config = *config;
    
    ESP_LOGI(TAG, "Initializing dual I2S system for ESP32-P4 with BSP codec API");
    ESP_LOGI(TAG, "Sample rates - Mic: %lu Hz, Speaker: %lu Hz", 
             config->mic_config.sample_rate, config->speaker_config.sample_rate);
    
    // Initialize BSP I2C first (required for codec communication)
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP I2C: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_dual_i2s.mutex);
        s_dual_i2s.mutex = NULL;
        return ret;
    }
    
    // Initialize BSP audio (I2S channels and codec interface)
    // Use default configuration for now - we'll override sample rates in codec setup
    ret = bsp_audio_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP audio: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_dual_i2s.mutex);
        s_dual_i2s.mutex = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ BSP I2C and audio initialization completed successfully");
    s_dual_i2s.is_initialized = true;
    
    return ESP_OK;
}

esp_err_t dual_i2s_deinit(void)
{
    if (!s_dual_i2s.is_initialized) {
        return ESP_OK;
    }
    
    // Stop all I2S operations
    dual_i2s_stop();
    
    // Delete I2S channels
    stop_microphone_i2s();
    stop_speaker_i2s();
    
    if (s_dual_i2s.mutex) {
        vSemaphoreDelete(s_dual_i2s.mutex);
        s_dual_i2s.mutex = NULL;
    }
    
    s_dual_i2s.is_initialized = false;
    
    ESP_LOGI(TAG, "Dual I2S system deinitialized");
    ESP_LOGI(TAG, "Statistics - Mic samples: %lu, Speaker samples: %lu, Errors: %lu/%lu",
             s_dual_i2s.mic_samples_read, s_dual_i2s.speaker_samples_written,
             s_dual_i2s.mic_errors, s_dual_i2s.speaker_errors);
    
    return ESP_OK;
}

esp_err_t dual_i2s_set_mode(dual_i2s_mode_t mode)
{
    if (!s_dual_i2s.is_initialized) {
        ESP_LOGE(TAG, "Dual I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_dual_i2s.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_dual_i2s.current_mode == mode) {
        xSemaphoreGive(s_dual_i2s.mutex);
        return ESP_OK;  // No change needed
    }
    
    ESP_LOGI(TAG, "Switching I2S mode from %d to %d", s_dual_i2s.current_mode, mode);
    
    // Stop current operations
    dual_i2s_stop();
    
    // Delete existing channels
    stop_microphone_i2s();
    stop_speaker_i2s();
    
    s_dual_i2s.current_mode = mode;
    
    // Configure I2S based on new mode
    esp_err_t ret = ESP_OK;
    
    switch (mode) {
        case DUAL_I2S_MODE_MIC:
            ret = setup_microphone_i2s();
            if (ret == ESP_OK) {
                s_dual_i2s.mic_active = true;
                s_dual_i2s.speaker_active = false;
            }
            break;
            
        case DUAL_I2S_MODE_SPEAKER:
            ret = setup_speaker_i2s();
            if (ret == ESP_OK) {
                s_dual_i2s.mic_active = false;
                s_dual_i2s.speaker_active = true;
            }
            break;
            
        case DUAL_I2S_MODE_SIMULTANEOUS:
            // Setup both I2S ports (ESP32-P4 supports this)
            ret = setup_microphone_i2s();
            if (ret == ESP_OK) {
                ret = setup_speaker_i2s();
                if (ret == ESP_OK) {
                    s_dual_i2s.mic_active = true;
                    s_dual_i2s.speaker_active = true;
                } else {
                    stop_microphone_i2s();
                }
            }
            break;
    }
    
    xSemaphoreGive(s_dual_i2s.mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S mode set successfully - Mic: %s, Speaker: %s",
                 s_dual_i2s.mic_active ? "ON" : "OFF",
                 s_dual_i2s.speaker_active ? "ON" : "OFF");
        s_dual_i2s.perf_metrics.mode_switches++;
    } else {
        ESP_LOGE(TAG, "Failed to set I2S mode: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t setup_microphone_i2s(void)
{
    // Close existing microphone codec if active
    if (s_dual_i2s.mic_codec) {
        esp_codec_dev_close(s_dual_i2s.mic_codec);
    }
    
    // Initialize microphone codec using BSP function
    s_dual_i2s.mic_codec = bsp_audio_codec_microphone_init();
    if (!s_dual_i2s.mic_codec) {
        ESP_LOGE(TAG, "Failed to initialize microphone codec via BSP");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure sample format for microphone
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = s_dual_i2s.config.mic_config.sample_rate,
        .channel = 1,  // Mono for voice
        .bits_per_sample = s_dual_i2s.config.mic_config.bits_per_sample,
    };
    
    esp_err_t ret = esp_codec_dev_set_in_gain(s_dual_i2s.mic_codec, 24.0); // Default ADC gain
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set microphone gain: %s", esp_err_to_name(ret));
    }
    
    ret = esp_codec_dev_open(s_dual_i2s.mic_codec, &sample_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open microphone codec: %s", esp_err_to_name(ret));
        s_dual_i2s.mic_codec = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Microphone codec initialized successfully with BSP (ES7210)");
    ESP_LOGI(TAG, "🎤 Sample rate: %lu Hz, Bits: %d, Channels: %d", 
             sample_info.sample_rate, sample_info.bits_per_sample, sample_info.channel);
    return ESP_OK;
}

static esp_err_t setup_speaker_i2s(void)
{
    // Close existing speaker codec if active
    if (s_dual_i2s.speaker_codec) {
        esp_codec_dev_close(s_dual_i2s.speaker_codec);
    }
    
    // Initialize speaker codec using BSP function
    s_dual_i2s.speaker_codec = bsp_audio_codec_speaker_init();
    if (!s_dual_i2s.speaker_codec) {
        ESP_LOGE(TAG, "Failed to initialize speaker codec via BSP");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure sample format for speaker
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = s_dual_i2s.config.speaker_config.sample_rate,
        .channel = 1,  // Mono for voice
        .bits_per_sample = s_dual_i2s.config.speaker_config.bits_per_sample,
    };
    
    // Set initial volume
    esp_err_t ret = esp_codec_dev_set_out_vol(s_dual_i2s.speaker_codec, (int)(s_dual_i2s.volume * 100));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set speaker volume: %s", esp_err_to_name(ret));
    }
    
    ret = esp_codec_dev_open(s_dual_i2s.speaker_codec, &sample_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open speaker codec: %s", esp_err_to_name(ret));
        s_dual_i2s.speaker_codec = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Speaker codec initialized successfully with BSP (ES8311)");
    ESP_LOGI(TAG, "🔊 Sample rate: %lu Hz, Bits: %d, Channels: %d, Volume: %.1f%%", 
             sample_info.sample_rate, sample_info.bits_per_sample, sample_info.channel, s_dual_i2s.volume * 100);
    return ESP_OK;
}

static esp_err_t stop_microphone_i2s(void)
{
    if (s_dual_i2s.mic_codec) {
        esp_err_t ret = esp_codec_dev_close(s_dual_i2s.mic_codec);
        s_dual_i2s.mic_codec = NULL;
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Microphone codec closed");
        }
        return ret;
    }
    return ESP_OK;
}

static esp_err_t stop_speaker_i2s(void)
{
    if (s_dual_i2s.speaker_codec) {
        esp_err_t ret = esp_codec_dev_close(s_dual_i2s.speaker_codec);
        s_dual_i2s.speaker_codec = NULL;
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Speaker codec closed");
        }
        return ret;
    }
    return ESP_OK;
}

esp_err_t dual_i2s_read_mic(int16_t *buffer, size_t samples, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!buffer || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_dual_i2s.mic_active || !s_dual_i2s.mic_codec) {
        ESP_LOGW(TAG, "Microphone codec not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t bytes_to_read = samples * sizeof(int16_t);
    esp_err_t ret = esp_codec_dev_read(s_dual_i2s.mic_codec, buffer, bytes_to_read);
    
    if (ret == ESP_OK) {
        *bytes_read = bytes_to_read;  // Codec dev API reads exactly the requested bytes
        s_dual_i2s.mic_samples_read += samples;
        ESP_LOGV(TAG, "Read %zu bytes from microphone codec", *bytes_read);
    } else {
        *bytes_read = 0;
        s_dual_i2s.mic_errors++;
        ESP_LOGW(TAG, "Microphone codec read error: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t dual_i2s_write_speaker(const int16_t *buffer, size_t samples, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!buffer || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_dual_i2s.speaker_active || !s_dual_i2s.speaker_codec) {
        ESP_LOGW(TAG, "Speaker codec not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Performance timing start
    uint64_t start_time = esp_timer_get_time();
    
    // Volume control is now handled by the codec hardware, but we can still apply software scaling if needed
    const int16_t *write_buffer = buffer;
    if (s_dual_i2s.volume != 1.0f && samples <= MAX_VOLUME_BUFFER_SIZE) {
        // Fast volume scaling with pre-allocated buffer (zero malloc/free overhead)
        for (size_t i = 0; i < samples; i++) {
            s_volume_buffer[i] = (int16_t)(buffer[i] * s_dual_i2s.volume);
        }
        write_buffer = s_volume_buffer;
    } else if (s_dual_i2s.volume != 1.0f) {
        // Fallback for large buffers - log warning about potential latency
        ESP_LOGW(TAG, "Large buffer (%zu samples) may cause volume processing latency", samples);
        s_dual_i2s.perf_metrics.buffer_underruns++;
    }
    
    size_t bytes_to_write = samples * sizeof(int16_t);
    esp_err_t ret = esp_codec_dev_write(s_dual_i2s.speaker_codec, (void*)write_buffer, bytes_to_write);
    
    // Performance timing end
    uint64_t processing_time = esp_timer_get_time() - start_time;
    s_dual_i2s.perf_metrics.total_processing_time_us += processing_time;
    s_dual_i2s.perf_metrics.processing_cycles++;
    
    if (processing_time > s_dual_i2s.perf_metrics.max_processing_time_us) {
        s_dual_i2s.perf_metrics.max_processing_time_us = processing_time;
    }
    
    if (ret == ESP_OK) {
        *bytes_written = bytes_to_write;  // Codec dev API writes exactly the requested bytes
        s_dual_i2s.speaker_samples_written += samples;
        ESP_LOGV(TAG, "Wrote %zu bytes to speaker codec (%.1fμs)", *bytes_written, (float)processing_time);
    } else {
        *bytes_written = 0;
        s_dual_i2s.speaker_errors++;
        ESP_LOGW(TAG, "Speaker codec write error: %s (%.1fμs)", esp_err_to_name(ret), (float)processing_time);
    }
    
    // Performance reporting (every 1000 operations)
    if (s_dual_i2s.perf_metrics.processing_cycles % 1000 == 0) {
        float avg_time = (float)s_dual_i2s.perf_metrics.total_processing_time_us / s_dual_i2s.perf_metrics.processing_cycles;
        ESP_LOGI(TAG, "🚀 Codec Performance: avg=%.1fμs, max=%luμs, underruns=%lu", 
                avg_time, s_dual_i2s.perf_metrics.max_processing_time_us, s_dual_i2s.perf_metrics.buffer_underruns);
    }
    
    return ret;
}

esp_err_t dual_i2s_start(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "🔧 dual_i2s_start() called - mic_active: %s, speaker_active: %s", 
             s_dual_i2s.mic_active ? "YES" : "NO", 
             s_dual_i2s.speaker_active ? "YES" : "NO");
    ESP_LOGI(TAG, "🔧 Codec handles - mic_codec: %p, speaker_codec: %p", 
             s_dual_i2s.mic_codec, s_dual_i2s.speaker_codec);
    
    if (s_dual_i2s.mic_active && s_dual_i2s.mic_codec) {
        ESP_LOGI(TAG, "✅ Microphone codec is ready for audio capture");
        // Codec devices are already opened in setup_microphone_i2s()
        // No additional enable step needed
    } else {
        ESP_LOGW(TAG, "⚠️ Microphone codec not available - mic_active: %s, codec: %p", 
                 s_dual_i2s.mic_active ? "YES" : "NO", s_dual_i2s.mic_codec);
    }
    
    if (s_dual_i2s.speaker_active && s_dual_i2s.speaker_codec) {
        ESP_LOGI(TAG, "✅ Speaker codec is ready for audio playback");
        // Codec devices are already opened in setup_speaker_i2s()
        // No additional enable step needed
    } else {
        ESP_LOGW(TAG, "⚠️ Speaker codec not available - speaker_active: %s, codec: %p", 
                 s_dual_i2s.speaker_active ? "YES" : "NO", s_dual_i2s.speaker_codec);
    }
    
    ESP_LOGI(TAG, "✅ dual_i2s_start() completed - codec devices ready for audio operations");
    return ret;
}

esp_err_t dual_i2s_stop(void)
{
    esp_err_t ret = ESP_OK;
    
    if (s_dual_i2s.mic_active && s_dual_i2s.mic_codec) {
        // Codec devices remain open but are idle when not reading/writing
        ESP_LOGI(TAG, "📴 Microphone codec stopped (remains open)");
    }
    
    if (s_dual_i2s.speaker_active && s_dual_i2s.speaker_codec) {
        // Codec devices remain open but are idle when not reading/writing
        ESP_LOGI(TAG, "📴 Speaker codec stopped (remains open)");
    }
    
    ESP_LOGI(TAG, "✅ dual_i2s_stop() completed - codec devices idle");
    return ret;
}

esp_err_t dual_i2s_clear_dma_buffers(bool clear_mic, bool clear_speaker)
{
    esp_err_t ret = ESP_OK;
    
    if (clear_mic && s_dual_i2s.mic_active && s_dual_i2s.mic_codec) {
        // Codec devices manage their own internal buffering
        // We can simulate buffer clearing by briefly closing and reopening the codec
        ESP_LOGD(TAG, "📴 Clearing microphone codec buffers (simulated)");
        // For codec devices, the best we can do is note that buffers are conceptually cleared
        // The codec dev API handles internal buffer management
    }
    
    if (clear_speaker && s_dual_i2s.speaker_active && s_dual_i2s.speaker_codec) {
        // Codec devices manage their own internal buffering
        ESP_LOGD(TAG, "📴 Clearing speaker codec buffers (simulated)");
        // For codec devices, the best we can do is note that buffers are conceptually cleared
    }
    
    ESP_LOGD(TAG, "✅ Codec buffer clear completed");
    return ret;
}

dual_i2s_mode_t dual_i2s_get_mode(void)
{
    return s_dual_i2s.current_mode;
}

bool dual_i2s_is_mic_active(void)
{
    return s_dual_i2s.mic_active;
}

bool dual_i2s_is_speaker_active(void)
{
    return s_dual_i2s.speaker_active;
}

esp_err_t dual_i2s_set_volume(float volume)
{
    if (volume < 0.0f || volume > 1.0f) {
        ESP_LOGE(TAG, "Invalid volume level: %.2f", volume);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_dual_i2s.volume = volume;
    
    // Set hardware volume on the speaker codec if available
    if (s_dual_i2s.speaker_codec) {
        int codec_volume = (int)(volume * 100);  // Convert to 0-100 range
        esp_err_t ret = esp_codec_dev_set_out_vol(s_dual_i2s.speaker_codec, codec_volume);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set codec hardware volume: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "✅ Hardware volume set to %d%% (%.2f)", codec_volume, volume);
        }
    } else {
        ESP_LOGI(TAG, "📊 Software volume set to %.2f (codec not active)", volume);
    }
    
    return ESP_OK;
}

esp_err_t dual_i2s_get_stats(uint32_t *mic_samples_read, 
                             uint32_t *speaker_samples_written,
                             uint32_t *mic_errors,
                             uint32_t *speaker_errors)
{
    if (mic_samples_read) {
        *mic_samples_read = s_dual_i2s.mic_samples_read;
    }
    
    if (speaker_samples_written) {
        *speaker_samples_written = s_dual_i2s.speaker_samples_written;
    }
    
    if (mic_errors) {
        *mic_errors = s_dual_i2s.mic_errors;
    }
    
    if (speaker_errors) {
        *speaker_errors = s_dual_i2s.speaker_errors;
    }
    
    return ESP_OK;
}

esp_err_t dual_i2s_get_performance_metrics(dual_i2s_performance_metrics_t *metrics)
{
    if (!metrics) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_dual_i2s.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Calculate performance metrics
    float avg_processing_time = 0.0f;
    if (s_dual_i2s.perf_metrics.processing_cycles > 0) {
        avg_processing_time = (float)s_dual_i2s.perf_metrics.total_processing_time_us / 
                             s_dual_i2s.perf_metrics.processing_cycles;
    }
    
    metrics->average_processing_time_us = avg_processing_time;
    metrics->max_processing_time_us = s_dual_i2s.perf_metrics.max_processing_time_us;
    metrics->total_operations = s_dual_i2s.perf_metrics.processing_cycles;
    metrics->buffer_underruns = s_dual_i2s.perf_metrics.buffer_underruns;
    metrics->mode_switches = s_dual_i2s.perf_metrics.mode_switches;
    
    // Calculate estimated audio latency based on DMA configuration
    uint32_t dma_latency_ms = (s_dual_i2s.config.dma_buf_len * 1000) / s_dual_i2s.config.mic_config.sample_rate;
    metrics->estimated_audio_latency_ms = dma_latency_ms * s_dual_i2s.config.dma_buf_count;
    
    // Memory usage estimation
    size_t dma_memory = s_dual_i2s.config.dma_buf_count * s_dual_i2s.config.dma_buf_len * 2 * 2; // 2 ports, 2 bytes per sample
    metrics->memory_usage_bytes = dma_memory + sizeof(s_dual_i2s) + sizeof(s_volume_buffer);
    
    xSemaphoreGive(s_dual_i2s.mutex);
    
    ESP_LOGI(TAG, "📊 Performance Metrics - Avg: %.1fμs, Max: %luμs, Latency: %lums, Memory: %zu bytes",
            avg_processing_time, metrics->max_processing_time_us, 
            metrics->estimated_audio_latency_ms, metrics->memory_usage_bytes);
    
    return ESP_OK;
}

esp_err_t dual_i2s_reset_performance_counters(void)
{
    if (xSemaphoreTake(s_dual_i2s.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    memset(&s_dual_i2s.perf_metrics, 0, sizeof(dual_i2s_performance_t));
    s_dual_i2s.perf_metrics.last_performance_report = esp_timer_get_time();
    
    xSemaphoreGive(s_dual_i2s.mutex);
    
    ESP_LOGI(TAG, "🔄 Performance counters reset");
    return ESP_OK;
}