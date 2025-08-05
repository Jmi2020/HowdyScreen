#include "voice_activity_detector.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "VAD";

// VAD instance structure
struct vad_instance {
    vad_config_t config;
    
    // State tracking
    bool current_voice_state;
    bool previous_voice_state;
    uint64_t voice_start_time;
    uint64_t silence_start_time;
    uint64_t last_process_time;
    
    // Statistics
    uint32_t total_voice_time_ms;
    uint32_t detection_count;
    
    // Signal processing
    uint16_t recent_max_amplitude;
    uint32_t samples_processed;
};

vad_handle_t vad_init(const vad_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid VAD configuration");
        return NULL;
    }
    
    struct vad_instance *vad = heap_caps_malloc(sizeof(struct vad_instance), MALLOC_CAP_DEFAULT);
    if (!vad) {
        ESP_LOGE(TAG, "Failed to allocate VAD instance");
        return NULL;
    }
    
    // Initialize VAD instance
    memset(vad, 0, sizeof(struct vad_instance));
    vad->config = *config;
    vad->last_process_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "VAD initialized - threshold: %d, sample_rate: %d Hz", 
             config->amplitude_threshold, config->sample_rate);
    
    return vad;
}

esp_err_t vad_deinit(vad_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "VAD deinitialized - total detections: %lu", handle->detection_count);
    heap_caps_free(handle);
    return ESP_OK;
}

esp_err_t vad_process_audio(vad_handle_t handle, const int16_t *buffer, size_t samples, vad_result_t *result)
{
    if (!handle || !buffer || !result || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t current_time = esp_timer_get_time();
    uint32_t frame_duration_ms = (samples * 1000) / handle->config.sample_rate;
    
    // Initialize result
    memset(result, 0, sizeof(vad_result_t));
    
    // Calculate maximum amplitude in buffer (inspired by Arduino detectSound)
    uint16_t max_amplitude = 0;
    for (size_t i = 0; i < samples; i++) {
        uint16_t amplitude = abs(buffer[i]);
        if (amplitude > max_amplitude) {
            max_amplitude = amplitude;
        }
    }
    
    // Voice detection based on amplitude threshold
    bool voice_detected = (max_amplitude > handle->config.amplitude_threshold);
    
    // State transition logic
    handle->previous_voice_state = handle->current_voice_state;
    handle->current_voice_state = voice_detected;
    
    // Detect speech start
    if (voice_detected && !handle->previous_voice_state) {
        handle->voice_start_time = current_time;
        handle->silence_start_time = 0;
        result->speech_started = true;
        handle->detection_count++;
        ESP_LOGD(TAG, "Speech started - amplitude: %d", max_amplitude);
    }
    
    // Detect speech end
    if (!voice_detected && handle->previous_voice_state) {
        handle->silence_start_time = current_time;
        
        // Calculate voice duration
        if (handle->voice_start_time > 0) {
            uint32_t voice_duration = (current_time - handle->voice_start_time) / 1000;
            handle->total_voice_time_ms += voice_duration;
            result->voice_duration_ms = voice_duration;
            ESP_LOGD(TAG, "Speech ended - duration: %lu ms", voice_duration);
        }
    }
    
    // Check for definitive speech end (after silence threshold)
    if (handle->silence_start_time > 0) {
        uint32_t silence_duration = (current_time - handle->silence_start_time) / 1000;
        result->silence_duration_ms = silence_duration;
        
        if (silence_duration >= handle->config.silence_threshold_ms) {
            result->speech_ended = true;
            ESP_LOGD(TAG, "Speech definitively ended after %lu ms silence", silence_duration);
        }
    }
    
    // Update result
    result->voice_detected = voice_detected;
    result->max_amplitude = max_amplitude;
    
    // Update running voice duration if currently speaking
    if (voice_detected && handle->voice_start_time > 0) {
        result->voice_duration_ms = (current_time - handle->voice_start_time) / 1000;
    }
    
    // Update statistics
    handle->recent_max_amplitude = max_amplitude;
    handle->samples_processed += samples;
    handle->last_process_time = current_time;
    
    ESP_LOGV(TAG, "VAD processed %zu samples - voice: %s, amp: %d", 
             samples, voice_detected ? "YES" : "NO", max_amplitude);
    
    return ESP_OK;
}

esp_err_t vad_reset(vad_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->current_voice_state = false;
    handle->previous_voice_state = false;
    handle->voice_start_time = 0;
    handle->silence_start_time = 0;
    handle->last_process_time = esp_timer_get_time();
    handle->recent_max_amplitude = 0;
    
    ESP_LOGI(TAG, "VAD state reset");
    return ESP_OK;
}

esp_err_t vad_update_config(vad_handle_t handle, const vad_config_t *config)
{
    if (!handle || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->config = *config;
    ESP_LOGI(TAG, "VAD configuration updated - threshold: %d", config->amplitude_threshold);
    return ESP_OK;
}

esp_err_t vad_get_stats(vad_handle_t handle, uint32_t *total_voice_time, uint32_t *detection_count)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (total_voice_time) {
        *total_voice_time = handle->total_voice_time_ms;
    }
    
    if (detection_count) {
        *detection_count = handle->detection_count;
    }
    
    return ESP_OK;
}