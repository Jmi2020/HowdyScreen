#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP32-P4 HowdyScreen Audio Interface Coordinator
 * 
 * The ESP32-P4 HowdyScreen acts as a smart audio interface device:
 * - Microphone: Captures voice audio and streams to HowdyTTS server (Mac) via WebSocket
 * - Speaker: Receives TTS audio from HowdyTTS server via WebSocket and plays through ES8311
 * - Display: Shows visual states (listening, processing, speaking, idle)
 * 
 * NO local STT/TTS processing - all AI processing happens on the Mac server.
 * The device is a "smart microphone + speaker + display" for HowdyTTS.
 */

/**
 * @brief Audio Interface Configuration
 */
typedef struct {
    // Audio capture settings (microphone)
    uint32_t capture_sample_rate;    // Microphone sample rate (16000 Hz recommended)
    uint8_t capture_channels;        // Microphone channels (1 for mono)
    uint8_t capture_bits_per_sample; // Capture bits per sample (16 recommended)
    float microphone_gain;           // Microphone gain (0.5 to 2.0)
    size_t capture_chunk_size;       // Size of audio chunks to capture and send
    
    // Audio playback settings (speaker)
    uint32_t playback_sample_rate;   // Speaker sample rate (16000 Hz recommended)
    uint8_t playback_channels;       // Speaker channels (1 for mono)
    uint8_t playback_bits_per_sample;// Playback bits per sample (16 recommended)
    float speaker_volume;            // Speaker volume (0.0 to 1.0)
    size_t playback_buffer_size;     // Playback buffer size
    
    // Interface behavior
    bool auto_start_listening;       // Start listening automatically after TTS finishes
    uint32_t silence_timeout_ms;     // Stop listening after silence (0 = no timeout)
    bool visual_feedback;            // Enable visual state feedback on display
    
} audio_interface_config_t;

/**
 * @brief Audio Interface States (for display feedback)
 */
typedef enum {
    AUDIO_INTERFACE_STATE_IDLE,        // Idle - not listening or speaking
    AUDIO_INTERFACE_STATE_LISTENING,   // Listening - capturing audio to send to server
    AUDIO_INTERFACE_STATE_PROCESSING,  // Processing - server is processing STT
    AUDIO_INTERFACE_STATE_SPEAKING,    // Speaking - playing TTS audio from server
    AUDIO_INTERFACE_STATE_ERROR        // Error state
} audio_interface_state_t;

/**
 * @brief Audio Interface Events
 */
typedef enum {
    // State change events (for UI feedback)
    AUDIO_INTERFACE_EVENT_STATE_CHANGED,    // Interface state changed
    
    // Audio streaming events
    AUDIO_INTERFACE_EVENT_AUDIO_CAPTURED,   // Audio chunk captured, ready to send
    AUDIO_INTERFACE_EVENT_AUDIO_RECEIVED,   // TTS audio received, playing
    
    // Voice activity events (basic level detection)
    AUDIO_INTERFACE_EVENT_VOICE_DETECTED,   // Voice activity detected
    AUDIO_INTERFACE_EVENT_SILENCE_DETECTED, // Silence detected
    
    // System events
    AUDIO_INTERFACE_EVENT_MICROPHONE_READY, // Microphone initialized and ready
    AUDIO_INTERFACE_EVENT_SPEAKER_READY,    // Speaker initialized and ready
    AUDIO_INTERFACE_EVENT_ERROR             // Error occurred
    
} audio_interface_event_t;

/**
 * @brief Audio Interface Status
 */
typedef struct {
    audio_interface_state_t current_state;
    bool microphone_active;
    bool speaker_active;
    bool voice_detected;
    float current_audio_level;          // Current audio level (0.0 to 1.0)
    
    // Statistics
    uint32_t audio_chunks_sent;         // Audio chunks sent to server
    uint32_t tts_chunks_received;       // TTS chunks received from server
    uint32_t bytes_captured;            // Total bytes captured
    uint32_t bytes_played;              // Total bytes played
    
} audio_interface_status_t;

/**
 * @brief Audio Interface Event Callback
 * 
 * @param event Event type
 * @param audio_data Audio data (for AUDIO_CAPTURED events)
 * @param data_len Length of audio data
 * @param status Current interface status
 * @param user_data User data passed during initialization
 */
typedef void (*audio_interface_event_callback_t)(audio_interface_event_t event,
                                                const uint8_t *audio_data,
                                                size_t data_len,
                                                const audio_interface_status_t *status,
                                                void *user_data);

/**
 * @brief Initialize audio interface coordinator
 * 
 * @param config Audio interface configuration
 * @param callback Event callback function (required for audio streaming)
 * @param user_data User data for callback (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_init(const audio_interface_config_t *config,
                              audio_interface_event_callback_t callback,
                              void *user_data);

/**
 * @brief Deinitialize audio interface coordinator
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_deinit(void);

/**
 * @brief Start listening mode (capture audio to send to server)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_start_listening(void);

/**
 * @brief Stop listening mode
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_stop_listening(void);

/**
 * @brief Play TTS audio chunk received from server
 * 
 * @param audio_data PCM audio data from HowdyTTS server
 * @param data_len Length of audio data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_play_tts_audio(const uint8_t *audio_data, size_t data_len);

/**
 * @brief Set interface state (for display feedback)
 * 
 * @param state New interface state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_set_state(audio_interface_state_t state);

/**
 * @brief Get current interface state
 * 
 * @return audio_interface_state_t Current state
 */
audio_interface_state_t audio_interface_get_state(void);

/**
 * @brief Set speaker volume
 * 
 * @param volume Volume level (0.0 to 1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_set_volume(float volume);

/**
 * @brief Set microphone gain
 * 
 * @param gain Gain level (0.5 to 2.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_set_gain(float gain);

/**
 * @brief Get current interface status
 * 
 * @param status Pointer to store current status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_get_status(audio_interface_status_t *status);

/**
 * @brief Check if interface is currently listening
 * 
 * @return true if listening, false otherwise
 */
bool audio_interface_is_listening(void);

/**
 * @brief Check if interface is currently playing TTS
 * 
 * @return true if playing TTS, false otherwise
 */
bool audio_interface_is_speaking(void);

/**
 * @brief Trigger manual listening start (push-to-talk style)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_interface_trigger_listening(void);

/**
 * @brief Interrupt any ongoing TTS playback immediately.
 *
 * - Stops audio_processor playback
 * - Clears any queued TTS chunks
 * - Leaves microphone/listening state unchanged
 */
esp_err_t audio_interface_interrupt_playback(void);

/**
 * @brief Default audio interface configuration for ESP32-P4 HowdyScreen
 */
#define AUDIO_INTERFACE_DEFAULT_CONFIG() { \
    .capture_sample_rate = 16000, \
    .capture_channels = 1, \
    .capture_bits_per_sample = 16, \
    .microphone_gain = 1.0f, \
    .capture_chunk_size = 1024, \
    .playback_sample_rate = 16000, \
    .playback_channels = 1, \
    .playback_bits_per_sample = 16, \
    .speaker_volume = 0.7f, \
    .playback_buffer_size = 4096, \
    .auto_start_listening = false, \
    .silence_timeout_ms = 5000, \
    .visual_feedback = true \
}

#ifdef __cplusplus
}
#endif
