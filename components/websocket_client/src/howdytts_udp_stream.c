#include "howdytts_udp_stream.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "HowdyTTSUDP";

// UDP streaming state
static struct {
    howdytts_udp_config_t config;
    int socket_fd;
    struct sockaddr_in server_addr;
    bool initialized;
    bool streaming_active;
    
    // Packet sequencing
    uint32_t sequence_number;
    
    // Statistics
    howdytts_udp_stats_t stats;
    uint64_t total_send_time_us;
    
    // Quality control
    uint8_t max_packet_loss_percent;
    bool adaptive_frame_size;
    
} s_udp_stream = {
    .socket_fd = -1,
    .initialized = false,
    .streaming_active = false,
    .sequence_number = 0,
    .max_packet_loss_percent = 5,
    .adaptive_frame_size = false,
};

esp_err_t howdytts_udp_init(const howdytts_udp_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid UDP configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->sample_rate == 0 || config->frame_size_samples == 0) {
        ESP_LOGE(TAG, "Invalid audio parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(config->server_ip) == 0 || config->server_port == 0) {
        ESP_LOGE(TAG, "Invalid server configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing UDP audio streaming");
    ESP_LOGI(TAG, "Server: %s:%d", config->server_ip, config->server_port);
    ESP_LOGI(TAG, "Audio: %lu Hz, %d ch, %d bit, %zu samples/frame", 
             config->sample_rate, config->channels, config->bits_per_sample, config->frame_size_samples);
    
    s_udp_stream.config = *config;
    s_udp_stream.initialized = true;
    s_udp_stream.sequence_number = 0;
    
    // Reset statistics
    memset(&s_udp_stream.stats, 0, sizeof(s_udp_stream.stats));
    s_udp_stream.total_send_time_us = 0;
    
    ESP_LOGI(TAG, "UDP audio streaming initialized successfully");
    return ESP_OK;
}

esp_err_t howdytts_udp_start(void)
{
    if (!s_udp_stream.initialized) {
        ESP_LOGE(TAG, "UDP streaming not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_udp_stream.streaming_active) {
        ESP_LOGW(TAG, "UDP streaming already active");
        return ESP_OK;
    }
    
    // Create UDP socket
    s_udp_stream.socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_stream.socket_fd < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    // Configure server address
    memset(&s_udp_stream.server_addr, 0, sizeof(s_udp_stream.server_addr));
    s_udp_stream.server_addr.sin_family = AF_INET;
    s_udp_stream.server_addr.sin_port = htons(s_udp_stream.config.server_port);
    
    if (inet_aton(s_udp_stream.config.server_ip, &s_udp_stream.server_addr.sin_addr) == 0) {
        ESP_LOGE(TAG, "Invalid server IP address: %s", s_udp_stream.config.server_ip);
        close(s_udp_stream.socket_fd);
        s_udp_stream.socket_fd = -1;
        return ESP_ERR_INVALID_ARG;
    }
    
    // Set socket options for low latency
    int enable = 1;
    if (setsockopt(s_udp_stream.socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Set socket to non-blocking for better real-time performance
    int flags = fcntl(s_udp_stream.socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(s_udp_stream.socket_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    s_udp_stream.streaming_active = true;
    s_udp_stream.sequence_number = 0;
    
    ESP_LOGI(TAG, "UDP audio streaming started (socket: %d)", s_udp_stream.socket_fd);
    return ESP_OK;
}

esp_err_t howdytts_udp_send_audio(const int16_t *audio_samples, size_t num_samples)
{
    uint32_t timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return howdytts_udp_send_audio_with_timestamp(audio_samples, num_samples, timestamp_ms);
}

esp_err_t howdytts_udp_send_audio_with_timestamp(const int16_t *audio_samples, size_t num_samples, 
                                                uint32_t timestamp_ms)
{
    if (!s_udp_stream.initialized || !s_udp_stream.streaming_active) {
        ESP_LOGE(TAG, "UDP streaming not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!audio_samples || num_samples == 0) {
        ESP_LOGE(TAG, "Invalid audio data");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_udp_stream.socket_fd < 0) {
        ESP_LOGE(TAG, "Invalid UDP socket");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Calculate packet size
    size_t header_size = sizeof(howdytts_udp_header_t);
    size_t audio_data_size = num_samples * sizeof(int16_t);
    size_t total_packet_size = header_size + audio_data_size;
    
    // Allocate packet buffer
    uint8_t *packet_buffer = malloc(total_packet_size);
    if (!packet_buffer) {
        ESP_LOGE(TAG, "Failed to allocate packet buffer");
        s_udp_stream.stats.send_errors++;
        return ESP_ERR_NO_MEM;
    }
    
    // Build packet header
    howdytts_udp_header_t *header = (howdytts_udp_header_t *)packet_buffer;
    header->sequence_number = s_udp_stream.sequence_number++;
    header->timestamp = timestamp_ms;
    header->sample_rate = (uint16_t)s_udp_stream.config.sample_rate;
    header->channels = s_udp_stream.config.channels;
    header->bits_per_sample = s_udp_stream.config.bits_per_sample;
    header->frame_samples = (uint16_t)num_samples;
    header->reserved = 0;
    
    // Copy audio data
    memcpy(packet_buffer + header_size, audio_samples, audio_data_size);
    
    // Send packet with timing measurement
    uint64_t send_start = esp_timer_get_time();
    
    ssize_t bytes_sent = sendto(s_udp_stream.socket_fd, packet_buffer, total_packet_size, 0,
                               (struct sockaddr *)&s_udp_stream.server_addr, sizeof(s_udp_stream.server_addr));
    
    uint64_t send_time_us = esp_timer_get_time() - send_start;
    
    free(packet_buffer);
    
    if (bytes_sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Non-blocking socket would block - not a critical error for real-time audio
            ESP_LOGD(TAG, "UDP send would block - dropping frame");
            s_udp_stream.stats.dropped_frames++;
        } else {
            ESP_LOGE(TAG, "UDP send failed: %s", strerror(errno));
            s_udp_stream.stats.send_errors++;
        }
        return ESP_FAIL;
    }
    
    if ((size_t)bytes_sent != total_packet_size) {
        ESP_LOGW(TAG, "Partial UDP send: %zd/%zu bytes", bytes_sent, total_packet_size);
        s_udp_stream.stats.send_errors++;
        return ESP_FAIL;
    }
    
    // Update statistics (save sequence number before freeing)
    uint32_t seq_num = header->sequence_number;
    s_udp_stream.stats.packets_sent++;
    s_udp_stream.stats.bytes_sent += bytes_sent;
    s_udp_stream.stats.last_sequence_number = seq_num;
    s_udp_stream.total_send_time_us += send_time_us;
    s_udp_stream.stats.average_send_time_ms = 
        (float)s_udp_stream.total_send_time_us / (float)s_udp_stream.stats.packets_sent / 1000.0f;
    
    ESP_LOGV(TAG, "UDP packet sent: seq=%lu, %zu samples, %zd bytes, %.2f ms", 
             seq_num, num_samples, bytes_sent, send_time_us / 1000.0f);
    
    return ESP_OK;
}

esp_err_t howdytts_udp_update_server(const char *server_ip)
{
    if (!server_ip || strlen(server_ip) == 0) {
        ESP_LOGE(TAG, "Invalid server IP");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Update configuration
    strncpy(s_udp_stream.config.server_ip, server_ip, sizeof(s_udp_stream.config.server_ip) - 1);
    s_udp_stream.config.server_ip[sizeof(s_udp_stream.config.server_ip) - 1] = '\0';
    
    // Update server address if streaming is active
    if (s_udp_stream.streaming_active) {
        if (inet_aton(server_ip, &s_udp_stream.server_addr.sin_addr) == 0) {
            ESP_LOGE(TAG, "Invalid server IP address: %s", server_ip);
            return ESP_ERR_INVALID_ARG;
        }
        
        ESP_LOGI(TAG, "Updated UDP server address to: %s", server_ip);
    }
    
    return ESP_OK;
}

esp_err_t howdytts_udp_get_stats(howdytts_udp_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *stats = s_udp_stream.stats;
    return ESP_OK;
}

esp_err_t howdytts_udp_reset_stats(void)
{
    ESP_LOGI(TAG, "Resetting UDP statistics");
    memset(&s_udp_stream.stats, 0, sizeof(s_udp_stream.stats));
    s_udp_stream.total_send_time_us = 0;
    return ESP_OK;
}

bool howdytts_udp_is_active(void)
{
    return s_udp_stream.initialized && s_udp_stream.streaming_active;
}

esp_err_t howdytts_udp_stop(void)
{
    if (!s_udp_stream.streaming_active) {
        ESP_LOGW(TAG, "UDP streaming not active");
        return ESP_OK;
    }
    
    if (s_udp_stream.socket_fd >= 0) {
        close(s_udp_stream.socket_fd);
        s_udp_stream.socket_fd = -1;
    }
    
    s_udp_stream.streaming_active = false;
    
    ESP_LOGI(TAG, "UDP audio streaming stopped");
    ESP_LOGI(TAG, "Final stats: %lu packets, %lu bytes, %.2f ms avg send time", 
             s_udp_stream.stats.packets_sent, s_udp_stream.stats.bytes_sent, 
             s_udp_stream.stats.average_send_time_ms);
    
    return ESP_OK;
}

esp_err_t howdytts_udp_cleanup(void)
{
    if (s_udp_stream.streaming_active) {
        howdytts_udp_stop();
    }
    
    s_udp_stream.initialized = false;
    ESP_LOGI(TAG, "UDP streaming cleanup completed");
    return ESP_OK;
}

esp_err_t howdytts_udp_set_quality_params(uint8_t max_packet_loss_percent, bool adaptive_frame_size)
{
    if (max_packet_loss_percent > 100) {
        ESP_LOGE(TAG, "Invalid packet loss threshold: %d%%", max_packet_loss_percent);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_udp_stream.max_packet_loss_percent = max_packet_loss_percent;
    s_udp_stream.adaptive_frame_size = adaptive_frame_size;
    
    ESP_LOGI(TAG, "Updated quality params: max loss=%d%%, adaptive_frames=%s", 
             max_packet_loss_percent, adaptive_frame_size ? "enabled" : "disabled");
    
    return ESP_OK;
}