#include "continuous_audio_processor.h"
#include "audio_memory_buffer.h"
#include "voice_activity_detector.h"
#include "dual_i2s_manager.h"
#include "websocket_client.h"
#include "udp_audio_streamer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ContinuousAudio";

// Continuous audio processor instance
struct continuous_audio_processor {
    continuous_audio_config_t config;
    audio_state_callback_t state_callback;
    
    // Processing state
    audio_processing_mode_t current_mode;
    audio_processing_mode_t previous_mode;
    
    // Audio components
    vad_handle_t vad_handle;
    audio_memory_buffer_t audio_buffer;
    audio_memory_buffer_t recording_buffer;  // For complete utterances
    
    // Wake word simulation state
    uint64_t wake_start_time;
    bool wake_candidate;
    uint32_t consecutive_wake_frames;
    
    // Recording state
    uint64_t recording_start_time;
    bool is_recording;
    uint32_t recorded_samples;
    
    // Streaming state
    uint64_t last_stream_time;
    bool streaming_enabled;
    
    // Statistics
    uint32_t frames_processed;
    uint32_t wake_events;
    uint32_t recording_sessions;
    uint64_t total_processing_time;
    
    // Thread safety
    SemaphoreHandle_t state_mutex;
    bool is_running;
};

// Forward declarations
static esp_err_t set_processing_mode(continuous_audio_handle_t handle, audio_processing_mode_t new_mode);
static esp_err_t process_wake_detection(continuous_audio_handle_t handle, const vad_result_t *vad_result);
static esp_err_t process_recording_mode(continuous_audio_handle_t handle, const int16_t *audio_buffer, size_t samples);
static esp_err_t handle_streaming(continuous_audio_handle_t handle, const int16_t *audio_buffer, size_t samples);

continuous_audio_handle_t continuous_audio_init(const continuous_audio_config_t *config, 
                                               audio_state_callback_t state_callback)
{
    if (!config || !state_callback) {
        ESP_LOGE(TAG, "Invalid parameters");
        return NULL;
    }
    
    struct continuous_audio_processor *handle = heap_caps_malloc(sizeof(struct continuous_audio_processor), 
                                                                MALLOC_CAP_DEFAULT);
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate continuous audio processor");
        return NULL;
    }
    
    memset(handle, 0, sizeof(struct continuous_audio_processor));
    
    // Copy configuration
    handle->config = *config;
    handle->state_callback = state_callback;
    handle->current_mode = AUDIO_MODE_WAITING;
    
    // Create state mutex
    handle->state_mutex = xSemaphoreCreateMutex();
    if (!handle->state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        heap_caps_free(handle);
        return NULL;
    }
    
    // Initialize VAD
    handle->vad_handle = vad_init(&config->vad_config);
    if (!handle->vad_handle) {
        ESP_LOGE(TAG, "Failed to initialize VAD");
        vSemaphoreDelete(handle->state_mutex);
        heap_caps_free(handle);
        return NULL;
    }
    
    // Initialize audio buffers
    if (audio_memory_buffer_init(&handle->audio_buffer, config->buffer_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio buffer");
        vad_deinit(handle->vad_handle);
        vSemaphoreDelete(handle->state_mutex);
        heap_caps_free(handle);
        return NULL;
    }
    
    // Initialize recording buffer (larger for complete utterances)
    size_t recording_buffer_size = (config->max_recording_duration_ms * config->sample_rate) / 1000;
    if (audio_memory_buffer_init(&handle->recording_buffer, recording_buffer_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize recording buffer");
        audio_memory_buffer_deinit(&handle->audio_buffer);
        vad_deinit(handle->vad_handle);
        vSemaphoreDelete(handle->state_mutex);
        heap_caps_free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Continuous audio processor initialized - mode: WAITING");
    ESP_LOGI(TAG, "Wake threshold: %d, Recording buffer: %zu samples", 
             config->wake_threshold, recording_buffer_size);
    
    return handle;
}

esp_err_t continuous_audio_deinit(continuous_audio_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop processing
    continuous_audio_stop(handle);
    
    // Cleanup components
    if (handle->vad_handle) {
        vad_deinit(handle->vad_handle);
    }
    
    audio_memory_buffer_deinit(&handle->audio_buffer);
    audio_memory_buffer_deinit(&handle->recording_buffer);
    
    if (handle->state_mutex) {
        vSemaphoreDelete(handle->state_mutex);
    }
    
    ESP_LOGI(TAG, "Continuous audio processor deinitialized - processed %lu frames, %lu wake events",
             handle->frames_processed, handle->wake_events);
    
    heap_caps_free(handle);
    return ESP_OK;
}

esp_err_t continuous_audio_start(continuous_audio_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handle->state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    handle->is_running = true;
    handle->streaming_enabled = handle->config.enable_streaming;
    set_processing_mode(handle, AUDIO_MODE_WAITING);
    
    // Reset statistics
    handle->frames_processed = 0;
    handle->wake_events = 0;
    handle->recording_sessions = 0;
    handle->total_processing_time = 0;
    
    xSemaphoreGive(handle->state_mutex);
    
    ESP_LOGI(TAG, "Continuous audio processing started");
    return ESP_OK;
}

esp_err_t continuous_audio_stop(continuous_audio_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handle->state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    handle->is_running = false;
    handle->is_recording = false;
    handle->streaming_enabled = false;
    
    // Clear buffers
    audio_memory_buffer_clear(&handle->audio_buffer);
    audio_memory_buffer_clear(&handle->recording_buffer);
    
    // Reset VAD
    vad_reset(handle->vad_handle);
    
    xSemaphoreGive(handle->state_mutex);
    
    ESP_LOGI(TAG, "Continuous audio processing stopped");
    return ESP_OK;
}

esp_err_t continuous_audio_process_frame(continuous_audio_handle_t handle, 
                                       const int16_t *audio_buffer, 
                                       size_t samples)
{
    if (!handle || !audio_buffer || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!handle->is_running) {
        return ESP_OK;  // Silently ignore if not running
    }
    
    uint64_t start_time = esp_timer_get_time();
    
    // Process with VAD
    vad_result_t vad_result;
    esp_err_t ret = vad_process_audio(handle->vad_handle, audio_buffer, samples, &vad_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "VAD processing failed");
        return ret;
    }
    
    // Add to audio buffer for potential streaming
    audio_memory_buffer_write(&handle->audio_buffer, audio_buffer, samples);
    
    // Process based on current mode
    switch (handle->current_mode) {
        case AUDIO_MODE_WAITING:
            process_wake_detection(handle, &vad_result);
            break;
            
        case AUDIO_MODE_LISTENING:
        case AUDIO_MODE_RECORDING:
            process_recording_mode(handle, audio_buffer, samples);
            break;
            
        case AUDIO_MODE_PROCESSING:
            // Server is processing - minimal activity
            break;
            
        case AUDIO_MODE_SPEAKING:
            // TTS is playing - ignore microphone input
            break;
            
        case AUDIO_MODE_ENDING:
            // Conversation ending - prepare to return to waiting
            set_processing_mode(handle, AUDIO_MODE_WAITING);
            break;
    }
    
    // Handle streaming if enabled
    if (handle->streaming_enabled && 
        (handle->current_mode == AUDIO_MODE_RECORDING || handle->current_mode == AUDIO_MODE_LISTENING)) {
        handle_streaming(handle, audio_buffer, samples);
    }
    
    // Update statistics
    handle->frames_processed++;
    handle->total_processing_time += (esp_timer_get_time() - start_time);
    
    ESP_LOGV(TAG, "Processed frame: %zu samples, mode: %d, voice: %s", 
             samples, handle->current_mode, vad_result.voice_detected ? "YES" : "NO");
    
    return ESP_OK;
}

static esp_err_t process_wake_detection(continuous_audio_handle_t handle, const vad_result_t *vad_result)
{
    uint64_t current_time = esp_timer_get_time();
    
    // Simple wake word simulation based on sustained voice activity
    if (vad_result->voice_detected && vad_result->max_amplitude > handle->config.wake_threshold) {
        if (!handle->wake_candidate) {
            handle->wake_candidate = true;
            handle->wake_start_time = current_time;
            handle->consecutive_wake_frames = 1;
            ESP_LOGD(TAG, "Wake candidate detected - amplitude: %d", vad_result->max_amplitude);
        } else {
            handle->consecutive_wake_frames++;
            uint32_t wake_duration = (current_time - handle->wake_start_time) / 1000;
            
            if (wake_duration >= handle->config.wake_duration_ms) {
                // Wake word detected!
                ESP_LOGI(TAG, "Wake word detected after %lu ms, %lu frames", 
                         wake_duration, handle->consecutive_wake_frames);
                
                handle->wake_events++;
                handle->wake_candidate = false;
                
                // Transition to listening mode
                set_processing_mode(handle, AUDIO_MODE_LISTENING);
                
                return ESP_OK;
            }
        }
    } else {
        // Reset wake candidate if voice stops or amplitude drops
        if (handle->wake_candidate) {
            uint32_t wake_duration = (current_time - handle->wake_start_time) / 1000;
            ESP_LOGD(TAG, "Wake candidate reset after %lu ms", wake_duration);
            handle->wake_candidate = false;
            handle->consecutive_wake_frames = 0;
        }
    }
    
    return ESP_OK;
}

static esp_err_t process_recording_mode(continuous_audio_handle_t handle, const int16_t *audio_buffer, size_t samples)
{
    if (!handle->is_recording) {
        // Start recording
        handle->is_recording = true;
        handle->recording_start_time = esp_timer_get_time();
        handle->recorded_samples = 0;
        handle->recording_sessions++;
        
        audio_memory_buffer_clear(&handle->recording_buffer);
        
        ESP_LOGI(TAG, "Started recording session #%lu", handle->recording_sessions);
        set_processing_mode(handle, AUDIO_MODE_RECORDING);
    }
    
    // Add audio to recording buffer
    audio_memory_buffer_write(&handle->recording_buffer, audio_buffer, samples);
    handle->recorded_samples += samples;
    
    // Check for recording timeout
    uint32_t recording_duration = (esp_timer_get_time() - handle->recording_start_time) / 1000;
    if (recording_duration >= handle->config.max_recording_duration_ms) {
        ESP_LOGI(TAG, "Recording timeout reached: %lu ms", recording_duration);
        handle->is_recording = false;
        set_processing_mode(handle, AUDIO_MODE_PROCESSING);
    }
    
    return ESP_OK;
}

static esp_err_t handle_streaming(continuous_audio_handle_t handle, const int16_t *audio_buffer, size_t samples)
{
    uint64_t current_time = esp_timer_get_time();
    uint32_t time_since_last_stream = (current_time - handle->last_stream_time) / 1000;
    
    if (time_since_last_stream >= handle->config.stream_interval_ms) {
        // Send audio to server via UDP and/or WebSocket
        bool stream_sent = false;
        
        // Try UDP streaming first (lower latency)
        if (udp_audio_is_streaming()) {
            esp_err_t ret = udp_audio_send(audio_buffer, samples);
            if (ret == ESP_OK) {
                handle->last_stream_time = current_time;
                stream_sent = true;
                ESP_LOGD(TAG, "Streamed %zu samples via UDP", samples);
            }
        }
        
        // Also send via WebSocket if available (for reliability/control)
        if (ws_client_is_audio_ready()) {
            esp_err_t ret = ws_client_send_binary_audio(audio_buffer, samples);
            if (ret == ESP_OK) {
                handle->last_stream_time = current_time;
                stream_sent = true;
                ESP_LOGD(TAG, "Streamed %zu samples via WebSocket", samples);
            }
        }
        
        if (!stream_sent) {
            ESP_LOGW(TAG, "Failed to stream audio - no transport available");
        }
    }
    
    return ESP_OK;
}

static esp_err_t set_processing_mode(continuous_audio_handle_t handle, audio_processing_mode_t new_mode)
{
    if (handle->current_mode == new_mode) {
        return ESP_OK;  // No change needed
    }
    
    audio_processing_mode_t old_mode = handle->current_mode;
    handle->previous_mode = old_mode;
    handle->current_mode = new_mode;
    
    // Mode-specific actions
    switch (new_mode) {
        case AUDIO_MODE_WAITING:
            handle->is_recording = false;
            handle->wake_candidate = false;
            vad_reset(handle->vad_handle);
            ESP_LOGI(TAG, "Mode: WAITING (listening for wake word)");
            break;
            
        case AUDIO_MODE_LISTENING:
            ESP_LOGI(TAG, "Mode: LISTENING (ready for speech)");
            break;
            
        case AUDIO_MODE_RECORDING:
            ESP_LOGI(TAG, "Mode: RECORDING (capturing speech)");
            break;
            
        case AUDIO_MODE_PROCESSING:
            handle->is_recording = false;
            ESP_LOGI(TAG, "Mode: PROCESSING (server thinking)");
            break;
            
        case AUDIO_MODE_SPEAKING:
            handle->is_recording = false;
            ESP_LOGI(TAG, "Mode: SPEAKING (TTS playback)");
            break;
            
        case AUDIO_MODE_ENDING:
            ESP_LOGI(TAG, "Mode: ENDING (conversation ending)");
            break;
    }
    
    // Notify application of mode change
    if (handle->state_callback) {
        vad_result_t dummy_vad = {0};  // Create empty VAD result for callback
        handle->state_callback(old_mode, new_mode, &dummy_vad);
    }
    
    return ESP_OK;
}

esp_err_t continuous_audio_set_mode(continuous_audio_handle_t handle, audio_processing_mode_t mode)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handle->state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = set_processing_mode(handle, mode);
    
    xSemaphoreGive(handle->state_mutex);
    return ret;
}

audio_processing_mode_t continuous_audio_get_mode(continuous_audio_handle_t handle)
{
    if (!handle) {
        return AUDIO_MODE_WAITING;
    }
    
    return handle->current_mode;
}

esp_err_t continuous_audio_handle_tts_audio(continuous_audio_handle_t handle, 
                                          const uint8_t *audio_data, 
                                          size_t length)
{
    if (!handle || !audio_data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Set to speaking mode
    set_processing_mode(handle, AUDIO_MODE_SPEAKING);
    
    // TODO: Integrate with audio output pipeline for TTS playback
    // For now, just log the received TTS audio
    ESP_LOGI(TAG, "Received TTS audio: %zu bytes", length);
    
    return ESP_OK;
}

esp_err_t continuous_audio_get_stats(continuous_audio_handle_t handle,
                                   uint32_t *frames_processed,
                                   uint32_t *wake_events,
                                   uint32_t *recording_sessions)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (frames_processed) {
        *frames_processed = handle->frames_processed;
    }
    
    if (wake_events) {
        *wake_events = handle->wake_events;
    }
    
    if (recording_sessions) {
        *recording_sessions = handle->recording_sessions;
    }
    
    return ESP_OK;
}