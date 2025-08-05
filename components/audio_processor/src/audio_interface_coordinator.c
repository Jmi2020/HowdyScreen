#include "audio_interface_coordinator.h"
#include "audio_processor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <string.h>
#include <math.h>

static const char *TAG = "AudioInterface";

/**
 * ESP32-P4 HowdyScreen Audio Interface Coordinator
 * 
 * Purpose: Smart microphone + speaker + display interface for HowdyTTS
 * - Captures voice audio → streams to Mac server via WebSocket
 * - Receives TTS audio ← from Mac server via WebSocket → plays via ES8311
 * - Provides visual state feedback on 800x800 display
 * 
 * NO local AI processing - pure audio interface device
 */

// Audio Interface state
static struct {
    bool initialized;
    audio_interface_config_t config;
    audio_interface_event_callback_t callback;
    void *user_data;
    
    // Current state
    audio_interface_state_t current_state;
    bool microphone_active;
    bool speaker_active;
    bool voice_detected;
    
    // Audio processing tasks
    TaskHandle_t capture_task_handle;
    TaskHandle_t playback_task_handle;
    SemaphoreHandle_t state_mutex;
    TimerHandle_t silence_timer;
    
    // Audio streaming queues
    QueueHandle_t tts_audio_queue;
    
    // Audio analysis
    float current_audio_level;
    float recent_levels[5];        // Ring buffer for voice detection
    uint8_t level_index;
    
    // Statistics
    uint32_t audio_chunks_sent;
    uint32_t tts_chunks_received;
    uint32_t bytes_captured;
    uint32_t bytes_played;
    
} s_audio_interface = {0};

// TTS audio chunk for playback queue
typedef struct {
    uint8_t *data;
    size_t length;
} tts_audio_chunk_t;

// Forward declarations
static void audio_capture_task(void *pvParameters);
static void tts_playback_task(void *pvParameters);
static void silence_timer_callback(TimerHandle_t xTimer);
static float calculate_audio_level(const int16_t *samples, size_t sample_count);
static bool detect_voice_activity(float audio_level);
static esp_err_t change_state(audio_interface_state_t new_state);
static void notify_event(audio_interface_event_t event, const uint8_t *audio_data, size_t data_len);
static const char* state_to_string(audio_interface_state_t state);

esp_err_t audio_interface_init(const audio_interface_config_t *config,
                              audio_interface_event_callback_t callback,
                              void *user_data)
{
    if (s_audio_interface.initialized) {
        ESP_LOGI(TAG, "Audio interface already initialized");
        return ESP_OK;
    }
    
    if (!config || !callback) {
        ESP_LOGE(TAG, "Invalid configuration or callback");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing ESP32-P4 HowdyScreen Audio Interface");
    ESP_LOGI(TAG, "Architecture: Smart microphone + speaker + display for HowdyTTS");
    ESP_LOGI(TAG, "Processing: All STT/TTS done on Mac server, ESP32-P4 is audio passthrough");
    ESP_LOGI(TAG, "Capture: %lu Hz, %d ch, %d bit, gain %.2f", 
             config->capture_sample_rate, config->capture_channels, 
             config->capture_bits_per_sample, config->microphone_gain);
    ESP_LOGI(TAG, "Playback: %lu Hz, %d ch, %d bit, vol %.2f", 
             config->playback_sample_rate, config->playback_channels, 
             config->playback_bits_per_sample, config->speaker_volume);
    
    // Copy configuration
    s_audio_interface.config = *config;
    s_audio_interface.callback = callback;
    s_audio_interface.user_data = user_data;
    
    // Create mutex for state protection
    s_audio_interface.state_mutex = xSemaphoreCreateMutex();
    if (!s_audio_interface.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Create TTS audio playback queue
    s_audio_interface.tts_audio_queue = xQueueCreate(20, sizeof(tts_audio_chunk_t));
    if (!s_audio_interface.tts_audio_queue) {
        ESP_LOGE(TAG, "Failed to create TTS audio queue");
        vSemaphoreDelete(s_audio_interface.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create silence timer for listening timeout
    if (config->silence_timeout_ms > 0) {
        s_audio_interface.silence_timer = xTimerCreate("silence_timer", 
                                                      pdMS_TO_TICKS(config->silence_timeout_ms),
                                                      pdFALSE, NULL, silence_timer_callback);
        if (!s_audio_interface.silence_timer) {
            ESP_LOGE(TAG, "Failed to create silence timer");
            vQueueDelete(s_audio_interface.tts_audio_queue);
            vSemaphoreDelete(s_audio_interface.state_mutex);
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Create audio capture task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        audio_capture_task,
        "audio_capture",
        8192,  // Large stack for audio processing
        NULL,
        6,     // High priority for real-time audio
        &s_audio_interface.capture_task_handle,
        1      // Core 1 (separate from network tasks)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio capture task");
        if (s_audio_interface.silence_timer) xTimerDelete(s_audio_interface.silence_timer, 0);
        vQueueDelete(s_audio_interface.tts_audio_queue);
        vSemaphoreDelete(s_audio_interface.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Create TTS playback task
    task_ret = xTaskCreatePinnedToCore(
        tts_playback_task,
        "tts_playback",
        8192,  // Large stack for audio processing
        NULL,
        5,     // Medium priority
        &s_audio_interface.playback_task_handle,
        1      // Core 1 (separate from network tasks)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS playback task");
        vTaskDelete(s_audio_interface.capture_task_handle);
        if (s_audio_interface.silence_timer) xTimerDelete(s_audio_interface.silence_timer, 0);
        vQueueDelete(s_audio_interface.tts_audio_queue);
        vSemaphoreDelete(s_audio_interface.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize state and statistics
    s_audio_interface.current_state = AUDIO_INTERFACE_STATE_IDLE;
    s_audio_interface.microphone_active = false;
    s_audio_interface.speaker_active = false;
    s_audio_interface.voice_detected = false;
    s_audio_interface.current_audio_level = 0.0f;
    s_audio_interface.level_index = 0;
    
    s_audio_interface.audio_chunks_sent = 0;
    s_audio_interface.tts_chunks_received = 0;
    s_audio_interface.bytes_captured = 0;
    s_audio_interface.bytes_played = 0;
    
    memset(s_audio_interface.recent_levels, 0, sizeof(s_audio_interface.recent_levels));
    
    s_audio_interface.initialized = true;
    
    ESP_LOGI(TAG, "ESP32-P4 HowdyScreen Audio Interface initialized successfully");
    ESP_LOGI(TAG, "Ready to stream audio to/from HowdyTTS server");
    
    // Notify microphone and speaker ready
    notify_event(AUDIO_INTERFACE_EVENT_MICROPHONE_READY, NULL, 0);
    notify_event(AUDIO_INTERFACE_EVENT_SPEAKER_READY, NULL, 0);
    
    return ESP_OK;
}

esp_err_t audio_interface_deinit(void)
{
    if (!s_audio_interface.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing audio interface");
    
    // Stop any active operations
    audio_interface_stop_listening();
    
    // Delete tasks
    if (s_audio_interface.capture_task_handle) {
        vTaskDelete(s_audio_interface.capture_task_handle);
        s_audio_interface.capture_task_handle = NULL;
    }
    
    if (s_audio_interface.playback_task_handle) {
        vTaskDelete(s_audio_interface.playback_task_handle);
        s_audio_interface.playback_task_handle = NULL;
    }
    
    // Clean up resources
    if (s_audio_interface.silence_timer) {
        xTimerDelete(s_audio_interface.silence_timer, portMAX_DELAY);
        s_audio_interface.silence_timer = NULL;
    }
    
    if (s_audio_interface.tts_audio_queue) {
        // Clear any remaining TTS chunks
        tts_audio_chunk_t chunk;
        while (xQueueReceive(s_audio_interface.tts_audio_queue, &chunk, 0) == pdTRUE) {
            free(chunk.data);
        }
        vQueueDelete(s_audio_interface.tts_audio_queue);
        s_audio_interface.tts_audio_queue = NULL;
    }
    
    if (s_audio_interface.state_mutex) {
        vSemaphoreDelete(s_audio_interface.state_mutex);
        s_audio_interface.state_mutex = NULL;
    }
    
    s_audio_interface.initialized = false;
    ESP_LOGI(TAG, "Audio interface deinitialized");
    
    return ESP_OK;
}

esp_err_t audio_interface_start_listening(void)
{
    if (!s_audio_interface.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting listening mode - will capture and stream audio to server");
    
    // Start audio processor capture
    esp_err_t ret = audio_processor_start_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio capture: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_audio_interface.microphone_active = true;
    change_state(AUDIO_INTERFACE_STATE_LISTENING);
    
    // Start silence timer if configured
    if (s_audio_interface.silence_timer) {
        xTimerReset(s_audio_interface.silence_timer, 0);
    }
    
    return ESP_OK;
}

esp_err_t audio_interface_stop_listening(void)
{
    if (!s_audio_interface.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_audio_interface.microphone_active) {
        ESP_LOGI(TAG, "Stopping listening mode");
        
        s_audio_interface.microphone_active = false;
        s_audio_interface.voice_detected = false;
        
        // Stop silence timer
        if (s_audio_interface.silence_timer) {
            xTimerStop(s_audio_interface.silence_timer, 0);
        }
        
        // Stop audio processor capture
        audio_processor_stop_capture();
        
        change_state(AUDIO_INTERFACE_STATE_IDLE);
    }
    
    return ESP_OK;
}

esp_err_t audio_interface_play_tts_audio(const uint8_t *audio_data, size_t data_len)
{
    if (!s_audio_interface.initialized || !audio_data || data_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "Received TTS audio chunk from server: %zu bytes", data_len);
    
    // Allocate memory for TTS chunk
    uint8_t *chunk_data = malloc(data_len);
    if (!chunk_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for TTS chunk (%zu bytes)", data_len);
        return ESP_ERR_NO_MEM;
    }
    
    // Copy TTS audio data
    memcpy(chunk_data, audio_data, data_len);
    
    // Apply volume scaling
    int16_t *samples = (int16_t *)chunk_data;
    size_t sample_count = data_len / sizeof(int16_t);
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] = (int16_t)(samples[i] * s_audio_interface.config.speaker_volume);
    }
    
    // Create TTS chunk
    tts_audio_chunk_t chunk = {
        .data = chunk_data,
        .length = data_len
    };
    
    // Queue TTS chunk for playback
    if (xQueueSend(s_audio_interface.tts_audio_queue, &chunk, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "TTS playback queue full, dropping chunk");
        free(chunk_data);
        return ESP_ERR_TIMEOUT;
    }
    
    // Start playback if not already active
    if (!s_audio_interface.speaker_active) {
        esp_err_t ret = audio_processor_start_playback();
        if (ret == ESP_OK) {
            s_audio_interface.speaker_active = true;
            change_state(AUDIO_INTERFACE_STATE_SPEAKING);
        }
    }
    
    s_audio_interface.tts_chunks_received++;
    notify_event(AUDIO_INTERFACE_EVENT_AUDIO_RECEIVED, audio_data, data_len);
    
    return ESP_OK;
}

esp_err_t audio_interface_set_state(audio_interface_state_t state)
{
    return change_state(state);
}

audio_interface_state_t audio_interface_get_state(void)
{
    return s_audio_interface.current_state;
}

esp_err_t audio_interface_set_volume(float volume)
{
    if (!s_audio_interface.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (volume < 0.0f || volume > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_audio_interface.config.speaker_volume = volume;
    ESP_LOGI(TAG, "Speaker volume set to %.2f", volume);
    
    return ESP_OK;
}

esp_err_t audio_interface_set_gain(float gain)
{
    if (!s_audio_interface.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gain < 0.5f || gain > 2.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_audio_interface.config.microphone_gain = gain;
    ESP_LOGI(TAG, "Microphone gain set to %.2f", gain);
    
    return ESP_OK;
}

esp_err_t audio_interface_get_status(audio_interface_status_t *status)
{
    if (!s_audio_interface.initialized || !status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->current_state = s_audio_interface.current_state;
    status->microphone_active = s_audio_interface.microphone_active;
    status->speaker_active = s_audio_interface.speaker_active;
    status->voice_detected = s_audio_interface.voice_detected;
    status->current_audio_level = s_audio_interface.current_audio_level;
    status->audio_chunks_sent = s_audio_interface.audio_chunks_sent;
    status->tts_chunks_received = s_audio_interface.tts_chunks_received;
    status->bytes_captured = s_audio_interface.bytes_captured;
    status->bytes_played = s_audio_interface.bytes_played;
    
    return ESP_OK;
}

bool audio_interface_is_listening(void)
{
    return s_audio_interface.initialized && s_audio_interface.microphone_active;
}

bool audio_interface_is_speaking(void)
{
    return s_audio_interface.initialized && s_audio_interface.speaker_active;
}

esp_err_t audio_interface_trigger_listening(void)
{
    ESP_LOGI(TAG, "Manual listening trigger activated");
    return audio_interface_start_listening();
}

static void audio_capture_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio capture task started - streaming to HowdyTTS server");
    
    uint8_t *audio_buffer = NULL;
    size_t buffer_length = 0;
    
    while (1) {
        if (s_audio_interface.microphone_active) {
            // Get audio buffer from processor
            esp_err_t ret = audio_processor_get_buffer(&audio_buffer, &buffer_length);
            if (ret == ESP_OK && audio_buffer && buffer_length > 0) {
                
                // Apply microphone gain
                int16_t *samples = (int16_t *)audio_buffer;
                size_t sample_count = buffer_length / sizeof(int16_t);
                for (size_t i = 0; i < sample_count; i++) {
                    float sample = samples[i] * s_audio_interface.config.microphone_gain;
                    if (sample > 32767.0f) sample = 32767.0f;
                    else if (sample < -32768.0f) sample = -32768.0f;
                    samples[i] = (int16_t)sample;
                }
                
                // Calculate audio level for voice detection
                float audio_level = calculate_audio_level(samples, sample_count);
                s_audio_interface.current_audio_level = audio_level;
                
                // Simple voice activity detection
                bool voice_active = detect_voice_activity(audio_level);
                if (voice_active != s_audio_interface.voice_detected) {
                    s_audio_interface.voice_detected = voice_active;
                    if (voice_active) {
                        notify_event(AUDIO_INTERFACE_EVENT_VOICE_DETECTED, NULL, 0);
                        // Reset silence timer when voice detected
                        if (s_audio_interface.silence_timer) {
                            xTimerReset(s_audio_interface.silence_timer, 0);
                        }
                    } else {
                        notify_event(AUDIO_INTERFACE_EVENT_SILENCE_DETECTED, NULL, 0);
                    }
                }
                
                // Send audio chunk to server (via callback)
                s_audio_interface.audio_chunks_sent++;
                s_audio_interface.bytes_captured += buffer_length;
                
                notify_event(AUDIO_INTERFACE_EVENT_AUDIO_CAPTURED, audio_buffer, buffer_length);
                
                // Release buffer
                audio_processor_release_buffer();
                
                ESP_LOGD(TAG, "Captured audio chunk: %zu bytes, level: %.3f, voice: %s", 
                        buffer_length, audio_level, voice_active ? "YES" : "NO");
                
            } else if (ret != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Failed to get audio buffer: %s", esp_err_to_name(ret));
                notify_event(AUDIO_INTERFACE_EVENT_ERROR, (const uint8_t*)&ret, sizeof(ret));
            }
        }
        
        // Short delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void tts_playback_task(void *pvParameters)
{
    ESP_LOGI(TAG, "TTS playback task started - playing audio from HowdyTTS server");
    
    tts_audio_chunk_t chunk;
    
    while (1) {
        // Wait for TTS audio chunks to play
        if (xQueueReceive(s_audio_interface.tts_audio_queue, &chunk, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            if (s_audio_interface.speaker_active) {
                // Write TTS audio data to processor for playback
                esp_err_t ret = audio_processor_write_data(chunk.data, chunk.length);
                if (ret == ESP_OK) {
                    s_audio_interface.bytes_played += chunk.length;
                    ESP_LOGD(TAG, "Played TTS chunk: %zu bytes", chunk.length);
                } else {
                    ESP_LOGE(TAG, "Failed to write TTS audio data: %s", esp_err_to_name(ret));
                    notify_event(AUDIO_INTERFACE_EVENT_ERROR, (const uint8_t*)&ret, sizeof(ret));
                }
            }
            
            // Free chunk memory
            free(chunk.data);
            
        } else {
            // No TTS data for a while, check if we should stop playback
            if (s_audio_interface.speaker_active && uxQueueMessagesWaiting(s_audio_interface.tts_audio_queue) == 0) {
                // Queue is empty, stop playback
                audio_processor_stop_playback();
                s_audio_interface.speaker_active = false;
                
                // Auto-start listening if configured
                if (s_audio_interface.config.auto_start_listening) {
                    ESP_LOGI(TAG, "TTS finished, auto-starting listening mode");
                    audio_interface_start_listening();
                } else {
                    change_state(AUDIO_INTERFACE_STATE_IDLE);
                }
            }
        }
    }
}

static void silence_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Silence timeout - stopping listening mode");
    audio_interface_stop_listening();
}

static float calculate_audio_level(const int16_t *samples, size_t sample_count)
{
    if (!samples || sample_count == 0) return 0.0f;
    
    float sum_squares = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        float sample = samples[i] / 32768.0f;  // Normalize to -1.0 to 1.0
        sum_squares += sample * sample;
    }
    
    return sqrtf(sum_squares / sample_count);
}

static bool detect_voice_activity(float audio_level)
{
    // Store recent level in ring buffer
    s_audio_interface.recent_levels[s_audio_interface.level_index] = audio_level;
    s_audio_interface.level_index = (s_audio_interface.level_index + 1) % 5;
    
    // Calculate average of recent levels for noise floor
    float avg_level = 0.0f;
    for (int i = 0; i < 5; i++) {
        avg_level += s_audio_interface.recent_levels[i];
    }
    avg_level /= 5.0f;
    
    // Simple voice activity detection
    const float vad_threshold = 0.02f;  // Adjust based on testing
    return (audio_level > vad_threshold) && (audio_level > avg_level * 2.0f);
}

static esp_err_t change_state(audio_interface_state_t new_state)
{
    if (xSemaphoreTake(s_audio_interface.state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (new_state != s_audio_interface.current_state) {
        audio_interface_state_t old_state = s_audio_interface.current_state;
        s_audio_interface.current_state = new_state;
        
        ESP_LOGI(TAG, "State changed: %s → %s", 
                state_to_string(old_state), state_to_string(new_state));
        
        notify_event(AUDIO_INTERFACE_EVENT_STATE_CHANGED, NULL, 0);
    }
    
    xSemaphoreGive(s_audio_interface.state_mutex);
    return ESP_OK;
}

static void notify_event(audio_interface_event_t event, const uint8_t *audio_data, size_t data_len)
{
    if (s_audio_interface.callback) {
        audio_interface_status_t status;
        audio_interface_get_status(&status);
        s_audio_interface.callback(event, audio_data, data_len, &status, s_audio_interface.user_data);
    }
}

static const char* state_to_string(audio_interface_state_t state)
{
    switch (state) {
        case AUDIO_INTERFACE_STATE_IDLE: return "IDLE";
        case AUDIO_INTERFACE_STATE_LISTENING: return "LISTENING";
        case AUDIO_INTERFACE_STATE_PROCESSING: return "PROCESSING";
        case AUDIO_INTERFACE_STATE_SPEAKING: return "SPEAKING";
        case AUDIO_INTERFACE_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}