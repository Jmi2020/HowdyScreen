#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HowdyTTS protocol message types
typedef enum {
    HOWDYTTS_MSG_AUDIO_STREAM = 0,
    HOWDYTTS_MSG_TTS_RESPONSE,
    HOWDYTTS_MSG_START_SESSION,
    HOWDYTTS_MSG_END_SESSION,
    HOWDYTTS_MSG_VOICE_DETECTED,
    HOWDYTTS_MSG_VOICE_ENDED,
    HOWDYTTS_MSG_ERROR,
    HOWDYTTS_MSG_PING,
    HOWDYTTS_MSG_PONG
} howdytts_message_type_t;

// HowdyTTS audio configuration
typedef struct {
    uint32_t sample_rate;    // 16000 Hz
    uint8_t channels;        // 1 (mono)
    uint8_t bits_per_sample; // 16
    bool use_compression;    // Enable Opus compression
} howdytts_audio_config_t;

// HowdyTTS session configuration
typedef struct {
    char session_id[64];
    char device_id[32];
    howdytts_audio_config_t audio_config;
    uint32_t keepalive_interval_ms;
} howdytts_session_config_t;

/**
 * @brief Initialize HowdyTTS protocol
 * 
 * @param session_config Session configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_protocol_init(const howdytts_session_config_t *session_config);

/**
 * @brief Create audio stream message
 * 
 * @param audio_data Audio buffer (16-bit PCM)
 * @param samples Number of samples
 * @param message_buffer Output buffer for JSON message
 * @param buffer_size Size of output buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_create_audio_message(const int16_t *audio_data, size_t samples, 
                                       char *message_buffer, size_t buffer_size);

/**
 * @brief Create session start message
 * 
 * @param message_buffer Output buffer for JSON message
 * @param buffer_size Size of output buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_create_session_start_message(char *message_buffer, size_t buffer_size);

/**
 * @brief Create voice detection message
 * 
 * @param voice_detected Voice detection state
 * @param confidence Voice detection confidence (0.0-1.0)
 * @param message_buffer Output buffer for JSON message
 * @param buffer_size Size of output buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_create_voice_message(bool voice_detected, float confidence,
                                       char *message_buffer, size_t buffer_size);

/**
 * @brief Parse incoming TTS response message
 * 
 * @param json_message Incoming JSON message
 * @param audio_data Output buffer for decoded audio
 * @param max_samples Maximum number of samples in output buffer
 * @param samples_decoded Number of samples decoded
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_parse_tts_response(const char *json_message, int16_t *audio_data, 
                                     size_t max_samples, size_t *samples_decoded);

/**
 * @brief Create ping message for keepalive
 * 
 * @param message_buffer Output buffer for JSON message
 * @param buffer_size Size of output buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_create_ping_message(char *message_buffer, size_t buffer_size);

/**
 * @brief Batch multiple audio frames into single message for efficiency
 * 
 * @param audio_frames Array of audio frame pointers
 * @param frame_sizes Array of frame sizes
 * @param num_frames Number of frames to batch
 * @param message_buffer Output buffer for JSON message
 * @param buffer_size Size of output buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_batch_audio_frames(const int16_t **audio_frames, const size_t *frame_sizes,
                                     size_t num_frames, char *message_buffer, size_t buffer_size);

/**
 * @brief Get current session ID
 * 
 * @return const char* Session ID string
 */
const char* howdytts_get_session_id(void);

/**
 * @brief Get protocol statistics
 * 
 * @param messages_sent Number of messages sent
 * @param audio_frames_sent Number of audio frames sent
 * @param bytes_compressed Bytes saved through compression
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_get_stats(uint32_t *messages_sent, uint32_t *audio_frames_sent, uint32_t *bytes_compressed);

#ifdef __cplusplus
}
#endif