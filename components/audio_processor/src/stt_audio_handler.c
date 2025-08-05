#include "stt_audio_handler.h"
#include "audio_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <string.h>
#include <math.h>

static const char *TAG = "STTAudioHandler";

// STT Audio state
static struct {
    bool initialized;
    bool capturing;
    bool voice_detected;
    stt_audio_config_t config;
    stt_audio_event_callback_t callback;
    void *user_data;
    
    // Audio processing
    TaskHandle_t capture_task_handle;
    SemaphoreHandle_t state_mutex;
    TimerHandle_t vad_timer;
    
    // Audio analysis
    stt_audio_quality_t quality;
    float *audio_buffer;
    size_t buffer_size;
    
    // Voice Activity Detection
    uint32_t voice_start_time;
    uint32_t silence_start_time;
    float recent_levels[10];  // Ring buffer for recent audio levels
    uint8_t level_index;
    
    // Statistics
    uint32_t chunks_captured;
    uint32_t bytes_captured;
    uint32_t voice_segments;
    float avg_level_accumulator;
    uint32_t level_samples;
    
} s_stt_audio = {0};

// Forward declarations
static void stt_capture_task(void *pvParameters);
static void vad_timer_callback(TimerHandle_t xTimer);
static float calculate_rms_level(const int16_t *samples, size_t sample_count);
static float calculate_peak_level(const int16_t *samples, size_t sample_count);
static bool detect_voice_activity(float rms_level);
static esp_err_t apply_gain(int16_t *samples, size_t sample_count, float gain);
static esp_err_t apply_noise_suppression(int16_t *samples, size_t sample_count);
static void notify_event(stt_audio_event_t event, const uint8_t *audio_data, 
                        size_t data_len, const stt_audio_quality_t *quality);

esp_err_t stt_audio_init(const stt_audio_config_t *config,
                        stt_audio_event_callback_t callback,
                        void *user_data)
{
    if (s_stt_audio.initialized) {
        ESP_LOGI(TAG, "STT audio handler already initialized");
        return ESP_OK;
    }
    
    if (!config || !callback) {
        ESP_LOGE(TAG, "Invalid configuration or callback");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing STT audio handler");
    ESP_LOGI(TAG, "Sample rate: %lu Hz, Channels: %d, Bits: %d", 
             config->sample_rate, config->channels, config->bits_per_sample);
    ESP_LOGI(TAG, "Gain: %.2f, Chunk size: %zu, VAD threshold: %.2f", 
             config->gain, config->chunk_size, config->vad_threshold);
    
    // Copy configuration
    s_stt_audio.config = *config;
    s_stt_audio.callback = callback;
    s_stt_audio.user_data = user_data;
    
    // Create mutex for state protection
    s_stt_audio.state_mutex = xSemaphoreCreateMutex();
    if (!s_stt_audio.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate audio buffer for processing
    s_stt_audio.buffer_size = config->chunk_size;
    s_stt_audio.audio_buffer = malloc(s_stt_audio.buffer_size * sizeof(float));
    if (!s_stt_audio.audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vSemaphoreDelete(s_stt_audio.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create VAD timer for voice activity timeout
    s_stt_audio.vad_timer = xTimerCreate("vad_timer", pdMS_TO_TICKS(500), 
                                        pdFALSE, NULL, vad_timer_callback);
    if (!s_stt_audio.vad_timer) {
        ESP_LOGE(TAG, "Failed to create VAD timer");
        free(s_stt_audio.audio_buffer);
        vSemaphoreDelete(s_stt_audio.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create STT capture task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        stt_capture_task,
        "stt_capture",
        8192,  // Large stack for audio processing
        NULL,
        6,     // Higher priority than TTS
        &s_stt_audio.capture_task_handle,
        1      // Core 1 (separate from network tasks)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create STT capture task");
        xTimerDelete(s_stt_audio.vad_timer, 0);
        free(s_stt_audio.audio_buffer);
        vSemaphoreDelete(s_stt_audio.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize statistics and state
    s_stt_audio.chunks_captured = 0;
    s_stt_audio.bytes_captured = 0;
    s_stt_audio.voice_segments = 0;
    s_stt_audio.avg_level_accumulator = 0.0f;
    s_stt_audio.level_samples = 0;
    s_stt_audio.capturing = false;
    s_stt_audio.voice_detected = false;
    s_stt_audio.level_index = 0;
    
    // Initialize quality metrics
    memset(&s_stt_audio.quality, 0, sizeof(s_stt_audio.quality));
    memset(s_stt_audio.recent_levels, 0, sizeof(s_stt_audio.recent_levels));
    
    s_stt_audio.initialized = true;
    ESP_LOGI(TAG, "STT audio handler initialized successfully");
    
    return ESP_OK;
}

esp_err_t stt_audio_deinit(void)
{
    if (!s_stt_audio.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing STT audio handler");
    
    // Stop capture if active
    stt_audio_stop_capture();
    
    // Delete task
    if (s_stt_audio.capture_task_handle) {
        vTaskDelete(s_stt_audio.capture_task_handle);
        s_stt_audio.capture_task_handle = NULL;
    }
    
    // Clean up resources
    if (s_stt_audio.vad_timer) {
        xTimerDelete(s_stt_audio.vad_timer, portMAX_DELAY);
        s_stt_audio.vad_timer = NULL;
    }
    
    if (s_stt_audio.audio_buffer) {
        free(s_stt_audio.audio_buffer);
        s_stt_audio.audio_buffer = NULL;
    }
    
    if (s_stt_audio.state_mutex) {
        vSemaphoreDelete(s_stt_audio.state_mutex);
        s_stt_audio.state_mutex = NULL;
    }
    
    s_stt_audio.initialized = false;
    ESP_LOGI(TAG, "STT audio handler deinitialized");
    
    return ESP_OK;
}

esp_err_t stt_audio_start_capture(void)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_stt_audio.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_stt_audio.capturing) {
        ESP_LOGI(TAG, "Starting STT audio capture");
        
        // Start audio processor capture
        esp_err_t ret = audio_processor_start_capture();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start audio processor capture: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_stt_audio.state_mutex);
            return ret;
        }
        
        s_stt_audio.capturing = true;
        s_stt_audio.silence_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        notify_event(STT_AUDIO_EVENT_STARTED, NULL, 0, NULL);
    }
    
    xSemaphoreGive(s_stt_audio.state_mutex);
    return ESP_OK;
}

esp_err_t stt_audio_stop_capture(void)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_stt_audio.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_stt_audio.capturing) {
        ESP_LOGI(TAG, "Stopping STT audio capture");
        
        s_stt_audio.capturing = false;
        
        // Stop VAD timer
        xTimerStop(s_stt_audio.vad_timer, 0);
        
        // If voice was active, notify end of voice
        if (s_stt_audio.voice_detected) {
            s_stt_audio.voice_detected = false;
            notify_event(STT_AUDIO_EVENT_VOICE_END, NULL, 0, &s_stt_audio.quality);
        }
        
        // Stop audio processor capture
        audio_processor_stop_capture();
        
        notify_event(STT_AUDIO_EVENT_STOPPED, NULL, 0, NULL);
    }
    
    xSemaphoreGive(s_stt_audio.state_mutex);
    return ESP_OK;
}

esp_err_t stt_audio_set_gain(float gain)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gain < 0.5f || gain > 2.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_stt_audio.config.gain = gain;
    ESP_LOGI(TAG, "STT gain set to %.2f", gain);
    
    return ESP_OK;
}

esp_err_t stt_audio_get_gain(float *gain)
{
    if (!s_stt_audio.initialized || !gain) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *gain = s_stt_audio.config.gain;
    return ESP_OK;
}

esp_err_t stt_audio_set_vad_threshold(float threshold)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (threshold < 0.0f || threshold > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_stt_audio.config.vad_threshold = threshold;
    ESP_LOGI(TAG, "STT VAD threshold set to %.2f", threshold);
    
    return ESP_OK;
}

bool stt_audio_is_capturing(void)
{
    return s_stt_audio.initialized && s_stt_audio.capturing;
}

bool stt_audio_is_voice_detected(void)
{
    return s_stt_audio.initialized && s_stt_audio.voice_detected;
}

esp_err_t stt_audio_get_quality(stt_audio_quality_t *quality)
{
    if (!s_stt_audio.initialized || !quality) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *quality = s_stt_audio.quality;
    return ESP_OK;
}

esp_err_t stt_audio_get_stats(uint32_t *chunks_captured,
                             uint32_t *bytes_captured,
                             uint32_t *voice_segments,
                             float *avg_level)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (chunks_captured) *chunks_captured = s_stt_audio.chunks_captured;
    if (bytes_captured) *bytes_captured = s_stt_audio.bytes_captured;
    if (voice_segments) *voice_segments = s_stt_audio.voice_segments;
    if (avg_level) {
        *avg_level = s_stt_audio.level_samples > 0 ? 
                    s_stt_audio.avg_level_accumulator / s_stt_audio.level_samples : 0.0f;
    }
    
    return ESP_OK;
}

esp_err_t stt_audio_trigger_voice_detection(bool force_voice_start)
{
    if (!s_stt_audio.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Manual voice detection trigger: %s", force_voice_start ? "START" : "END");
    
    if (force_voice_start && !s_stt_audio.voice_detected) {
        s_stt_audio.voice_detected = true;
        s_stt_audio.voice_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        s_stt_audio.voice_segments++;
        notify_event(STT_AUDIO_EVENT_VOICE_START, NULL, 0, &s_stt_audio.quality);
    } else if (!force_voice_start && s_stt_audio.voice_detected) {
        s_stt_audio.voice_detected = false;
        s_stt_audio.silence_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        notify_event(STT_AUDIO_EVENT_VOICE_END, NULL, 0, &s_stt_audio.quality);
    }
    
    return ESP_OK;
}

static void stt_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "STT capture task started");
    
    uint8_t *audio_buffer = NULL;
    size_t buffer_length = 0;
    
    while (1) {
        if (s_stt_audio.capturing) {
            // Get audio buffer from processor
            esp_err_t ret = audio_processor_get_buffer(&audio_buffer, &buffer_length);
            if (ret == ESP_OK && audio_buffer && buffer_length > 0) {
                
                // Process audio data
                int16_t *samples = (int16_t *)audio_buffer;
                size_t sample_count = buffer_length / sizeof(int16_t);
                
                // Apply gain
                apply_gain(samples, sample_count, s_stt_audio.config.gain);
                
                // Apply noise suppression if enabled
                if (s_stt_audio.config.noise_suppression) {
                    apply_noise_suppression(samples, sample_count);
                }
                
                // Calculate audio quality metrics
                float rms_level = calculate_rms_level(samples, sample_count);
                float peak_level = calculate_peak_level(samples, sample_count);
                
                // Update quality metrics
                s_stt_audio.quality.rms_level = rms_level;
                s_stt_audio.quality.peak_level = peak_level;
                
                // Voice Activity Detection
                bool voice_active = detect_voice_activity(rms_level);
                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                
                if (voice_active && !s_stt_audio.voice_detected) {
                    // Voice activity started
                    s_stt_audio.voice_detected = true;
                    s_stt_audio.voice_start_time = current_time;
                    s_stt_audio.quality.voice_duration_ms = 0;
                    s_stt_audio.voice_segments++;
                    
                    notify_event(STT_AUDIO_EVENT_VOICE_START, audio_buffer, buffer_length, &s_stt_audio.quality);
                    
                } else if (!voice_active && s_stt_audio.voice_detected) {
                    // Start VAD timer for voice end detection (avoid false positives)
                    xTimerReset(s_stt_audio.vad_timer, 0);
                }
                
                // Update duration metrics
                if (s_stt_audio.voice_detected) {
                    s_stt_audio.quality.voice_duration_ms = current_time - s_stt_audio.voice_start_time;
                    s_stt_audio.quality.silence_duration_ms = 0;
                } else {
                    s_stt_audio.quality.silence_duration_ms = current_time - s_stt_audio.silence_start_time;
                    s_stt_audio.quality.voice_duration_ms = 0;
                }
                
                s_stt_audio.quality.voice_detected = s_stt_audio.voice_detected;
                
                // Update statistics
                s_stt_audio.chunks_captured++;
                s_stt_audio.bytes_captured += buffer_length;
                s_stt_audio.avg_level_accumulator += rms_level;
                s_stt_audio.level_samples++;
                
                // Notify audio chunk ready (always send audio for STT processing)
                notify_event(STT_AUDIO_EVENT_CHUNK_READY, audio_buffer, buffer_length, &s_stt_audio.quality);
                
                // Release buffer
                audio_processor_release_buffer();
                
                ESP_LOGD(TAG, "STT chunk: %zu bytes, RMS: %.3f, Voice: %s", 
                        buffer_length, rms_level, s_stt_audio.voice_detected ? "YES" : "NO");
                
            } else if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Failed to get audio buffer: %s", esp_err_to_name(ret));
                notify_event(STT_AUDIO_EVENT_ERROR, (const uint8_t*)&ret, sizeof(ret), NULL);
            }
        }
        
        // Short delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "STT capture task ended");
    vTaskDelete(NULL);
}

static void vad_timer_callback(TimerHandle_t xTimer)
{
    // Voice end timeout - confirm voice activity has ended
    if (s_stt_audio.voice_detected) {
        s_stt_audio.voice_detected = false;
        s_stt_audio.silence_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        notify_event(STT_AUDIO_EVENT_VOICE_END, NULL, 0, &s_stt_audio.quality);
    }
}

static float calculate_rms_level(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) return 0.0f;
    
    float sum_squares = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        float sample = samples[i] / 32768.0f;  // Normalize to -1.0 to 1.0
        sum_squares += sample * sample;
    }
    
    return sqrtf(sum_squares / sample_count);
}

static float calculate_peak_level(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) return 0.0f;
    
    int16_t peak = 0;
    for (size_t i = 0; i < sample_count; i++) {
        int16_t abs_sample = abs(samples[i]);
        if (abs_sample > peak) {
            peak = abs_sample;
        }
    }
    
    return peak / 32768.0f;  // Normalize to 0.0 to 1.0
}

static bool detect_voice_activity(float rms_level)
{
    // Store recent level in ring buffer
    s_stt_audio.recent_levels[s_stt_audio.level_index] = rms_level;
    s_stt_audio.level_index = (s_stt_audio.level_index + 1) % 10;
    
    // Calculate average of recent levels for noise floor estimation
    float avg_level = 0.0f;
    for (int i = 0; i < 10; i++) {
        avg_level += s_stt_audio.recent_levels[i];
    }
    avg_level /= 10.0f;
    
    // Voice activity if current level is significantly above average and threshold
    return (rms_level > s_stt_audio.config.vad_threshold) && 
           (rms_level > avg_level * 1.5f);
}

static esp_err_t apply_gain(int16_t *samples, size_t sample_count, float gain)
{
    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        float sample = samples[i] * gain;
        
        // Clamp to prevent overflow
        if (sample > 32767.0f) sample = 32767.0f;
        else if (sample < -32768.0f) sample = -32768.0f;
        
        samples[i] = (int16_t)sample;
    }
    
    return ESP_OK;
}

static esp_err_t apply_noise_suppression(int16_t *samples, size_t sample_count)
{
    // Simple noise gate - suppress very low level signals
    const float noise_gate_threshold = 0.01f;  // -40dB
    
    for (size_t i = 0; i < sample_count; i++) {
        float sample = samples[i] / 32768.0f;
        if (fabsf(sample) < noise_gate_threshold) {
            samples[i] = 0;  // Suppress noise
        }
    }
    
    return ESP_OK;
}

static void notify_event(stt_audio_event_t event, const uint8_t *audio_data, 
                        size_t data_len, const stt_audio_quality_t *quality)
{
    if (s_stt_audio.callback) {
        s_stt_audio.callback(event, audio_data, data_len, quality, s_stt_audio.user_data);
    }
}