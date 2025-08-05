#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief STT Audio Handler Configuration
 */
typedef struct {
    uint32_t sample_rate;          // STT audio sample rate (usually 16000 Hz)
    uint8_t channels;              // Number of channels (1 for mono, 2 for stereo) 
    uint8_t bits_per_sample;       // Bits per sample (16 recommended)
    float gain;                    // Microphone gain (0.5 to 2.0)
    size_t chunk_size;             // Size of audio chunks to capture
    uint32_t capture_timeout_ms;   // Timeout for capture operations
    bool noise_suppression;        // Enable basic noise suppression
    float vad_threshold;           // Voice Activity Detection threshold (0.0 to 1.0)
} stt_audio_config_t;

/**
 * @brief STT Audio Events
 */
typedef enum {
    STT_AUDIO_EVENT_STARTED,       // STT capture started
    STT_AUDIO_EVENT_STOPPED,       // STT capture stopped
    STT_AUDIO_EVENT_CHUNK_READY,   // Audio chunk ready for STT processing
    STT_AUDIO_EVENT_VOICE_START,   // Voice activity detected (start of speech)
    STT_AUDIO_EVENT_VOICE_END,     // Voice activity ended (end of speech)
    STT_AUDIO_EVENT_SILENCE,       // Silence detected
    STT_AUDIO_EVENT_ERROR          // Error occurred during capture
} stt_audio_event_t;

/**
 * @brief STT Audio Quality Metrics
 */
typedef struct {
    float rms_level;               // RMS audio level (0.0 to 1.0)
    float peak_level;              // Peak audio level (0.0 to 1.0)
    float snr_estimate;            // Signal-to-noise ratio estimate (dB)
    bool voice_detected;           // Voice activity detected
    uint32_t silence_duration_ms;  // Duration of current silence
    uint32_t voice_duration_ms;    // Duration of current voice activity
} stt_audio_quality_t;

/**
 * @brief STT Audio Event Callback
 * 
 * @param event Event type
 * @param audio_data Audio data (for CHUNK_READY events)
 * @param data_len Length of audio data
 * @param quality Audio quality metrics (can be NULL)
 * @param user_data User data passed during initialization
 */
typedef void (*stt_audio_event_callback_t)(stt_audio_event_t event,
                                          const uint8_t *audio_data,
                                          size_t data_len,
                                          const stt_audio_quality_t *quality,
                                          void *user_data);

/**
 * @brief Initialize STT audio handler
 * 
 * @param config STT audio configuration
 * @param callback Event callback function (required)
 * @param user_data User data for callback (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_init(const stt_audio_config_t *config,
                        stt_audio_event_callback_t callback,
                        void *user_data);

/**
 * @brief Deinitialize STT audio handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_deinit(void);

/**
 * @brief Start STT audio capture
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_start_capture(void);

/**
 * @brief Stop STT audio capture
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_stop_capture(void);

/**
 * @brief Set STT microphone gain
 * 
 * @param gain Gain level (0.5 to 2.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_set_gain(float gain);

/**
 * @brief Get current STT microphone gain
 * 
 * @param gain Pointer to store current gain
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_get_gain(float *gain);

/**
 * @brief Set Voice Activity Detection threshold
 * 
 * @param threshold VAD threshold (0.0 to 1.0, higher = less sensitive)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_set_vad_threshold(float threshold);

/**
 * @brief Check if STT is currently capturing
 * 
 * @return true if STT is capturing, false otherwise
 */
bool stt_audio_is_capturing(void);

/**
 * @brief Check if voice activity is currently detected
 * 
 * @return true if voice is detected, false otherwise
 */
bool stt_audio_is_voice_detected(void);

/**
 * @brief Get current audio quality metrics
 * 
 * @param quality Pointer to store quality metrics
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_get_quality(stt_audio_quality_t *quality);

/**
 * @brief Get STT audio statistics
 * 
 * @param chunks_captured Number of audio chunks captured
 * @param bytes_captured Total bytes captured
 * @param voice_segments Number of voice segments detected
 * @param avg_level Average audio level
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_get_stats(uint32_t *chunks_captured,
                             uint32_t *bytes_captured,
                             uint32_t *voice_segments,
                             float *avg_level);

/**
 * @brief Trigger manual voice activity detection
 * Useful for push-to-talk functionality
 * 
 * @param force_voice_start Force voice detection start
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stt_audio_trigger_voice_detection(bool force_voice_start);

/**
 * @brief Default STT audio configuration
 */
#define STT_AUDIO_DEFAULT_CONFIG() { \
    .sample_rate = 16000, \
    .channels = 1, \
    .bits_per_sample = 16, \
    .gain = 1.0f, \
    .chunk_size = 1024, \
    .capture_timeout_ms = 100, \
    .noise_suppression = true, \
    .vad_threshold = 0.3f \
}

#ifdef __cplusplus
}
#endif