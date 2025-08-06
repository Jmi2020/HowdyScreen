#pragma once

#include "esp_err.h"
#include "esp32_p4_wake_word.h"
#include "enhanced_vad.h"
#include "enhanced_udp_audio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP32-P4 VAD Feedback WebSocket Client
 * 
 * WebSocket client to connect to HowdyTTS server port 8001 for receiving
 * real-time VAD corrections and wake word feedback. Enables adaptive
 * learning and threshold updates based on server validation.
 */

// VAD feedback message types
typedef enum {
    VAD_FEEDBACK_WAKE_WORD_VALIDATION = 0x01,   // Wake word validation result
    VAD_FEEDBACK_THRESHOLD_UPDATE = 0x02,       // Threshold adjustment recommendation  
    VAD_FEEDBACK_TRAINING_DATA = 0x03,          // Training data for improvement
    VAD_FEEDBACK_STATISTICS_REQUEST = 0x04,     // Server requesting device statistics
    VAD_FEEDBACK_PING = 0x05,                   // Keep-alive ping
    VAD_FEEDBACK_ERROR = 0xFF                   // Error message
} vad_feedback_message_type_t;

/**
 * @brief VAD feedback configuration
 */
typedef struct {
    // Server connection
    char server_uri[128];               // WebSocket URI (ws://server:8001/vad_feedback)
    uint16_t server_port;               // Server port (8001)
    uint32_t connection_timeout_ms;     // Connection timeout (5000ms)
    
    // Client identification
    char device_id[32];                 // Device identifier
    char device_name[32];               // Human-readable device name
    char room[16];                      // Room location
    
    // Feedback settings
    bool enable_wake_word_feedback;     // Enable wake word validation feedback
    bool enable_threshold_adaptation;   // Enable automatic threshold adjustment
    bool enable_training_mode;          // Enable training data collection
    uint16_t feedback_timeout_ms;       // Feedback response timeout (3000ms)
    
    // Connection management
    bool auto_reconnect;                // Automatic reconnection
    uint16_t reconnect_interval_ms;     // Reconnection interval (10000ms)
    uint8_t max_reconnect_attempts;     // Maximum reconnection attempts (5)
    
    // Performance settings
    uint16_t keepalive_interval_ms;     // Keep-alive interval (30000ms)
    uint16_t message_queue_size;        // Message queue size (20)
    uint16_t buffer_size;               // WebSocket buffer size (2048)
} vad_feedback_config_t;

/**
 * @brief Wake word validation message
 */
typedef struct {
    uint32_t detection_id;              // Detection ID to validate
    bool validated;                     // True if wake word confirmed
    float server_confidence;            // Server's confidence score (0.0-1.0)
    uint32_t processing_time_ms;        // Server processing time
    char feedback_text[64];             // Optional feedback text
    
    // Improvement suggestions
    bool suggest_threshold_adjustment;   // Recommend threshold change
    int16_t threshold_delta;            // Suggested threshold change (+/-)
    float suggested_confidence;         // Suggested confidence threshold
} vad_feedback_wake_word_validation_t;

/**
 * @brief Threshold update recommendation
 */
typedef struct {
    uint16_t new_energy_threshold;      // Recommended energy threshold
    float new_confidence_threshold;     // Recommended confidence threshold  
    uint8_t urgency;                    // Update urgency (0-255)
    char reason[48];                    // Reason for update
    uint32_t expires_ms;                // Expiration time for recommendation
} vad_feedback_threshold_update_t;

/**
 * @brief Training data request
 */
typedef struct {
    bool collect_positive_samples;      // Collect positive wake word samples
    bool collect_negative_samples;      // Collect false positive samples
    uint16_t sample_duration_ms;        // Required sample duration
    uint8_t samples_requested;          // Number of samples needed
    char instructions[96];              // Instructions for user
} vad_feedback_training_request_t;

/**
 * @brief VAD feedback statistics
 */
typedef struct {
    // Connection statistics
    uint32_t messages_sent;             // Messages sent to server
    uint32_t messages_received;         // Messages received from server
    uint32_t connection_uptime_s;       // Connection uptime in seconds
    uint16_t reconnections;             // Number of reconnections
    
    // Validation statistics  
    uint32_t wake_word_validations;     // Wake word validations received
    uint32_t positive_validations;     // Positive validations
    uint32_t negative_validations;      // Negative validations
    float validation_accuracy;          // Validation accuracy (0.0-1.0)
    
    // Adaptation statistics
    uint16_t threshold_updates;         // Threshold updates applied
    uint16_t training_samples_sent;     // Training samples sent
    uint32_t average_feedback_time_ms;  // Average feedback response time
    
    // Performance metrics
    uint32_t bytes_transmitted;         // Total bytes transmitted
    uint32_t bytes_received;            // Total bytes received
    float average_latency_ms;           // Average message latency
} vad_feedback_stats_t;

/**
 * @brief VAD feedback event callback
 * 
 * Called when feedback events are received from server
 */
typedef void (*vad_feedback_event_callback_t)(vad_feedback_message_type_t type, 
                                             const void *data, 
                                             size_t data_len, 
                                             void *user_data);

/**
 * @brief VAD feedback client handle (opaque)
 */
typedef struct vad_feedback_client* vad_feedback_handle_t;

/**
 * @brief Initialize VAD feedback WebSocket client
 * 
 * @param config Client configuration
 * @param event_callback Event callback function
 * @param user_data User data for callback
 * @return VAD feedback client handle, NULL on failure
 */
vad_feedback_handle_t vad_feedback_init(const vad_feedback_config_t *config,
                                       vad_feedback_event_callback_t event_callback,
                                       void *user_data);

/**
 * @brief Deinitialize VAD feedback client
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_deinit(vad_feedback_handle_t handle);

/**
 * @brief Start VAD feedback WebSocket connection
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_connect(vad_feedback_handle_t handle);

/**
 * @brief Stop VAD feedback WebSocket connection
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_disconnect(vad_feedback_handle_t handle);

/**
 * @brief Send wake word detection event to server for validation
 * 
 * @param handle VAD feedback client handle
 * @param detection_id Local detection ID
 * @param wake_word_result Wake word detection result
 * @param vad_result Enhanced VAD result (optional)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_send_wake_word_detection(vad_feedback_handle_t handle,
                                              uint32_t detection_id,
                                              const esp32_p4_wake_word_result_t *wake_word_result,
                                              const enhanced_vad_result_t *vad_result);

/**
 * @brief Send device statistics to server
 * 
 * @param handle VAD feedback client handle
 * @param wake_word_stats Wake word detection statistics
 * @param vad_stats Enhanced VAD statistics (optional)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_send_statistics(vad_feedback_handle_t handle,
                                      const esp32_p4_wake_word_stats_t *wake_word_stats,
                                      const enhanced_udp_audio_stats_t *vad_stats);

/**
 * @brief Send training audio sample to server
 * 
 * @param handle VAD feedback client handle
 * @param audio_data Audio sample (16-bit PCM)
 * @param sample_count Number of samples
 * @param is_positive_sample True if positive wake word sample
 * @param metadata Optional metadata string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_send_training_sample(vad_feedback_handle_t handle,
                                          const int16_t *audio_data,
                                          size_t sample_count,
                                          bool is_positive_sample,
                                          const char *metadata);

/**
 * @brief Apply threshold update received from server
 * 
 * @param handle VAD feedback client handle
 * @param wake_word_handle Wake word detector handle to update
 * @param threshold_update Threshold update data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_apply_threshold_update(vad_feedback_handle_t handle,
                                            esp32_p4_wake_word_handle_t wake_word_handle,
                                            const vad_feedback_threshold_update_t *threshold_update);

/**
 * @brief Send keep-alive ping to server
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_ping(vad_feedback_handle_t handle);

/**
 * @brief Check if VAD feedback client is connected
 * 
 * @param handle VAD feedback client handle
 * @return true if connected to server
 */
bool vad_feedback_is_connected(vad_feedback_handle_t handle);

/**
 * @brief Get VAD feedback statistics
 * 
 * @param handle VAD feedback client handle
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_get_stats(vad_feedback_handle_t handle,
                                vad_feedback_stats_t *stats);

/**
 * @brief Reset VAD feedback statistics
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_reset_stats(vad_feedback_handle_t handle);

/**
 * @brief Get default VAD feedback configuration
 * 
 * @param server_ip Server IP address
 * @param device_id Device identifier
 * @param config Output configuration structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_get_default_config(const char *server_ip,
                                         const char *device_id,
                                         vad_feedback_config_t *config);

/**
 * @brief Enable/disable training mode
 * 
 * @param handle VAD feedback client handle
 * @param enable True to enable training mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_set_training_mode(vad_feedback_handle_t handle, bool enable);

/**
 * @brief Update device location information
 * 
 * @param handle VAD feedback client handle
 * @param room New room location
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_update_location(vad_feedback_handle_t handle, const char *room);

/**
 * @brief Request server to send current threshold recommendations
 * 
 * @param handle VAD feedback client handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_request_threshold_update(vad_feedback_handle_t handle);

#ifdef __cplusplus
}
#endif