#pragma once

#include "esp_err.h"
#include "voice_activity_detector.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enhanced Voice Activity Detection (VAD) for ESP32-P4
 * 
 * Multi-layer VAD implementation optimized for HowdyTTS integration:
 * - Layer 1: Enhanced energy-based detection with adaptive noise floor
 * - Layer 2: Spectral analysis using ESP32-P4 DSP capabilities  
 * - Layer 3: Multi-frame consistency checking
 * 
 * Designed for <50ms latency and minimal memory overhead
 */

// Enhanced VAD feature flags
#define ENHANCED_VAD_ENABLE_ADAPTIVE_THRESHOLD  (1 << 0)
#define ENHANCED_VAD_ENABLE_SPECTRAL_ANALYSIS   (1 << 1)
#define ENHANCED_VAD_ENABLE_CONSISTENCY_CHECK   (1 << 2)
#define ENHANCED_VAD_ENABLE_SNR_ANALYSIS        (1 << 3)

/**
 * @brief Enhanced VAD configuration
 */
typedef struct {
    // Basic energy-based detection (Layer 1 - Enhanced)
    uint16_t amplitude_threshold;        // Base amplitude threshold
    uint16_t silence_threshold_ms;       // Silence duration before stopping
    uint16_t min_voice_duration_ms;      // Minimum voice duration to trigger
    uint16_t sample_rate;               // Audio sample rate
    
    // Adaptive threshold configuration
    float noise_floor_alpha;            // Noise floor adaptation rate (0.01-0.1)
    float snr_threshold_db;             // Signal-to-noise ratio threshold (6-12 dB)
    uint16_t adaptation_window_ms;       // Adaptation window size (100-1000ms)
    
    // Spectral analysis configuration (Layer 2)
    uint16_t zcr_threshold_min;         // Zero-crossing rate min (5 crossings/frame)
    uint16_t zcr_threshold_max;         // Zero-crossing rate max (300 crossings/frame)
    float low_freq_ratio_threshold;     // Low frequency energy ratio (0.3-0.7)
    float spectral_rolloff_threshold;   // Spectral rolloff threshold (0.85)
    
    // Consistency checking (Layer 3)  
    uint8_t consistency_frames;         // Frames for consistency check (3-7)
    float confidence_threshold;         // Overall confidence threshold (0.5-0.8)
    
    // Feature enable flags
    uint32_t feature_flags;             // Combination of ENHANCED_VAD_ENABLE_* flags
    
    // Performance tuning
    uint8_t processing_mode;            // 0=full, 1=optimized, 2=minimal
} enhanced_vad_config_t;

/**
 * @brief Enhanced VAD detection result
 */
typedef struct {
    // Basic detection results (compatible with existing vad_result_t)
    bool voice_detected;                // True if voice is currently detected
    bool speech_started;                // True if speech just started
    bool speech_ended;                  // True if speech just ended
    uint16_t max_amplitude;             // Maximum amplitude in current frame
    uint32_t voice_duration_ms;         // Duration of current voice activity
    uint32_t silence_duration_ms;       // Duration of current silence
    
    // Enhanced detection results
    float confidence;                   // Overall detection confidence (0.0-1.0)
    float snr_db;                      // Current signal-to-noise ratio
    uint16_t noise_floor;              // Current adaptive noise floor
    
    // Spectral analysis results
    uint16_t zero_crossing_rate;        // Zero crossings per frame
    float low_freq_energy_ratio;       // Low frequency energy ratio (300-1000Hz)
    float spectral_rolloff;            // Spectral rolloff point
    
    // Quality metrics
    uint8_t detection_quality;         // Quality score (0-255)
    bool high_confidence;              // True if high confidence detection
    uint32_t frames_processed;         // Total frames processed this session
} enhanced_vad_result_t;

/**
 * @brief Enhanced VAD statistics
 */
typedef struct {
    // Detection statistics
    uint32_t total_voice_time_ms;      // Total voice time detected
    uint32_t detection_count;          // Number of voice detections
    uint32_t false_positive_count;     // Estimated false positives
    
    // Performance statistics  
    uint32_t average_processing_time_us; // Average processing time per frame
    uint32_t max_processing_time_us;   // Maximum processing time
    float average_confidence;          // Average detection confidence
    
    // Adaptation statistics
    uint16_t current_noise_floor;      // Current noise floor level
    uint16_t min_noise_floor;          // Minimum observed noise floor
    uint16_t max_noise_floor;          // Maximum observed noise floor
    uint32_t adaptations_count;        // Number of threshold adaptations
} enhanced_vad_stats_t;

/**
 * @brief Enhanced VAD instance handle
 */
typedef struct enhanced_vad_instance* enhanced_vad_handle_t;

/**
 * @brief Initialize Enhanced Voice Activity Detector
 * 
 * @param config Enhanced VAD configuration
 * @return enhanced_vad_handle_t VAD handle or NULL on failure
 */
enhanced_vad_handle_t enhanced_vad_init(const enhanced_vad_config_t *config);

/**
 * @brief Deinitialize Enhanced Voice Activity Detector
 * 
 * @param handle Enhanced VAD handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_deinit(enhanced_vad_handle_t handle);

/**
 * @brief Process audio buffer for enhanced voice activity detection
 * 
 * Multi-layer processing:
 * 1. Enhanced energy detection with adaptive noise floor
 * 2. Spectral analysis for speech characteristics
 * 3. Multi-frame consistency checking
 * 
 * @param handle Enhanced VAD handle
 * @param buffer Audio buffer (int16_t samples)
 * @param samples Number of samples in buffer
 * @param result Output enhanced VAD result
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_process_audio(enhanced_vad_handle_t handle, 
                                   const int16_t *buffer, 
                                   size_t samples, 
                                   enhanced_vad_result_t *result);

/**
 * @brief Reset enhanced VAD state
 * 
 * @param handle Enhanced VAD handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_reset(enhanced_vad_handle_t handle);

/**
 * @brief Update enhanced VAD configuration
 * 
 * @param handle Enhanced VAD handle
 * @param config New configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_update_config(enhanced_vad_handle_t handle, const enhanced_vad_config_t *config);

/**
 * @brief Get enhanced VAD statistics
 * 
 * @param handle Enhanced VAD handle
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_get_stats(enhanced_vad_handle_t handle, enhanced_vad_stats_t *stats);

/**
 * @brief Get default enhanced VAD configuration optimized for ESP32-P4
 * 
 * @param sample_rate Target sample rate (typically 16000)
 * @param config Output configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_get_default_config(uint16_t sample_rate, enhanced_vad_config_t *config);

/**
 * @brief Set enhanced VAD processing mode for power optimization
 * 
 * @param handle Enhanced VAD handle
 * @param mode Processing mode (0=full, 1=optimized, 2=minimal)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_set_processing_mode(enhanced_vad_handle_t handle, uint8_t mode);

/**
 * @brief Convert enhanced VAD result to basic VAD result for compatibility
 * 
 * @param enhanced_result Enhanced VAD result
 * @param basic_result Output basic VAD result
 * @return esp_err_t ESP_OK on success
 */
esp_err_t enhanced_vad_to_basic_result(const enhanced_vad_result_t *enhanced_result, 
                                     vad_result_t* basic_result);

#ifdef __cplusplus
}
#endif