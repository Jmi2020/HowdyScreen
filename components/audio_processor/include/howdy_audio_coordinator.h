#pragma once

#include "esp_err.h"
#include "stt_audio_handler.h"
#include "tts_audio_handler.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HowdyTTS Audio Coordinator Configuration
 */
typedef struct {
    // STT Configuration
    stt_audio_config_t stt_config;
    
    // TTS Configuration  
    tts_audio_config_t tts_config;
    
    // Coordination Settings
    bool echo_cancellation;        // Enable echo cancellation during TTS playback
    bool auto_mute_microphone;     // Auto-mute microphone during TTS playback
    uint32_t voice_timeout_ms;     // Timeout for voice activity (0 = no timeout)
    uint32_t silence_timeout_ms;   // Timeout for silence before stopping STT
    bool push_to_talk_mode;        // Enable push-to-talk functionality
    
} howdy_audio_config_t;

/**
 * @brief HowdyTTS Audio Events
 */
typedef enum {
    // STT Events
    HOWDY_AUDIO_EVENT_STT_STARTED,       // STT capture started
    HOWDY_AUDIO_EVENT_STT_STOPPED,       // STT capture stopped  
    HOWDY_AUDIO_EVENT_STT_VOICE_START,   // Voice activity detected
    HOWDY_AUDIO_EVENT_STT_VOICE_END,     // Voice activity ended
    HOWDY_AUDIO_EVENT_STT_CHUNK_READY,   // STT audio chunk ready for transmission
    HOWDY_AUDIO_EVENT_STT_SILENCE,       // Silence detected
    
    // TTS Events
    HOWDY_AUDIO_EVENT_TTS_STARTED,       // TTS playback started
    HOWDY_AUDIO_EVENT_TTS_FINISHED,      // TTS playback finished
    HOWDY_AUDIO_EVENT_TTS_CHUNK_PLAYED,  // TTS audio chunk played
    
    // Coordination Events
    HOWDY_AUDIO_EVENT_MODE_CHANGED,      // Audio mode changed (STT/TTS/IDLE)
    HOWDY_AUDIO_EVENT_ERROR              // Error occurred
    
} howdy_audio_event_t;

/**
 * @brief HowdyTTS Audio Mode
 */
typedef enum {
    HOWDY_AUDIO_MODE_IDLE,        // No audio activity
    HOWDY_AUDIO_MODE_LISTENING,   // STT listening mode
    HOWDY_AUDIO_MODE_SPEAKING,    // TTS speaking mode
    HOWDY_AUDIO_MODE_PROCESSING   // Processing audio (brief transition state)
} howdy_audio_mode_t;

/**
 * @brief HowdyTTS Audio Status
 */
typedef struct {
    howdy_audio_mode_t current_mode;
    bool stt_active;
    bool tts_active;
    bool voice_detected;
    bool microphone_muted;
    
    // Audio quality
    stt_audio_quality_t stt_quality;
    float tts_volume;
    
    // Statistics
    uint32_t stt_chunks_captured;
    uint32_t tts_chunks_played;
    uint32_t voice_sessions;
    uint32_t total_voice_duration_ms;
    
} howdy_audio_status_t;

/**
 * @brief HowdyTTS Audio Event Callback
 * 
 * @param event Event type
 * @param audio_data Audio data (for STT_CHUNK_READY events)
 * @param data_len Length of audio data
 * @param status Current audio status
 * @param user_data User data passed during initialization
 */
typedef void (*howdy_audio_event_callback_t)(howdy_audio_event_t event,
                                            const uint8_t *audio_data,
                                            size_t data_len,
                                            const howdy_audio_status_t *status,
                                            void *user_data);

/**
 * @brief Initialize HowdyTTS audio coordinator
 * 
 * @param config Audio coordinator configuration
 * @param callback Event callback function (required)
 * @param user_data User data for callback (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_init(const howdy_audio_config_t *config,
                          howdy_audio_event_callback_t callback,
                          void *user_data);

/**
 * @brief Deinitialize HowdyTTS audio coordinator
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_deinit(void);

/**
 * @brief Start listening mode (STT)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_start_listening(void);

/**
 * @brief Stop listening mode (STT)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_stop_listening(void);

/**
 * @brief Start speaking mode (TTS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_start_speaking(void);

/**
 * @brief Stop speaking mode (TTS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_stop_speaking(void);

/**
 * @brief Play TTS audio chunk
 * 
 * @param audio_data PCM audio data
 * @param data_len Length of audio data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_play_tts_chunk(const uint8_t *audio_data, size_t data_len);

/**
 * @brief Trigger push-to-talk (for manual voice activation)
 * 
 * @param pressed True when button pressed, false when released
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_push_to_talk(bool pressed);

/**
 * @brief Set TTS volume
 * 
 * @param volume Volume level (0.0 to 1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_set_tts_volume(float volume);

/**
 * @brief Set STT microphone gain
 * 
 * @param gain Gain level (0.5 to 2.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_set_stt_gain(float gain);

/**
 * @brief Set Voice Activity Detection threshold
 * 
 * @param threshold VAD threshold (0.0 to 1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_set_vad_threshold(float threshold);

/**
 * @brief Get current audio status
 * 
 * @param status Pointer to store current status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_get_status(howdy_audio_status_t *status);

/**
 * @brief Get current audio mode
 * 
 * @return howdy_audio_mode_t Current audio mode
 */
howdy_audio_mode_t howdy_audio_get_mode(void);

/**
 * @brief Check if voice is currently detected
 * 
 * @return true if voice is detected, false otherwise
 */
bool howdy_audio_is_voice_detected(void);

/**
 * @brief Check if TTS is currently playing
 * 
 * @return true if TTS is playing, false otherwise
 */
bool howdy_audio_is_tts_playing(void);

/**
 * @brief Mute/unmute microphone
 * 
 * @param muted True to mute, false to unmute
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_audio_set_microphone_mute(bool muted);

/**
 * @brief Default HowdyTTS audio configuration
 */
#define HOWDY_AUDIO_DEFAULT_CONFIG() { \
    .stt_config = STT_AUDIO_DEFAULT_CONFIG(), \
    .tts_config = TTS_AUDIO_DEFAULT_CONFIG(), \
    .echo_cancellation = true, \
    .auto_mute_microphone = true, \
    .voice_timeout_ms = 30000, \
    .silence_timeout_ms = 3000, \
    .push_to_talk_mode = false \
}

#ifdef __cplusplus
}
#endif