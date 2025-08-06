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
    VAD_FEEDBACK_TTS_AUDIO_START = 0x06,        // TTS audio session start
    VAD_FEEDBACK_TTS_AUDIO_CHUNK = 0x07,        // TTS audio chunk data
    VAD_FEEDBACK_TTS_AUDIO_END = 0x08,          // TTS audio session end
    VAD_FEEDBACK_TTS_PLAYBACK_STATUS = 0x09,    // TTS playback status response
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
 * @brief TTS audio session information
 */
typedef struct {
    char session_id[32];                // Unique session identifier
    char response_text[256];            // Text being synthesized
    uint16_t estimated_duration_ms;     // Estimated playback duration
    uint16_t total_chunks_expected;     // Number of audio chunks expected
    uint32_t sample_rate;               // Audio sample rate (16000)
    uint8_t channels;                   // Number of channels (1 = mono)
    uint8_t bits_per_sample;            // Bits per sample (16)
    uint32_t total_samples;             // Total audio samples
    float volume;                       // Playback volume (0.0-1.0)
    uint16_t fade_in_ms;                // Fade in duration
    uint16_t fade_out_ms;               // Fade out duration
    bool interrupt_recording;           // Should interrupt microphone recording
    bool echo_cancellation;             // Enable echo cancellation
} vad_feedback_tts_session_t;

/**
 * @brief TTS audio chunk data
 */
typedef struct {
    char session_id[32];                // Session identifier
    uint16_t chunk_sequence;            // Chunk sequence number (0-based)
    uint16_t chunk_size;                // Size of audio data in bytes
    uint8_t *audio_data;                // PCM audio data (16-bit samples)
    bool is_final;                      // True if this is the last chunk
    uint32_t checksum;                  // Data integrity checksum
    uint16_t chunk_start_time_ms;       // Start time within session
    uint16_t chunk_duration_ms;         // Duration of this chunk
} vad_feedback_tts_chunk_t;

/**
 * @brief TTS audio session end information
 */
typedef struct {
    char session_id[32];                // Session identifier
    uint16_t total_chunks_sent;         // Total chunks sent by server
    uint32_t total_audio_bytes;         // Total audio data bytes
    uint16_t actual_duration_ms;        // Actual audio duration
    uint16_t transmission_time_ms;      // Time taken to transmit
    uint16_t fade_out_ms;               // Fade out duration
    bool return_to_listening;           // Should return to listening mode
    uint16_t cooldown_period_ms;        // Cooldown before listening
} vad_feedback_tts_end_t;

/**
 * @brief TTS playback status
 */
typedef struct {
    char session_id[32];                // Session identifier
    uint8_t playback_state;             // 0=idle, 1=buffering, 2=playing, 3=paused, 4=finished
    uint16_t chunks_received;           // Chunks received from server
    uint16_t chunks_played;             // Chunks played to speaker
    uint16_t buffer_level_ms;           // Current buffer level in milliseconds
    uint8_t audio_quality;              // Audio quality rating (0-100)
    uint16_t playback_latency_ms;       // Current playback latency
    uint16_t buffer_underruns;          // Number of buffer underruns
    uint16_t audio_dropouts;            // Number of audio dropouts
    float echo_suppression_db;          // Echo suppression level (dB)
} vad_feedback_tts_status_t;

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

/**
 * @brief Handle TTS audio session start from server
 * 
 * @param handle VAD feedback client handle
 * @param session_info TTS session information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_handle_tts_audio_start(vad_feedback_handle_t handle,
                                             const vad_feedback_tts_session_t *session_info);

/**
 * @brief Handle TTS audio chunk from server
 * 
 * @param handle VAD feedback client handle
 * @param chunk_data TTS audio chunk data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_handle_tts_audio_chunk(vad_feedback_handle_t handle,
                                             const vad_feedback_tts_chunk_t *chunk_data);

/**
 * @brief Handle TTS audio session end from server
 * 
 * @param handle VAD feedback client handle
 * @param end_info TTS session end information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_handle_tts_audio_end(vad_feedback_handle_t handle,
                                           const vad_feedback_tts_end_t *end_info);

/**
 * @brief Send TTS playback status to server
 * 
 * @param handle VAD feedback client handle
 * @param status_info Current playback status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_send_tts_playback_status(vad_feedback_handle_t handle,
                                               const vad_feedback_tts_status_t *status_info);

/**
 * @brief Set TTS audio callback for handling received audio
 * 
 * @param handle VAD feedback client handle
 * @param callback Function to call when TTS audio is received
 * @param user_data User data to pass to callback
 * @return esp_err_t ESP_OK on success
 */
typedef void (*vad_feedback_tts_audio_callback_t)(const vad_feedback_tts_session_t *session,
                                                 const int16_t *audio_data,
                                                 size_t sample_count,
                                                 void *user_data);

esp_err_t vad_feedback_set_tts_audio_callback(vad_feedback_handle_t handle,
                                             vad_feedback_tts_audio_callback_t callback,
                                             void *user_data);

/**
 * @brief Get current TTS session information
 * 
 * @param handle VAD feedback client handle
 * @param session_id Session ID to query
 * @param session_info Output session information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_get_tts_session_info(vad_feedback_handle_t handle,
                                           const char *session_id,
                                           vad_feedback_tts_session_t *session_info);

/**
 * @brief Cancel active TTS session
 * 
 * @param handle VAD feedback client handle
 * @param session_id Session ID to cancel
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_cancel_tts_session(vad_feedback_handle_t handle,
                                         const char *session_id);

/**
 * @brief Check if a TTS session is currently active
 * 
 * @param handle VAD feedback client handle
 * @return true if TTS session is active, false otherwise
 */
bool vad_feedback_is_tts_session_active(vad_feedback_handle_t handle);

/**
 * @brief Get TTS audio playback statistics
 * 
 * @param handle VAD feedback client handle
 * @param chunks_received Output pointer for chunks received count (can be NULL)
 * @param chunks_played Output pointer for chunks played count (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t vad_feedback_get_tts_stats(vad_feedback_handle_t handle,
                                     uint16_t *chunks_received,
                                     uint16_t *chunks_played);

#ifdef __cplusplus
}
#endif