#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TTS Audio Handler Configuration
 */
typedef struct {
    uint32_t sample_rate;          // TTS audio sample rate (usually 16000 Hz)
    uint8_t channels;              // Number of channels (1 for mono, 2 for stereo)
    uint8_t bits_per_sample;       // Bits per sample (16 recommended)
    float volume;                  // Playback volume (0.0 to 1.0)
    size_t buffer_size;            // Internal buffer size for TTS audio chunks
    uint32_t buffer_timeout_ms;    // Timeout for buffer operations
} tts_audio_config_t;

/**
 * @brief TTS Audio Events
 */
typedef enum {
    TTS_AUDIO_EVENT_STARTED,       // TTS playback started
    TTS_AUDIO_EVENT_FINISHED,      // TTS playback finished
    TTS_AUDIO_EVENT_CHUNK_PLAYED,  // Audio chunk finished playing
    TTS_AUDIO_EVENT_BUFFER_EMPTY,  // Buffer is empty, ready for more data
    TTS_AUDIO_EVENT_ERROR          // Error occurred during playback
} tts_audio_event_t;

/**
 * @brief TTS Audio Event Callback
 * 
 * @param event Event type
 * @param data Event-specific data (can be NULL)
 * @param data_len Length of event data
 * @param user_data User data passed during initialization
 */
typedef void (*tts_audio_event_callback_t)(tts_audio_event_t event, 
                                          const void *data, 
                                          size_t data_len, 
                                          void *user_data);

/**
 * @brief Initialize TTS audio handler
 * 
 * @param config TTS audio configuration
 * @param callback Event callback function (can be NULL)
 * @param user_data User data for callback (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_init(const tts_audio_config_t *config, 
                        tts_audio_event_callback_t callback, 
                        void *user_data);

/**
 * @brief Deinitialize TTS audio handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_deinit(void);

/**
 * @brief Play TTS audio chunk
 * 
 * @param audio_data PCM audio data
 * @param data_len Length of audio data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_play_chunk(const uint8_t *audio_data, size_t data_len);

/**
 * @brief Start TTS playback session
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_start_playback(void);

/**
 * @brief Stop TTS playback session
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_stop_playback(void);

/**
 * @brief Set TTS playback volume
 * 
 * @param volume Volume level (0.0 to 1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_set_volume(float volume);

/**
 * @brief Get current TTS playback volume
 * 
 * @param volume Pointer to store current volume
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_get_volume(float *volume);

/**
 * @brief Check if TTS is currently playing
 * 
 * @return true if TTS is playing, false otherwise
 */
bool tts_audio_is_playing(void);

/**
 * @brief Clear TTS audio buffer
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_clear_buffer(void);

/**
 * @brief Get TTS audio statistics
 * 
 * @param chunks_played Number of audio chunks played
 * @param bytes_played Total bytes played
 * @param buffer_underruns Number of buffer underruns
 * @return esp_err_t ESP_OK on success
 */
esp_err_t tts_audio_get_stats(uint32_t *chunks_played, 
                             uint32_t *bytes_played, 
                             uint32_t *buffer_underruns);

/**
 * @brief Default TTS audio configuration
 */
#define TTS_AUDIO_DEFAULT_CONFIG() { \
    .sample_rate = 16000, \
    .channels = 1, \
    .bits_per_sample = 16, \
    .volume = 0.7f, \
    .buffer_size = 4096, \
    .buffer_timeout_ms = 1000 \
}

#ifdef __cplusplus
}
#endif