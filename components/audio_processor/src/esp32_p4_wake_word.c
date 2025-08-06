#include "esp32_p4_wake_word.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "ESP32P4_WakeWord";

// Wake word pattern for "Hey Howdy" (simplified energy pattern)
// Pattern represents relative energy levels across frames
static const float HEY_HOWDY_PATTERN[] = {
    0.3, 0.7, 0.4,  // "Hey" - medium start, high peak, medium end
    0.2, 0.1,       // Brief pause
    0.4, 0.8, 0.5,  // "How" - medium start, high peak, medium end  
    0.6, 0.3, 0.1   // "dy" - medium-high start, fade out
};

#define HEY_HOWDY_PATTERN_LENGTH (sizeof(HEY_HOWDY_PATTERN) / sizeof(float))
#define MIN_SYLLABLE_COUNT 2
#define MAX_SYLLABLE_COUNT 4
#define EXPECTED_SYLLABLE_COUNT 3  // "Hey-How-dy"

// Detection constants
#define MIN_DETECTION_FRAMES 10
#define MAX_DETECTION_FRAMES 30
#define ENERGY_HISTORY_SIZE 50
#define PATTERN_MATCH_THRESHOLD 0.65f
#define SYLLABLE_DETECTION_THRESHOLD 0.4f

/**
 * @brief Internal wake word detector structure
 */
typedef struct esp32_p4_wake_word_detector {
    // Configuration
    esp32_p4_wake_word_config_t config;
    
    // State
    esp32_p4_wake_word_state_t state;
    bool enabled;
    uint32_t detection_id_counter;
    
    // Audio processing
    float energy_history[ENERGY_HISTORY_SIZE];
    uint8_t energy_history_index;
    uint16_t current_energy;
    uint16_t adaptive_threshold;
    
    // Pattern matching
    float pattern_buffer[MAX_DETECTION_FRAMES];
    uint8_t pattern_buffer_index;
    uint8_t detection_frame_count;
    bool potential_wake_word;
    
    // Timing
    uint64_t last_detection_time;
    uint64_t last_process_time;
    uint64_t silence_start_time;
    
    // Statistics
    esp32_p4_wake_word_stats_t stats;
    
    // Callback
    esp32_p4_wake_word_callback_t callback;
    void *callback_user_data;
    
    // Thread safety
    SemaphoreHandle_t mutex;
} esp32_p4_wake_word_detector_t;

// Forward declarations
static uint16_t calculate_audio_energy(const int16_t *audio_data, size_t sample_count);
static float calculate_pattern_match(const float *pattern_buffer, uint8_t length);
static uint8_t detect_syllables(const float *energy_pattern, uint8_t length);
static void update_adaptive_threshold(esp32_p4_wake_word_detector_t *detector, uint16_t energy);
static esp32_p4_wake_word_confidence_t calculate_confidence_level(float confidence_score);

esp32_p4_wake_word_handle_t esp32_p4_wake_word_init(const esp32_p4_wake_word_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return NULL;
    }
    
    esp32_p4_wake_word_detector_t *detector = malloc(sizeof(esp32_p4_wake_word_detector_t));
    if (!detector) {
        ESP_LOGE(TAG, "Failed to allocate memory for detector");
        return NULL;
    }
    
    memset(detector, 0, sizeof(esp32_p4_wake_word_detector_t));
    
    // Copy configuration
    detector->config = *config;
    detector->state = WAKE_WORD_STATE_LISTENING;
    detector->enabled = true;
    detector->adaptive_threshold = config->energy_threshold;
    
    // Initialize mutex
    detector->mutex = xSemaphoreCreateMutex();
    if (!detector->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(detector);
        return NULL;
    }
    
    // Initialize statistics
    detector->stats.current_energy_threshold = config->energy_threshold;
    detector->stats.last_detection_time = 0;
    
    ESP_LOGI(TAG, "ESP32-P4 Wake Word Detection initialized");
    ESP_LOGI(TAG, "Target phrase: 'Hey Howdy'");
    ESP_LOGI(TAG, "Energy threshold: %d", config->energy_threshold);
    ESP_LOGI(TAG, "Confidence threshold: %.2f", config->confidence_threshold);
    ESP_LOGI(TAG, "Sample rate: %lu Hz", config->sample_rate);
    
    return (esp32_p4_wake_word_handle_t)detector;
}

esp_err_t esp32_p4_wake_word_deinit(esp32_p4_wake_word_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (detector->mutex) {
        vSemaphoreDelete(detector->mutex);
    }
    
    free(detector);
    
    ESP_LOGI(TAG, "ESP32-P4 Wake Word Detection deinitialized");
    return ESP_OK;
}

esp_err_t esp32_p4_wake_word_process(esp32_p4_wake_word_handle_t handle,
                                    const int16_t *audio_data,
                                    size_t sample_count,
                                    const enhanced_vad_result_t *vad_result,
                                    esp32_p4_wake_word_result_t *result)
{
    if (!handle || !audio_data || !result || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (!detector->enabled) {
        memset(result, 0, sizeof(esp32_p4_wake_word_result_t));
        result->state = WAKE_WORD_STATE_LISTENING;
        return ESP_OK;
    }
    
    if (xSemaphoreTake(detector->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint64_t start_time = esp_timer_get_time();
    
    // Initialize result
    memset(result, 0, sizeof(esp32_p4_wake_word_result_t));
    result->state = detector->state;
    result->detection_timestamp_ms = start_time / 1000;
    
    // Calculate audio energy
    uint16_t energy = calculate_audio_energy(audio_data, sample_count);
    detector->current_energy = energy;
    result->energy_level = energy;
    
    // Update energy history
    detector->energy_history[detector->energy_history_index] = (float)energy;
    detector->energy_history_index = (detector->energy_history_index + 1) % ENERGY_HISTORY_SIZE;
    
    // Update adaptive threshold if enabled
    if (detector->config.enable_adaptation) {
        update_adaptive_threshold(detector, energy);
    }
    
    // VAD integration
    if (vad_result) {
        result->vad_active = vad_result->voice_detected;
        result->speech_boundary_detected = vad_result->speech_started || vad_result->speech_ended;
        result->noise_floor = vad_result->noise_floor;
        result->snr_db = vad_result->snr_db;
    }
    
    // Check if we're in a potential wake word detection
    bool above_threshold = energy > detector->adaptive_threshold;
    bool vad_confirms = vad_result ? vad_result->voice_detected : true; // Assume voice if no VAD
    
    if (above_threshold && vad_confirms) {
        if (!detector->potential_wake_word) {
            // Start potential detection
            detector->potential_wake_word = true;
            detector->detection_frame_count = 0;
            detector->pattern_buffer_index = 0;
            ESP_LOGD(TAG, "Potential wake word detection started (energy: %d)", energy);
        }
        
        // Add to pattern buffer
        if (detector->pattern_buffer_index < MAX_DETECTION_FRAMES) {
            float normalized_energy = (float)energy / (detector->adaptive_threshold * 2.0f);
            normalized_energy = fminf(1.0f, normalized_energy); // Cap at 1.0
            detector->pattern_buffer[detector->pattern_buffer_index] = normalized_energy;
            detector->pattern_buffer_index++;
            detector->detection_frame_count++;
        }
        
        detector->silence_start_time = 0; // Reset silence timer
    } else {
        // Below threshold or VAD indicates no voice
        if (detector->potential_wake_word) {
            if (detector->silence_start_time == 0) {
                detector->silence_start_time = start_time;
            }
            
            uint64_t silence_duration = (start_time - detector->silence_start_time) / 1000;
            
            // Check if we have enough data and silence timeout
            if (detector->detection_frame_count >= MIN_DETECTION_FRAMES && 
                silence_duration > detector->config.silence_timeout_ms) {
                
                // Analyze the pattern
                float pattern_match = calculate_pattern_match(detector->pattern_buffer, 
                                                            detector->pattern_buffer_index);
                uint8_t syllable_count = detect_syllables(detector->pattern_buffer, 
                                                        detector->pattern_buffer_index);
                
                ESP_LOGD(TAG, "Pattern analysis - Match: %.3f, Syllables: %d, Frames: %d", 
                        pattern_match, syllable_count, detector->detection_frame_count);
                
                // Calculate overall confidence
                float confidence = pattern_match;
                
                // Boost confidence if syllable count matches expected
                if (syllable_count == EXPECTED_SYLLABLE_COUNT) {
                    confidence *= 1.2f; // 20% bonus for correct syllable count
                } else if (syllable_count >= MIN_SYLLABLE_COUNT && syllable_count <= MAX_SYLLABLE_COUNT) {
                    confidence *= 1.1f; // 10% bonus for reasonable syllable count
                } else {
                    confidence *= 0.8f; // Penalty for wrong syllable count
                }
                
                // VAD confidence integration
                if (vad_result && vad_result->high_confidence) {
                    confidence *= 1.1f; // Bonus for high VAD confidence
                }
                
                confidence = fminf(1.0f, confidence); // Cap at 1.0
                
                // Check if detection meets threshold
                if (confidence >= detector->config.confidence_threshold) {
                    // Wake word detected!
                    detector->state = WAKE_WORD_STATE_TRIGGERED;
                    detector->detection_id_counter++;
                    detector->last_detection_time = start_time;
                    
                    // Update statistics
                    detector->stats.total_detections++;
                    detector->stats.average_confidence = 
                        (detector->stats.average_confidence * (detector->stats.total_detections - 1) + 
                         confidence) / detector->stats.total_detections;
                    
                    // Fill result
                    result->state = WAKE_WORD_STATE_TRIGGERED;
                    result->confidence_score = confidence;
                    result->confidence_level = calculate_confidence_level(confidence);
                    result->detection_duration_ms = detector->detection_frame_count * 
                        (detector->config.frame_size * 1000 / detector->config.sample_rate);
                    result->pattern_match_score = (uint16_t)(pattern_match * 1000);
                    result->syllable_count = syllable_count;
                    result->detection_quality = (uint8_t)(confidence * 255);
                    
                    ESP_LOGI(TAG, "🎯 Wake word 'Hey Howdy' detected! Confidence: %.2f%% (ID: %lu)", 
                            confidence * 100, detector->detection_id_counter);
                    
                    // Call callback if set
                    if (detector->callback) {
                        detector->callback(result, detector->callback_user_data);
                    }
                } else {
                    ESP_LOGD(TAG, "Pattern rejected - confidence %.3f below threshold %.3f", 
                            confidence, detector->config.confidence_threshold);
                }
                
                // Reset detection state
                detector->potential_wake_word = false;
                detector->detection_frame_count = 0;
                detector->silence_start_time = 0;
            }
        }
    }
    
    // Calculate processing time
    uint64_t processing_time = esp_timer_get_time() - start_time;
    result->processing_time_us = (uint32_t)processing_time;
    
    // Update performance statistics
    detector->stats.average_processing_time_us = 
        (detector->stats.average_processing_time_us * 9 + processing_time) / 10; // Running average
    
    detector->stats.current_energy_threshold = detector->adaptive_threshold;
    detector->last_process_time = start_time;
    
    xSemaphoreGive(detector->mutex);
    
    return ESP_OK;
}

esp_err_t esp32_p4_wake_word_set_callback(esp32_p4_wake_word_handle_t handle,
                                         esp32_p4_wake_word_callback_t callback,
                                         void *user_data)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (xSemaphoreTake(detector->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        detector->callback = callback;
        detector->callback_user_data = user_data;
        xSemaphoreGive(detector->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t esp32_p4_wake_word_server_feedback(esp32_p4_wake_word_handle_t handle,
                                            uint32_t detection_id,
                                            bool validated,
                                            uint32_t response_time_ms)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (xSemaphoreTake(detector->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (validated) {
            detector->state = WAKE_WORD_STATE_CONFIRMED;
            detector->stats.true_positives++;
            detector->stats.consecutive_false_positives = 0;
            ESP_LOGI(TAG, "✅ Server confirmed wake word detection (ID: %lu, response: %lums)", 
                    detection_id, response_time_ms);
            
            // Slightly reduce threshold if we're getting good detections
            if (detector->config.enable_adaptation && detector->stats.true_positives > 5) {
                uint16_t adjustment = detector->adaptive_threshold * 0.02f; // 2% reduction
                detector->adaptive_threshold = detector->adaptive_threshold > adjustment ? 
                    detector->adaptive_threshold - adjustment : detector->adaptive_threshold;
                detector->stats.threshold_adjustments++;
            }
        } else {
            detector->state = WAKE_WORD_STATE_REJECTED;
            detector->stats.false_positives++;
            detector->stats.consecutive_false_positives++;
            ESP_LOGW(TAG, "❌ Server rejected wake word detection (ID: %lu, response: %lums)", 
                    detection_id, response_time_ms);
            
            // Increase threshold if we're getting too many false positives
            if (detector->config.enable_adaptation && detector->stats.consecutive_false_positives > 2) {
                uint16_t adjustment = detector->adaptive_threshold * 0.05f; // 5% increase
                detector->adaptive_threshold += adjustment;
                detector->stats.threshold_adjustments++;
                detector->stats.consecutive_false_positives = 0; // Reset after adjustment
            }
        }
        
        // Update false positive rate
        uint32_t total_feedback = detector->stats.true_positives + detector->stats.false_positives;
        detector->stats.false_positive_rate = total_feedback > 0 ? 
            (float)detector->stats.false_positives / total_feedback : 0.0f;
        
        // Ensure threshold stays within reasonable bounds
        detector->adaptive_threshold = fmax(detector->config.energy_threshold / 2, 
                                          fmin(detector->config.energy_threshold * 3, 
                                               detector->adaptive_threshold));
        
        xSemaphoreGive(detector->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

// Helper function implementations
static uint16_t calculate_audio_energy(const int16_t *audio_data, size_t sample_count)
{
    uint64_t energy_sum = 0;
    
    for (size_t i = 0; i < sample_count; i++) {
        int32_t sample = audio_data[i];
        energy_sum += (uint32_t)(sample * sample);
    }
    
    // Return RMS energy scaled to 16-bit range
    uint32_t rms_energy = (uint32_t)sqrt(energy_sum / sample_count);
    return (uint16_t)fmin(65535, rms_energy);
}

static float calculate_pattern_match(const float *pattern_buffer, uint8_t length)
{
    if (length < MIN_DETECTION_FRAMES) {
        return 0.0f;
    }
    
    // Simple correlation with expected "Hey Howdy" pattern
    float correlation = 0.0f;
    uint8_t comparison_length = fmin(length, HEY_HOWDY_PATTERN_LENGTH);
    
    // Calculate normalized cross-correlation
    float pattern_energy = 0.0f;
    float buffer_energy = 0.0f;
    
    for (uint8_t i = 0; i < comparison_length; i++) {
        pattern_energy += HEY_HOWDY_PATTERN[i] * HEY_HOWDY_PATTERN[i];
        buffer_energy += pattern_buffer[i] * pattern_buffer[i];
        correlation += HEY_HOWDY_PATTERN[i] * pattern_buffer[i];
    }
    
    if (pattern_energy > 0 && buffer_energy > 0) {
        correlation /= sqrt(pattern_energy * buffer_energy);
        correlation = fmax(0.0f, correlation); // Only positive correlation
    }
    
    return correlation;
}

static uint8_t detect_syllables(const float *energy_pattern, uint8_t length)
{
    if (length < 3) {
        return 0;
    }
    
    uint8_t syllable_count = 0;
    bool in_syllable = false;
    
    for (uint8_t i = 0; i < length; i++) {
        if (energy_pattern[i] > SYLLABLE_DETECTION_THRESHOLD) {
            if (!in_syllable) {
                syllable_count++;
                in_syllable = true;
            }
        } else {
            in_syllable = false;
        }
    }
    
    return syllable_count;
}

static void update_adaptive_threshold(esp32_p4_wake_word_detector_t *detector, uint16_t energy)
{
    // Simple adaptive threshold based on noise floor estimation
    static uint64_t last_update = 0;
    uint64_t now = esp_timer_get_time();
    
    // Update every 5 seconds
    if (now - last_update > 5000000) {
        // Calculate average background energy
        float avg_energy = 0.0f;
        for (int i = 0; i < ENERGY_HISTORY_SIZE; i++) {
            avg_energy += detector->energy_history[i];
        }
        avg_energy /= ENERGY_HISTORY_SIZE;
        
        // Set threshold as multiple of background noise
        uint16_t noise_based_threshold = (uint16_t)(avg_energy * 2.5f);
        
        // Smooth adjustment
        detector->adaptive_threshold = 
            (detector->adaptive_threshold * 0.9f) + (noise_based_threshold * 0.1f);
        
        // Keep within bounds
        detector->adaptive_threshold = fmax(detector->config.energy_threshold / 3,
                                          fmin(detector->config.energy_threshold * 4,
                                               detector->adaptive_threshold));
        
        last_update = now;
    }
}

static esp32_p4_wake_word_confidence_t calculate_confidence_level(float confidence_score)
{
    if (confidence_score >= 0.86f) {
        return WAKE_WORD_CONFIDENCE_VERY_HIGH;
    } else if (confidence_score >= 0.71f) {
        return WAKE_WORD_CONFIDENCE_HIGH;
    } else if (confidence_score >= 0.41f) {
        return WAKE_WORD_CONFIDENCE_MEDIUM;
    } else {
        return WAKE_WORD_CONFIDENCE_LOW;
    }
}

// Additional API implementations
esp_err_t esp32_p4_wake_word_get_default_config(esp32_p4_wake_word_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    config->sample_rate = 16000;
    config->frame_size = 320;              // 20ms frames
    config->energy_threshold = 3500;       // Moderate sensitivity
    config->confidence_threshold = 0.65f;  // 65% confidence required
    config->silence_timeout_ms = 2000;     // 2 second timeout
    config->pattern_frames = 20;           // Analyze 20 frames max
    config->consistency_frames = 3;        // Require 3 consistent frames
    config->enable_adaptation = true;      // Enable adaptive thresholds
    config->adaptation_rate = 0.05f;       // 5% adaptation rate
    config->processing_interval_ms = 20;   // 20ms processing interval
    config->max_detections_per_min = 10;   // Max 10 detections per minute
    
    return ESP_OK;
}

esp_err_t esp32_p4_wake_word_get_stats(esp32_p4_wake_word_handle_t handle,
                                      esp32_p4_wake_word_stats_t *stats)
{
    if (!handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (xSemaphoreTake(detector->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *stats = detector->stats;
        xSemaphoreGive(detector->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool esp32_p4_wake_word_is_listening(esp32_p4_wake_word_handle_t handle)
{
    if (!handle) {
        return false;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    return detector->enabled && detector->state == WAKE_WORD_STATE_LISTENING;
}

esp_err_t esp32_p4_wake_word_enable(esp32_p4_wake_word_handle_t handle, bool enable)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    detector->enabled = enable;
    
    if (enable) {
        detector->state = WAKE_WORD_STATE_LISTENING;
    }
    
    ESP_LOGI(TAG, "Wake word detection %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp32_p4_wake_word_state_t esp32_p4_wake_word_get_state(esp32_p4_wake_word_handle_t handle)
{
    if (!handle) {
        return WAKE_WORD_STATE_LISTENING;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    return detector->state;
}

esp_err_t esp32_p4_wake_word_update_thresholds(esp32_p4_wake_word_handle_t handle,
                                              uint16_t new_energy_threshold,
                                              float new_confidence_threshold)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp32_p4_wake_word_detector_t *detector = (esp32_p4_wake_word_detector_t*)handle;
    
    if (xSemaphoreTake(detector->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Validate threshold ranges
        if (new_energy_threshold > 0 && new_energy_threshold < 32768) {
            detector->config.energy_threshold = new_energy_threshold;
            detector->adaptive_threshold = new_energy_threshold;
            ESP_LOGI(TAG, "Updated energy threshold to %d", new_energy_threshold);
        }
        
        if (new_confidence_threshold >= 0.1f && new_confidence_threshold <= 1.0f) {
            detector->config.confidence_threshold = new_confidence_threshold;
            ESP_LOGI(TAG, "Updated confidence threshold to %.2f", new_confidence_threshold);
        }
        
        // Update statistics
        detector->stats.threshold_adjustments++;
        detector->stats.current_energy_threshold = detector->adaptive_threshold;
        
        xSemaphoreGive(detector->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}