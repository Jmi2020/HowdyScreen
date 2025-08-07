#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UDP audio streaming configuration
 */
typedef struct {
    const char *server_ip;      // Server IP address
    uint16_t server_port;       // Server UDP port (typically 8003)
    uint16_t local_port;        // Local UDP port for receiving
    size_t buffer_size;         // UDP buffer size in bytes
    uint32_t packet_size_ms;    // Audio packet duration in ms (e.g., 20ms)
    bool enable_compression;    // Enable audio compression
} udp_audio_config_t;

/**
 * @brief UDP audio packet header
 * 
 * Matches HowdyTTS UDP protocol format
 */
typedef struct __attribute__((packed)) {
    uint32_t sequence;          // Packet sequence number
    uint16_t sample_count;      // Number of samples in packet
    uint16_t sample_rate;       // Sample rate (16000 Hz)
    uint8_t  channels;          // Number of channels (1 = mono)
    uint8_t  bits_per_sample;   // Bits per sample (16)
    uint16_t flags;             // Packet flags (compression, etc.)
} udp_audio_header_t;

/**
 * @brief UDP audio statistics
 */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t sequence_errors;
    uint32_t socket_errors;
    float average_latency_ms;
} udp_audio_stats_t;

/**
 * @brief Audio receive callback
 * 
 * Called when audio data is received via UDP
 */
typedef void (*udp_audio_receive_cb_t)(const int16_t *samples, size_t sample_count, void *user_data);

/**
 * @brief Initialize UDP audio streamer
 * 
 * @param config UDP audio configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_init(const udp_audio_config_t *config);

/**
 * @brief Deinitialize UDP audio streamer
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_deinit(void);

/**
 * @brief Start UDP audio streaming
 * 
 * Creates receive task and opens sockets
 * 
 * @param receive_cb Callback for received audio (can be NULL)
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_start(udp_audio_receive_cb_t receive_cb, void *user_data);

/**
 * @brief Stop UDP audio streaming
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_stop(void);

/**
 * @brief Send audio samples via UDP
 * 
 * Packets audio data according to configured packet size
 * 
 * @param samples Audio samples (16-bit PCM mono)
 * @param sample_count Number of samples
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_send(const int16_t *samples, size_t sample_count);

/**
 * @brief Send pre-formatted audio packet
 * 
 * For advanced use - send complete packet with header
 * 
 * @param header Packet header
 * @param audio_data Audio payload
 * @param audio_size Audio data size in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_send_packet(const udp_audio_header_t *header, 
                               const uint8_t *audio_data, 
                               size_t audio_size);

/**
 * @brief Update server endpoint
 * 
 * Changes the destination for audio streaming
 * 
 * @param server_ip New server IP
 * @param server_port New server port
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_set_server(const char *server_ip, uint16_t server_port);

/**
 * @brief Get streaming statistics
 * 
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_get_stats(udp_audio_stats_t *stats);

/**
 * @brief Reset statistics counters
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_reset_stats(void);

/**
 * @brief Check if UDP audio is streaming
 * 
 * @return true if actively streaming
 */
bool udp_audio_is_streaming(void);

/**
 * @brief Set audio compression level
 * 
 * @param level Compression level (0-10, 0=disabled)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t udp_audio_set_compression(uint8_t level);

#ifdef __cplusplus
}
#endif