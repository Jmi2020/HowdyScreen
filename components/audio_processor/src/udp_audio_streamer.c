#include "udp_audio_streamer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "UDPAudio";

#define UDP_RECV_TASK_STACK_SIZE    4096
#define UDP_RECV_TASK_PRIORITY      18
#define UDP_MAX_PACKET_SIZE         1472  // Typical MTU - IP/UDP headers
#define UDP_RECV_TIMEOUT_MS         100

// UDP audio streamer state
static struct {
    udp_audio_config_t config;
    int send_socket;
    int recv_socket;
    struct sockaddr_in server_addr;
    bool is_initialized;
    bool is_streaming;
    uint32_t sequence_number;
    
    // Receive handling
    udp_audio_receive_cb_t receive_callback;
    void *callback_user_data;
    TaskHandle_t receive_task_handle;
    
    // Statistics
    udp_audio_stats_t stats;
    
    // Thread safety
    SemaphoreHandle_t mutex;
    
    // Packet buffer for assembly
    uint8_t packet_buffer[UDP_MAX_PACKET_SIZE];
    size_t packet_buffer_used;
    uint32_t samples_per_packet;
    // Store a safe copy of server IP
    char server_ip_str[16];
} s_udp_audio = {
    .send_socket = -1,
    .recv_socket = -1,
    .is_initialized = false,
    .is_streaming = false,
    .sequence_number = 0,
    .mutex = NULL
};

// Forward declarations
static void udp_receive_task(void *pvParameters);
static esp_err_t create_sockets(void);
static void close_sockets(void);
static size_t calculate_packet_size(uint32_t packet_ms, uint32_t sample_rate);

esp_err_t udp_audio_init(const udp_audio_config_t *config)
{
    if (!config || !config->server_ip) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_udp_audio.is_initialized) {
        ESP_LOGI(TAG, "UDP audio already initialized");
        return ESP_OK;
    }
    
    // Create mutex
    s_udp_audio.mutex = xSemaphoreCreateMutex();
    if (!s_udp_audio.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration and take ownership of server IP via internal buffer
    s_udp_audio.config = *config;
    if (config->server_ip) {
        strncpy(s_udp_audio.server_ip_str, config->server_ip, sizeof(s_udp_audio.server_ip_str) - 1);
        s_udp_audio.server_ip_str[sizeof(s_udp_audio.server_ip_str) - 1] = '\0';
        s_udp_audio.config.server_ip = s_udp_audio.server_ip_str;
    } else {
        s_udp_audio.server_ip_str[0] = '\0';
        s_udp_audio.config.server_ip = s_udp_audio.server_ip_str;
    }
    
    // Calculate samples per packet based on duration
    s_udp_audio.samples_per_packet = calculate_packet_size(
        config->packet_size_ms, 16000  // 16kHz sample rate
    );
    
    // Setup server address
    memset(&s_udp_audio.server_addr, 0, sizeof(s_udp_audio.server_addr));
    s_udp_audio.server_addr.sin_family = AF_INET;
    s_udp_audio.server_addr.sin_port = htons(config->server_port);
    inet_pton(AF_INET, s_udp_audio.config.server_ip, &s_udp_audio.server_addr.sin_addr);
    
    s_udp_audio.is_initialized = true;
    
    ESP_LOGI(TAG, "UDP audio initialized - Server: %s:%d, Packet: %lums (%lu samples)",
             config->server_ip, config->server_port, 
             config->packet_size_ms, s_udp_audio.samples_per_packet);
    
    return ESP_OK;
}

esp_err_t udp_audio_deinit(void)
{
    if (!s_udp_audio.is_initialized) {
        return ESP_OK;
    }
    
    // Stop streaming if active
    udp_audio_stop();
    
    if (s_udp_audio.mutex) {
        vSemaphoreDelete(s_udp_audio.mutex);
        s_udp_audio.mutex = NULL;
    }
    
    s_udp_audio.is_initialized = false;
    
    ESP_LOGI(TAG, "UDP audio deinitialized");
    return ESP_OK;
}

esp_err_t udp_audio_start(udp_audio_receive_cb_t receive_cb, void *user_data)
{
    if (!s_udp_audio.is_initialized) {
        ESP_LOGE(TAG, "UDP audio not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_udp_audio.is_streaming) {
        ESP_LOGI(TAG, "UDP audio already streaming");
        return ESP_OK;
    }
    
    // Create sockets
    esp_err_t ret = create_sockets();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sockets");
        return ret;
    }
    
    // Store callback
    s_udp_audio.receive_callback = receive_cb;
    s_udp_audio.callback_user_data = user_data;
    
    // Create receive task if callback provided
    if (receive_cb) {
        BaseType_t task_ret = xTaskCreatePinnedToCore(
            udp_receive_task,
            "udp_audio_rx",
            UDP_RECV_TASK_STACK_SIZE,
            NULL,
            UDP_RECV_TASK_PRIORITY,
            &s_udp_audio.receive_task_handle,
            1  // Core 1 for network tasks
        );
        
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create receive task");
            close_sockets();
            return ESP_FAIL;
        }
    }
    
    s_udp_audio.is_streaming = true;
    s_udp_audio.sequence_number = 0;
    s_udp_audio.packet_buffer_used = 0;
    
    ESP_LOGI(TAG, "UDP audio streaming started");
    return ESP_OK;
}

esp_err_t udp_audio_stop(void)
{
    if (!s_udp_audio.is_streaming) {
        return ESP_OK;
    }
    
    s_udp_audio.is_streaming = false;
    
    // Stop receive task
    if (s_udp_audio.receive_task_handle) {
        // Task will exit on next receive timeout
        vTaskDelay(pdMS_TO_TICKS(UDP_RECV_TIMEOUT_MS + 50));
        s_udp_audio.receive_task_handle = NULL;
    }
    
    // Close sockets
    close_sockets();
    
    ESP_LOGI(TAG, "UDP audio streaming stopped");
    ESP_LOGI(TAG, "Stats - Sent: %lu packets (%lu bytes), Received: %lu packets (%lu bytes)",
             s_udp_audio.stats.packets_sent, s_udp_audio.stats.bytes_sent,
             s_udp_audio.stats.packets_received, s_udp_audio.stats.bytes_received);
    
    return ESP_OK;
}

esp_err_t udp_audio_send(const int16_t *samples, size_t sample_count)
{
    if (!s_udp_audio.is_streaming || s_udp_audio.send_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!samples || sample_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_udp_audio.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    size_t samples_processed = 0;
    esp_err_t ret = ESP_OK;
    
    while (samples_processed < sample_count && ret == ESP_OK) {
        // Calculate how many samples to add to current packet
        size_t current_packet_samples = s_udp_audio.packet_buffer_used / sizeof(int16_t);
        size_t samples_needed = s_udp_audio.samples_per_packet - current_packet_samples;
        size_t samples_to_copy = MIN(samples_needed, sample_count - samples_processed);
        
        // Copy samples to packet buffer
        memcpy(s_udp_audio.packet_buffer + s_udp_audio.packet_buffer_used,
               &samples[samples_processed],
               samples_to_copy * sizeof(int16_t));
        
        s_udp_audio.packet_buffer_used += samples_to_copy * sizeof(int16_t);
        samples_processed += samples_to_copy;
        
        // Send packet if full
        if (s_udp_audio.packet_buffer_used >= s_udp_audio.samples_per_packet * sizeof(int16_t)) {
            // Create header
            udp_audio_header_t header = {
                .sequence = s_udp_audio.sequence_number++,
                .sample_count = s_udp_audio.samples_per_packet,
                .sample_rate = 16000,
                .channels = 1,
                .bits_per_sample = 16,
                .flags = s_udp_audio.config.enable_compression ? 0x0001 : 0x0000
            };
            
            // Build complete packet
            uint8_t packet[UDP_MAX_PACKET_SIZE];
            memcpy(packet, &header, sizeof(header));
            memcpy(packet + sizeof(header), s_udp_audio.packet_buffer, s_udp_audio.packet_buffer_used);
            
            size_t packet_size = sizeof(header) + s_udp_audio.packet_buffer_used;
            
            // Send packet
            ssize_t sent = sendto(s_udp_audio.send_socket, packet, packet_size, 0,
                                 (struct sockaddr *)&s_udp_audio.server_addr,
                                 sizeof(s_udp_audio.server_addr));
            
            if (sent < 0) {
                ESP_LOGE(TAG, "Failed to send UDP packet: %d", errno);
                s_udp_audio.stats.socket_errors++;
                ret = ESP_FAIL;
            } else {
                s_udp_audio.stats.packets_sent++;
                s_udp_audio.stats.bytes_sent += sent;
                ESP_LOGV(TAG, "Sent UDP packet %lu (%zd bytes)", header.sequence, sent);
            }
            
            // Reset packet buffer
            s_udp_audio.packet_buffer_used = 0;
        }
    }
    
    xSemaphoreGive(s_udp_audio.mutex);
    return ret;
}

esp_err_t udp_audio_send_packet(const udp_audio_header_t *header, 
                               const uint8_t *audio_data, 
                               size_t audio_size)
{
    if (!s_udp_audio.is_streaming || s_udp_audio.send_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!header || !audio_data || audio_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build complete packet
    uint8_t packet[UDP_MAX_PACKET_SIZE];
    size_t header_size = sizeof(udp_audio_header_t);
    
    if (header_size + audio_size > UDP_MAX_PACKET_SIZE) {
        ESP_LOGE(TAG, "Packet too large: %zu bytes", header_size + audio_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    memcpy(packet, header, header_size);
    memcpy(packet + header_size, audio_data, audio_size);
    
    ssize_t sent = sendto(s_udp_audio.send_socket, packet, header_size + audio_size, 0,
                         (struct sockaddr *)&s_udp_audio.server_addr,
                         sizeof(s_udp_audio.server_addr));
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send UDP packet: %d", errno);
        s_udp_audio.stats.socket_errors++;
        return ESP_FAIL;
    }
    
    s_udp_audio.stats.packets_sent++;
    s_udp_audio.stats.bytes_sent += sent;
    
    return ESP_OK;
}

static void udp_receive_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UDP receive task started");
    
    uint8_t recv_buffer[UDP_MAX_PACKET_SIZE];
    struct sockaddr_in source_addr;
    socklen_t addr_len = sizeof(source_addr);
    
    // Set receive timeout
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = UDP_RECV_TIMEOUT_MS * 1000
    };
    setsockopt(s_udp_audio.recv_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    while (s_udp_audio.is_streaming) {
        ssize_t received = recvfrom(s_udp_audio.recv_socket, recv_buffer, sizeof(recv_buffer), 0,
                                   (struct sockaddr *)&source_addr, &addr_len);
        
        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "UDP receive error: %d", errno);
                s_udp_audio.stats.socket_errors++;
            }
            continue;
        }
        
        // Validate packet size
        if (received < sizeof(udp_audio_header_t)) {
            ESP_LOGW(TAG, "Received packet too small: %zd bytes", received);
            continue;
        }
        
        // Parse header
        udp_audio_header_t *header = (udp_audio_header_t *)recv_buffer;
        size_t audio_size = received - sizeof(udp_audio_header_t);
        
        // Validate audio size matches header
        size_t expected_size = header->sample_count * sizeof(int16_t);
        if (audio_size != expected_size) {
            ESP_LOGW(TAG, "Audio size mismatch: expected %zu, got %zu", expected_size, audio_size);
            continue;
        }
        
        // Check sequence number
        static uint32_t last_sequence = 0;
        if (header->sequence != last_sequence + 1 && last_sequence != 0) {
            s_udp_audio.stats.sequence_errors++;
            ESP_LOGW(TAG, "Sequence gap: expected %lu, got %lu", last_sequence + 1, header->sequence);
        }
        last_sequence = header->sequence;
        
        // Update statistics
        s_udp_audio.stats.packets_received++;
        s_udp_audio.stats.bytes_received += received;
        
        // Invoke callback with audio data
        if (s_udp_audio.receive_callback) {
            int16_t *audio_data = (int16_t *)(recv_buffer + sizeof(udp_audio_header_t));
            s_udp_audio.receive_callback(audio_data, header->sample_count, 
                                       s_udp_audio.callback_user_data);
        }
        
        ESP_LOGV(TAG, "Received UDP packet %lu (%zd bytes)", header->sequence, received);
    }
    
    ESP_LOGI(TAG, "UDP receive task stopped");
    vTaskDelete(NULL);
}

static esp_err_t create_sockets(void)
{
    // Create send socket
    s_udp_audio.send_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_audio.send_socket < 0) {
        ESP_LOGE(TAG, "Failed to create send socket: %d", errno);
        return ESP_FAIL;
    }
    
    // Create receive socket
    s_udp_audio.recv_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_udp_audio.recv_socket < 0) {
        ESP_LOGE(TAG, "Failed to create receive socket: %d", errno);
        close(s_udp_audio.send_socket);
        s_udp_audio.send_socket = -1;
        return ESP_FAIL;
    }
    
    // Bind receive socket to local port
    struct sockaddr_in local_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_udp_audio.config.local_port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    
    if (bind(s_udp_audio.recv_socket, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind receive socket: %d", errno);
        close_sockets();
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "UDP sockets created - Send: %d, Receive: %d (port %d)",
             s_udp_audio.send_socket, s_udp_audio.recv_socket, s_udp_audio.config.local_port);
    
    return ESP_OK;
}

static void close_sockets(void)
{
    if (s_udp_audio.send_socket >= 0) {
        close(s_udp_audio.send_socket);
        s_udp_audio.send_socket = -1;
    }
    
    if (s_udp_audio.recv_socket >= 0) {
        close(s_udp_audio.recv_socket);
        s_udp_audio.recv_socket = -1;
    }
}

static size_t calculate_packet_size(uint32_t packet_ms, uint32_t sample_rate)
{
    // Calculate samples per packet based on duration
    // e.g., 20ms at 16kHz = 320 samples
    return (sample_rate * packet_ms) / 1000;
}

esp_err_t udp_audio_set_server(const char *server_ip, uint16_t server_port)
{
    if (!server_ip) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_udp_audio.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Update server address
    s_udp_audio.server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &s_udp_audio.server_addr.sin_addr);
    
    // Update config
    strncpy(s_udp_audio.server_ip_str, server_ip, sizeof(s_udp_audio.server_ip_str) - 1);
    s_udp_audio.server_ip_str[sizeof(s_udp_audio.server_ip_str) - 1] = '\0';
    s_udp_audio.config.server_ip = s_udp_audio.server_ip_str;
    s_udp_audio.config.server_port = server_port;
    
    xSemaphoreGive(s_udp_audio.mutex);
    
    ESP_LOGI(TAG, "Server endpoint updated: %s:%d", server_ip, server_port);
    return ESP_OK;
}

esp_err_t udp_audio_get_stats(udp_audio_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *stats = s_udp_audio.stats;
    return ESP_OK;
}

esp_err_t udp_audio_reset_stats(void)
{
    memset(&s_udp_audio.stats, 0, sizeof(s_udp_audio.stats));
    return ESP_OK;
}

bool udp_audio_is_streaming(void)
{
    return s_udp_audio.is_streaming;
}

esp_err_t udp_audio_set_compression(uint8_t level)
{
    if (level > 10) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_udp_audio.config.enable_compression = (level > 0);
    
    ESP_LOGI(TAG, "Compression %s (level %d)", 
             s_udp_audio.config.enable_compression ? "enabled" : "disabled", level);
    
    return ESP_OK;
}
