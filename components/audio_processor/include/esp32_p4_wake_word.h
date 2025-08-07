#pragma once

#include "esp_err.h"
#include "enhanced_vad.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP32-P4 Wake Word Detection Engine
 * 
 * Lightweight wake word detection optimized for "Hey Howdy" phrase
 * using energy-based detection and pattern matching for ESP32-P4.
 * Integrates with enhanced VAD for speech boundary detection.
 */

// Wake word states
typedef enum {
    WAKE_WORD_STATE_LISTENING = 0,  // Listening for wake word
    WAKE_WORD_STATE_TRIGGERED,      // Wake word detected
    WAKE_WORD_STATE_CONFIRMED,      // Wake word confirmed by server
    WAKE_WORD_STATE_REJECTED        // Wake word rejected by server
} esp32_p4_wake_word_state_t;

// Wake word confidence levels
typedef enum {
    WAKE_WORD_CONFIDENCE_LOW = 0,    // 0-40% confidence
    WAKE_WORD_CONFIDENCE_MEDIUM,     // 41-70% confidence  
    WAKE_WORD_CONFIDENCE_HIGH,       // 71-85% confidence
    WAKE_WORD_CONFIDENCE_VERY_HIGH   // 86-100% confidence
} esp32_p4_wake_word_confidence_t;

/**
 * @brief Wake word detection configuration
 */
typedef struct {
    // Audio parameters
    uint32_t sample_rate;               // Sample rate (16kHz recommended)
    uint16_t frame_size;                // Frame size in samples (320 for 20ms)
    
    // Detection thresholds
    uint16_t energy_threshold;          // Minimum energy threshold (2000-8000)
    float confidence_threshold;         // Minimum confidence (0.6-0.8)
    uint16_t silence_timeout_ms;        // Silence timeout after detection (2000ms)
    
    // Pattern matching
    uint8_t pattern_frames;             // Frames to analyze for pattern (15-25)
    uint8_t consistency_frames;         // Consistency requirement (3-7)
    
    // Adaptive learning
    bool enable_adaptation;             // Enable adaptive threshold adjustment
    float adaptation_rate;              // Adaptation rate (0.01-0.1)
    
    // Conversation-aware configuration
    struct {
        bool enable_context_awareness;      // Enable conversation context awareness
        uint16_t idle_sensitivity_boost;    // Sensitivity boost in idle state (percentage)
        uint16_t speaking_suppression;      // Suppression during TTS (percentage) 
        uint16_t echo_rejection_db;         // Echo rejection threshold in dB
        bool enable_during_conversation;    // Allow wake word during active conversation
    } conversation;
    
    // Performance tuning
    uint16_t processing_interval_ms;    // Processing interval (20-50ms)
    uint8_t max_detections_per_min;     // Rate limiting (5-15 per minute)
} esp32_p4_wake_word_config_t;

/**
 * @brief Wake word detection result
 */
typedef struct {
    // Detection state
    esp32_p4_wake_word_state_t state;
    esp32_p4_wake_word_confidence_t confidence_level;
    float confidence_score;              // Exact confidence (0.0-1.0)
    
    // Timing information
    uint32_t detection_timestamp_ms;     // When wake word was detected
    uint16_t detection_duration_ms;      // Duration of detection
    uint32_t processing_time_us;         // Processing time for this frame
    
    // Pattern analysis
    uint16_t energy_level;               // Current energy level
    uint16_t pattern_match_score;        // Pattern matching score (0-1000)
    uint8_t syllable_count;              // Detected syllable count (should be 3 for "Hey Howdy")
    
    // VAD integration
    bool vad_active;                     // VAD detected voice activity
    bool speech_boundary_detected;       // Speech start/end detected
    
    // Quality metrics
    uint16_t noise_floor;                // Background noise level
    float snr_db;                        // Signal-to-noise ratio
    uint8_t detection_quality;           // Overall quality score (0-255)
    
    // Server feedback integration
    bool server_validated;               // Server confirmed detection
    bool server_rejected;                // Server rejected detection
    uint32_t server_response_time_ms;    // Server response time
    
    // Conversation context
    vad_conversation_context_t conversation_context; // Current conversation context
    bool context_suppressed;             // Detection suppressed due to context
    float echo_suppression_applied;      // Amount of echo suppression applied (0.0-1.0)
} esp32_p4_wake_word_result_t;

/**
 * @brief Wake word detection statistics
 */
typedef struct {
    // Detection counters
    uint32_t total_detections;           // Total wake word detections
    uint32_t true_positives;             // Confirmed by server
    uint32_t false_positives;            // Rejected by server
    uint32_t missed_detections;          // Reported by server but missed locally
    
    // Performance metrics
    float average_confidence;            // Average detection confidence
    uint32_t average_processing_time_us; // Average processing time
    float detection_rate_per_hour;       // Detections per hour
    
    // Adaptation metrics
    uint16_t current_energy_threshold;   // Current adaptive threshold
    uint16_t threshold_adjustments;      // Number of threshold adjustments
    
    // Quality metrics
    float false_positive_rate;           // False positive rate (0.0-1.0)
    uint32_t last_detection_time;        // Last detection timestamp
    uint16_t consecutive_false_positives; // Recent false positives
} esp32_p4_wake_word_stats_t;

/**
 * @brief Wake word detection callback function
 * 
 * Called when wake word is detected or state changes
 */
typedef void (*esp32_p4_wake_word_callback_t)(const esp32_p4_wake_word_result_t *result, void *user_data);

/**
 * @brief Wake word detection handle (opaque)
 */
typedef struct esp32_p4_wake_word_detector* esp32_p4_wake_word_handle_t;

/**
 * @brief Initialize ESP32-P4 wake word detection engine
 * 
 * @param config Detection configuration
 * @return Wake word detector handle, NULL on failure
 */
esp32_p4_wake_word_handle_t esp32_p4_wake_word_init(const esp32_p4_wake_word_config_t *config);

/**
 * @brief Deinitialize wake word detection engine
 * 
 * @param handle Wake word detector handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_deinit(esp32_p4_wake_word_handle_t handle);

/**
 * @brief Process audio frame for wake word detection
 * 
 * @param handle Wake word detector handle
 * @param audio_data Audio samples (16-bit PCM)
 * @param sample_count Number of samples
 * @param vad_result Enhanced VAD result (optional, can be NULL)
 * @param result Output detection result
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_process(esp32_p4_wake_word_handle_t handle,
                                    const int16_t *audio_data,
                                    size_t sample_count,
                                    const enhanced_vad_result_t *vad_result,
                                    esp32_p4_wake_word_result_t *result);

/**
 * @brief Set wake word detection callback
 * 
 * @param handle Wake word detector handle
 * @param callback Callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_set_callback(esp32_p4_wake_word_handle_t handle,
                                         esp32_p4_wake_word_callback_t callback,
                                         void *user_data);

/**
 * @brief Update wake word detection thresholds
 * 
 * @param handle Wake word detector handle
 * @param energy_threshold New energy threshold
 * @param confidence_threshold New confidence threshold
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_update_thresholds(esp32_p4_wake_word_handle_t handle,
                                              uint16_t energy_threshold,
                                              float confidence_threshold);

/**
 * @brief Handle server validation feedback
 * 
 * @param handle Wake word detector handle
 * @param detection_id Detection ID to validate
 * @param validated True if server confirmed, false if rejected
 * @param response_time_ms Server response time
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_server_feedback(esp32_p4_wake_word_handle_t handle,
                                            uint32_t detection_id,
                                            bool validated,
                                            uint32_t response_time_ms);

/**
 * @brief Get wake word detection statistics
 * 
 * @param handle Wake word detector handle
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_get_stats(esp32_p4_wake_word_handle_t handle,
                                      esp32_p4_wake_word_stats_t *stats);

/**
 * @brief Reset wake word detection statistics
 * 
 * @param handle Wake word detector handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_reset_stats(esp32_p4_wake_word_handle_t handle);

/**
 * @brief Get default wake word configuration
 * 
 * @param config Output configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_get_default_config(esp32_p4_wake_word_config_t *config);

/**
 * @brief Check if wake word detection is active
 * 
 * @param handle Wake word detector handle
 * @return true if actively listening for wake word
 */
bool esp32_p4_wake_word_is_listening(esp32_p4_wake_word_handle_t handle);

/**
 * @brief Enable/disable wake word detection
 * 
 * @param handle Wake word detector handle
 * @param enable True to enable, false to disable
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_enable(esp32_p4_wake_word_handle_t handle, bool enable);

/**
 * @brief Trigger adaptive threshold adjustment
 * 
 * Called when false positives/negatives are detected to improve accuracy
 * 
 * @param handle Wake word detector handle
 * @param adjustment_type 0=reduce sensitivity, 1=increase sensitivity
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_adapt_threshold(esp32_p4_wake_word_handle_t handle,
                                           uint8_t adjustment_type);

/**
 * @brief Get current detection state
 * 
 * @param handle Wake word detector handle
 * @return Current wake word state
 */
esp32_p4_wake_word_state_t esp32_p4_wake_word_get_state(esp32_p4_wake_word_handle_t handle);

/**
 * @brief Set conversation context for adaptive wake word behavior
 * 
 * Adjusts wake word detection sensitivity based on conversation state:
 * - IDLE: Enhanced sensitivity for wake word detection
 * - LISTENING: Normal sensitivity during active conversation
 * - SPEAKING: Reduced sensitivity with echo suppression during TTS
 * - PROCESSING: Maintain current behavior
 * 
 * @param handle Wake word detector handle
 * @param context Current conversation context
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_set_conversation_context(esp32_p4_wake_word_handle_t handle,
                                                    vad_conversation_context_t context);

/**
 * @brief Get current conversation context
 * 
 * @param handle Wake word detector handle
 * @return Current conversation context
 */
vad_conversation_context_t esp32_p4_wake_word_get_conversation_context(esp32_p4_wake_word_handle_t handle);

/**
 * @brief Notify wake word detector of TTS audio level for echo suppression
 * 
 * During TTS playback, this helps the wake word detector distinguish between
 * actual user speech and echo from the speaker output
 * 
 * @param handle Wake word detector handle
 * @param tts_level Current TTS audio level (0.0 = silent, 1.0 = maximum)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_set_tts_level(esp32_p4_wake_word_handle_t handle, 
                                          float tts_level);

/**
 * @brief Get conversation-aware default configuration
 * 
 * Returns configuration optimized for conversation flow with appropriate
 * context-aware settings and echo suppression
 * 
 * @param config Output configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t esp32_p4_wake_word_get_conversation_config(esp32_p4_wake_word_config_t *config);

#ifdef __cplusplus
}
#endif