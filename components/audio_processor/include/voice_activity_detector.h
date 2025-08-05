#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Voice Activity Detection (VAD) configuration
 */
typedef struct {
    uint16_t amplitude_threshold;    // Minimum amplitude for voice detection
    uint16_t silence_threshold_ms;   // Silence duration before stopping recording
    uint16_t min_voice_duration_ms;  // Minimum voice duration to trigger
    uint16_t sample_rate;           // Audio sample rate
} vad_config_t;

/**
 * @brief VAD detection result
 */
typedef struct {
    bool voice_detected;            // True if voice is currently detected
    bool speech_started;            // True if speech just started
    bool speech_ended;              // True if speech just ended
    uint16_t max_amplitude;         // Maximum amplitude in current frame
    uint32_t voice_duration_ms;     // Duration of current voice activity
    uint32_t silence_duration_ms;   // Duration of current silence
} vad_result_t;

/**
 * @brief VAD instance handle
 */
typedef struct vad_instance* vad_handle_t;

/**
 * @brief Initialize Voice Activity Detector
 * 
 * @param config VAD configuration
 * @return vad_handle_t VAD handle or NULL on failure
 */
vad_handle_t vad_init(const vad_config_t *config);

/**
 * @brief Deinitialize Voice Activity Detector
 * 
 * @param handle VAD handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_deinit(vad_handle_t handle);

/**
 * @brief Process audio buffer for voice activity detection
 * 
 * Inspired by Arduino detectSound function but adapted for continuous monitoring
 * 
 * @param handle VAD handle
 * @param buffer Audio buffer (int16_t samples)
 * @param samples Number of samples in buffer
 * @param result Output VAD result
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_process_audio(vad_handle_t handle, const int16_t *buffer, size_t samples, vad_result_t *result);

/**
 * @brief Reset VAD state (use when changing listening mode)
 * 
 * @param handle VAD handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_reset(vad_handle_t handle);

/**
 * @brief Update VAD configuration
 * 
 * @param handle VAD handle
 * @param config New configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_update_config(vad_handle_t handle, const vad_config_t *config);

/**
 * @brief Get current VAD statistics
 * 
 * @param handle VAD handle
 * @param total_voice_time Total time voice was detected (ms)
 * @param detection_count Number of voice detection events
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_get_stats(vad_handle_t handle, uint32_t *total_voice_time, uint32_t *detection_count);

#ifdef __cplusplus
}
#endif