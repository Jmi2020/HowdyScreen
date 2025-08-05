#pragma once

#include "esp_err.h"
#include "voice_activity_detector.h"
#include "audio_memory_buffer.h"
#include "websocket_client.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Continuous audio processing mode (aligns with HowdyTTS states)
 */
typedef enum {
    AUDIO_MODE_WAITING = 0,     // Waiting for wake word (passive listening)
    AUDIO_MODE_LISTENING,       // Active listening after wake word
    AUDIO_MODE_RECORDING,       // Recording user speech
    AUDIO_MODE_PROCESSING,      // Server is processing
    AUDIO_MODE_SPEAKING,        // TTS playback
    AUDIO_MODE_ENDING          // Conversation ending
} audio_processing_mode_t;

/**
 * @brief Audio processing configuration
 */
typedef struct {
    uint32_t sample_rate;           // Audio sample rate (16000 Hz)
    uint16_t frame_size;            // Processing frame size in samples
    uint16_t buffer_size;           // Audio buffer size in samples
    
    // VAD configuration
    vad_config_t vad_config;
    
    // Wake word simulation (until real wake word detection)
    uint16_t wake_threshold;        // Amplitude threshold for wake simulation
    uint32_t wake_duration_ms;      // Duration needed to trigger wake
    
    // Recording parameters
    uint32_t max_recording_duration_ms;  // Maximum recording time
    uint16_t silence_timeout_ms;         // Silence before stopping recording
    
    // WebSocket integration
    bool enable_streaming;          // Enable real-time streaming to server
    uint32_t stream_interval_ms;    // Streaming interval
} continuous_audio_config_t;

/**
 * @brief Audio processing state callback
 * 
 * Called when audio processing state changes
 */
typedef void (*audio_state_callback_t)(audio_processing_mode_t old_mode, 
                                      audio_processing_mode_t new_mode, 
                                      const vad_result_t *vad_result);

/**
 * @brief Continuous audio processor handle
 */
typedef struct continuous_audio_processor* continuous_audio_handle_t;

/**
 * @brief Initialize continuous audio processor
 * 
 * This replaces button-triggered recording with continuous wake word listening
 * 
 * @param config Processing configuration
 * @param state_callback State change callback
 * @return continuous_audio_handle_t Handle or NULL on failure
 */
continuous_audio_handle_t continuous_audio_init(const continuous_audio_config_t *config, 
                                               audio_state_callback_t state_callback);

/**
 * @brief Deinitialize continuous audio processor
 * 
 * @param handle Processor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_deinit(continuous_audio_handle_t handle);

/**
 * @brief Start continuous audio processing
 * 
 * @param handle Processor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_start(continuous_audio_handle_t handle);

/**
 * @brief Stop continuous audio processing
 * 
 * @param handle Processor handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_stop(continuous_audio_handle_t handle);

/**
 * @brief Process audio frame (called from I2S task)
 * 
 * Replaces the Arduino micTask function with continuous processing
 * 
 * @param handle Processor handle
 * @param audio_buffer Audio samples
 * @param samples Number of samples
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_process_frame(continuous_audio_handle_t handle, 
                                       const int16_t *audio_buffer, 
                                       size_t samples);

/**
 * @brief Set processing mode (external control from HowdyTTS states)
 * 
 * @param handle Processor handle
 * @param mode New processing mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_set_mode(continuous_audio_handle_t handle, audio_processing_mode_t mode);

/**
 * @brief Get current processing mode
 * 
 * @param handle Processor handle
 * @return audio_processing_mode_t Current mode
 */
audio_processing_mode_t continuous_audio_get_mode(continuous_audio_handle_t handle);

/**
 * @brief Handle TTS audio playback
 * 
 * @param handle Processor handle
 * @param audio_data TTS audio data
 * @param length Audio data length in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_handle_tts_audio(continuous_audio_handle_t handle, 
                                          const uint8_t *audio_data, 
                                          size_t length);

/**
 * @brief Get processing statistics
 * 
 * @param handle Processor handle
 * @param frames_processed Number of frames processed
 * @param wake_events Number of wake events detected
 * @param recording_sessions Number of recording sessions
 * @return esp_err_t ESP_OK on success
 */
esp_err_t continuous_audio_get_stats(continuous_audio_handle_t handle,
                                   uint32_t *frames_processed,
                                   uint32_t *wake_events,
                                   uint32_t *recording_sessions);

#ifdef __cplusplus
}
#endif