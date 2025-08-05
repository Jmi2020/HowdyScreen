#include "tts_audio_handler.h"
#include "audio_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "TTSAudioHandler";

// TTS Audio state
static struct {
    bool initialized;
    bool playing;
    tts_audio_config_t config;
    tts_audio_event_callback_t callback;
    void *user_data;
    
    // Audio processing
    QueueHandle_t audio_queue;
    TaskHandle_t playback_task_handle;
    SemaphoreHandle_t state_mutex;
    
    // Statistics
    uint32_t chunks_played;
    uint32_t bytes_played;
    uint32_t buffer_underruns;
    
} s_tts_audio = {0};

// Audio chunk for queue
typedef struct {
    uint8_t *data;
    size_t length;
} tts_audio_chunk_t;

// Forward declarations
static void tts_playback_task(void *pvParameters);
static esp_err_t apply_volume(uint8_t *audio_data, size_t length, float volume);
static void notify_event(tts_audio_event_t event, const void *data, size_t data_len);

esp_err_t tts_audio_init(const tts_audio_config_t *config, 
                        tts_audio_event_callback_t callback, 
                        void *user_data)
{
    if (s_tts_audio.initialized) {
        ESP_LOGI(TAG, "TTS audio handler already initialized");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing TTS audio handler");
    ESP_LOGI(TAG, "Sample rate: %lu Hz, Channels: %d, Bits: %d", 
             config->sample_rate, config->channels, config->bits_per_sample);
    ESP_LOGI(TAG, "Volume: %.2f, Buffer size: %zu bytes", 
             config->volume, config->buffer_size);
    
    // Copy configuration
    s_tts_audio.config = *config;
    s_tts_audio.callback = callback;
    s_tts_audio.user_data = user_data;
    
    // Create mutex for state protection
    s_tts_audio.state_mutex = xSemaphoreCreateMutex();
    if (!s_tts_audio.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Create audio queue for TTS chunks
    s_tts_audio.audio_queue = xQueueCreate(10, sizeof(tts_audio_chunk_t));
    if (!s_tts_audio.audio_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        vSemaphoreDelete(s_tts_audio.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create TTS playback task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        tts_playback_task,
        "tts_playback",
        8192,  // Large stack for audio processing
        NULL,
        5,     // Medium priority
        &s_tts_audio.playback_task_handle,
        1      // Core 1 (separate from network tasks)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS playback task");
        vQueueDelete(s_tts_audio.audio_queue);
        vSemaphoreDelete(s_tts_audio.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize statistics
    s_tts_audio.chunks_played = 0;
    s_tts_audio.bytes_played = 0;
    s_tts_audio.buffer_underruns = 0;
    s_tts_audio.playing = false;
    
    s_tts_audio.initialized = true;
    ESP_LOGI(TAG, "TTS audio handler initialized successfully");
    
    return ESP_OK;
}

esp_err_t tts_audio_deinit(void)
{
    if (!s_tts_audio.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing TTS audio handler");
    
    // Stop playback if active
    tts_audio_stop_playback();
    
    // Delete task
    if (s_tts_audio.playback_task_handle) {
        vTaskDelete(s_tts_audio.playback_task_handle);
        s_tts_audio.playback_task_handle = NULL;
    }
    
    // Clean up resources
    if (s_tts_audio.audio_queue) {
        // Clear any remaining audio chunks
        tts_audio_chunk_t chunk;
        while (xQueueReceive(s_tts_audio.audio_queue, &chunk, 0) == pdTRUE) {
            free(chunk.data);
        }
        vQueueDelete(s_tts_audio.audio_queue);
        s_tts_audio.audio_queue = NULL;
    }
    
    if (s_tts_audio.state_mutex) {
        vSemaphoreDelete(s_tts_audio.state_mutex);
        s_tts_audio.state_mutex = NULL;
    }
    
    s_tts_audio.initialized = false;
    ESP_LOGI(TAG, "TTS audio handler deinitialized");
    
    return ESP_OK;
}

esp_err_t tts_audio_play_chunk(const uint8_t *audio_data, size_t data_len)
{
    if (!s_tts_audio.initialized || !audio_data || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Allocate memory for audio chunk
    uint8_t *chunk_data = malloc(data_len);
    if (!chunk_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio chunk (%zu bytes)", data_len);
        return ESP_ERR_NO_MEM;
    }
    
    // Copy audio data
    memcpy(chunk_data, audio_data, data_len);
    
    // Apply volume scaling
    apply_volume(chunk_data, data_len, s_tts_audio.config.volume);
    
    // Create chunk structure
    tts_audio_chunk_t chunk = {
        .data = chunk_data,
        .length = data_len
    };
    
    // Queue audio chunk for playback
    if (xQueueSend(s_tts_audio.audio_queue, &chunk, pdMS_TO_TICKS(s_tts_audio.config.buffer_timeout_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue audio chunk - buffer full");
        free(chunk_data);
        s_tts_audio.buffer_underruns++;
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGD(TAG, "Queued TTS audio chunk: %zu bytes", data_len);
    return ESP_OK;
}

esp_err_t tts_audio_start_playback(void)
{
    if (!s_tts_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_tts_audio.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_tts_audio.playing) {
        ESP_LOGI(TAG, "Starting TTS playback");
        
        // Start audio processor playback
        esp_err_t ret = audio_processor_start_playback();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start audio processor playback: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_tts_audio.state_mutex);
            return ret;
        }
        
        s_tts_audio.playing = true;
        notify_event(TTS_AUDIO_EVENT_STARTED, NULL, 0);
    }
    
    xSemaphoreGive(s_tts_audio.state_mutex);
    return ESP_OK;
}

esp_err_t tts_audio_stop_playback(void)
{
    if (!s_tts_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_tts_audio.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_tts_audio.playing) {
        ESP_LOGI(TAG, "Stopping TTS playback");
        
        s_tts_audio.playing = false;
        
        // Clear audio queue
        tts_audio_clear_buffer();
        
        // Stop audio processor playback
        audio_processor_stop_playback();
        
        notify_event(TTS_AUDIO_EVENT_FINISHED, NULL, 0);
    }
    
    xSemaphoreGive(s_tts_audio.state_mutex);
    return ESP_OK;
}

esp_err_t tts_audio_set_volume(float volume)
{
    if (!s_tts_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (volume < 0.0f || volume > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_tts_audio.config.volume = volume;
    ESP_LOGI(TAG, "TTS volume set to %.2f", volume);
    
    return ESP_OK;
}

esp_err_t tts_audio_get_volume(float *volume)
{
    if (!s_tts_audio.initialized || !volume) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *volume = s_tts_audio.config.volume;
    return ESP_OK;
}

bool tts_audio_is_playing(void)
{
    return s_tts_audio.initialized && s_tts_audio.playing;
}

esp_err_t tts_audio_clear_buffer(void)
{
    if (!s_tts_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clear all queued audio chunks
    tts_audio_chunk_t chunk;
    int cleared_chunks = 0;
    
    while (xQueueReceive(s_tts_audio.audio_queue, &chunk, 0) == pdTRUE) {
        free(chunk.data);
        cleared_chunks++;
    }
    
    if (cleared_chunks > 0) {
        ESP_LOGI(TAG, "Cleared %d audio chunks from buffer", cleared_chunks);
        notify_event(TTS_AUDIO_EVENT_BUFFER_EMPTY, NULL, 0);
    }
    
    return ESP_OK;
}

esp_err_t tts_audio_get_stats(uint32_t *chunks_played, 
                             uint32_t *bytes_played, 
                             uint32_t *buffer_underruns)
{
    if (!s_tts_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (chunks_played) *chunks_played = s_tts_audio.chunks_played;
    if (bytes_played) *bytes_played = s_tts_audio.bytes_played;
    if (buffer_underruns) *buffer_underruns = s_tts_audio.buffer_underruns;
    
    return ESP_OK;
}

static void tts_playback_task(void *pvParameters)
{
    ESP_LOGI(TAG, "TTS playback task started");
    
    tts_audio_chunk_t chunk;
    
    while (1) {
        // Wait for audio chunks to play
        if (xQueueReceive(s_tts_audio.audio_queue, &chunk, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            if (s_tts_audio.playing) {
                // Write audio data to processor for playback
                esp_err_t ret = audio_processor_write_data(chunk.data, chunk.length);
                if (ret == ESP_OK) {
                    s_tts_audio.chunks_played++;
                    s_tts_audio.bytes_played += chunk.length;
                    
                    ESP_LOGD(TAG, "Played TTS chunk: %zu bytes", chunk.length);
                    notify_event(TTS_AUDIO_EVENT_CHUNK_PLAYED, chunk.data, chunk.length);
                } else {
                    ESP_LOGE(TAG, "Failed to write audio data: %s", esp_err_to_name(ret));
                    notify_event(TTS_AUDIO_EVENT_ERROR, &ret, sizeof(ret));
                }
            }
            
            // Free chunk memory
            free(chunk.data);
        }
    }
    
    ESP_LOGI(TAG, "TTS playback task ended");
    vTaskDelete(NULL);
}

static esp_err_t apply_volume(uint8_t *audio_data, size_t length, float volume)
{
    if (!audio_data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Apply volume scaling to 16-bit PCM data
    int16_t *samples = (int16_t *)audio_data;
    size_t sample_count = length / sizeof(int16_t);
    
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = (int16_t)(samples[i] * volume);
    }
    
    return ESP_OK;
}

static void notify_event(tts_audio_event_t event, const void *data, size_t data_len)
{
    if (s_tts_audio.callback) {
        s_tts_audio.callback(event, data, data_len, s_tts_audio.user_data);
    }
}