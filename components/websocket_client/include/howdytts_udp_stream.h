#pragma once

#include "esp_err.h"
#include "lwip/sockets.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UDP audio streaming configuration
 */
typedef struct {
    char server_ip[16];             // HowdyTTS server IP address
    uint16_t server_port;           // UDP audio port (e.g., 8081)
    uint16_t local_port;            // Local UDP port (0 for auto)
    uint32_t sample_rate;           // Audio sample rate (16000 Hz)
    uint8_t channels;               // Number of channels (1 for mono)
    uint8_t bits_per_sample;        // Bits per sample (16)
    size_t frame_size_samples;      // Samples per UDP packet (160 for 10ms at 16kHz)
    uint32_t send_interval_ms;      // Send interval in milliseconds (10ms)
    bool enable_sequence_numbers;   // Add sequence numbers to packets
} howdytts_udp_config_t;

/**
 * @brief UDP packet header for audio streaming
 */
typedef struct __attribute__((packed)) {
    uint32_t sequence_number;       // Packet sequence number
    uint32_t timestamp;             // Timestamp (ms since boot)
    uint16_t sample_rate;           // Sample rate (Hz)
    uint8_t channels;               // Number of channels
    uint8_t bits_per_sample;        // Bits per sample
    uint16_t frame_samples;         // Number of samples in this frame
    uint16_t reserved;              // Reserved for future use
    // Audio data follows immediately after header
} howdytts_udp_header_t;

/**
 * @brief UDP streaming statistics
 */
typedef struct {
    uint32_t packets_sent;          // Total packets sent
    uint32_t bytes_sent;            // Total bytes sent
    uint32_t send_errors;           // Number of send errors
    uint32_t dropped_frames;        // Frames dropped due to network issues
    float average_send_time_ms;     // Average time per packet send
    uint32_t last_sequence_number;  // Last sequence number sent
} howdytts_udp_stats_t;

/**
 * @brief Initialize UDP audio streaming
 * 
 * @param config UDP streaming configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_init(const howdytts_udp_config_t *config);

/**
 * @brief Start UDP audio streaming
 * 
 * Creates UDP socket and prepares for streaming
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_start(void);

/**
 * @brief Send audio frame via UDP
 * 
 * @param audio_samples PCM audio samples (16-bit)
 * @param num_samples Number of samples in frame
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_send_audio(const int16_t *audio_samples, size_t num_samples);

/**
 * @brief Send audio frame with custom timestamp
 * 
 * @param audio_samples PCM audio samples (16-bit)
 * @param num_samples Number of samples in frame
 * @param timestamp_ms Custom timestamp in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_send_audio_with_timestamp(const int16_t *audio_samples, size_t num_samples, 
                                                uint32_t timestamp_ms);

/**
 * @brief Update server IP address for streaming
 * 
 * @param server_ip New server IP address
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_update_server(const char *server_ip);

/**
 * @brief Get UDP streaming statistics
 * 
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_get_stats(howdytts_udp_stats_t *stats);

/**
 * @brief Reset UDP streaming statistics
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_reset_stats(void);

/**
 * @brief Check if UDP streaming is active
 * 
 * @return bool true if streaming is active
 */
bool howdytts_udp_is_active(void);

/**
 * @brief Stop UDP audio streaming
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_stop(void);

/**
 * @brief Cleanup UDP streaming resources
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_cleanup(void);

/**
 * @brief Set UDP streaming quality parameters
 * 
 * @param max_packet_loss_percent Maximum acceptable packet loss (0-100)
 * @param adaptive_frame_size Enable adaptive frame sizing based on network conditions
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_udp_set_quality_params(uint8_t max_packet_loss_percent, bool adaptive_frame_size);

#ifdef __cplusplus
}
#endif