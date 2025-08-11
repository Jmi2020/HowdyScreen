/**
 * @file howdytts_network_integration.c
 * @brief HowdyTTS Native Protocol Integration Implementation
 * 
 * ESP32-P4 optimized implementation with raw PCM streaming and minimal memory overhead.
 */

#include "howdytts_network_integration.h"
#include "udp_audio_streamer.h"
#include "dual_i2s_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static const char *TAG = "HowdyTTS";

// Global state structure
typedef struct {
    // Configuration
    howdytts_integration_config_t config;
    howdytts_integration_callbacks_t callbacks;
    
    // Connection state
    howdytts_connection_state_t connection_state;
    howdytts_va_state_t va_state;
    howdytts_protocol_mode_t current_protocol;
    
    // Server information
    howdytts_server_info_t connected_server;
    howdytts_server_info_t discovered_servers[8];
    size_t discovered_server_count;
    
    // Network sockets
    int discovery_socket;
    int audio_socket;
    int http_server_handle;
    
    // Audio statistics
    howdytts_audio_stats_t audio_stats;
    uint32_t sequence_number;
    
    // Tasks and timers
    TaskHandle_t discovery_task;
    TaskHandle_t audio_streaming_task;
    esp_timer_handle_t keepalive_timer;
    
    // Synchronization
    SemaphoreHandle_t state_mutex;
    bool initialized;
    bool discovery_active;
    bool streaming_active;
    
} howdytts_integration_state_t;

static howdytts_integration_state_t s_howdytts_state = {0};

// Forward declarations
static void discovery_task(void *pvParameters);
static void audio_streaming_task(void *pvParameters);
static esp_err_t start_http_server(void);
static esp_err_t stop_http_server(void);
static esp_err_t send_discovery_request(void);
static esp_err_t handle_discovery_response(const char *response, const char *from_ip);
static esp_err_t create_audio_packet(const int16_t *audio_data, size_t samples, 
                                   howdytts_pcm_packet_t **packet, size_t *packet_size);
static void audio_streaming_task(void *pvParameters);

// Utility functions
static const char* connection_state_to_string(howdytts_connection_state_t state)
{
    switch (state) {
        case HOWDYTTS_STATE_DISCONNECTED: return "DISCONNECTED";
        case HOWDYTTS_STATE_DISCOVERING: return "DISCOVERING";
        case HOWDYTTS_STATE_CONNECTING: return "CONNECTING";
        case HOWDYTTS_STATE_CONNECTED: return "CONNECTED";
        case HOWDYTTS_STATE_STREAMING: return "STREAMING";
        case HOWDYTTS_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static const char* va_state_to_string(howdytts_va_state_t state)
{
    switch (state) {
        case HOWDYTTS_VA_STATE_WAITING: return "waiting";
        case HOWDYTTS_VA_STATE_LISTENING: return "listening";
        case HOWDYTTS_VA_STATE_THINKING: return "thinking";
        case HOWDYTTS_VA_STATE_SPEAKING: return "speaking";
        case HOWDYTTS_VA_STATE_ENDING: return "ending";
        default: return "unknown";
    }
}

static howdytts_va_state_t string_to_va_state(const char *state_str)
{
    if (!state_str) return HOWDYTTS_VA_STATE_WAITING;
    
    if (strcmp(state_str, "waiting") == 0) return HOWDYTTS_VA_STATE_WAITING;
    if (strcmp(state_str, "listening") == 0) return HOWDYTTS_VA_STATE_LISTENING;
    if (strcmp(state_str, "thinking") == 0) return HOWDYTTS_VA_STATE_THINKING;
    if (strcmp(state_str, "speaking") == 0) return HOWDYTTS_VA_STATE_SPEAKING;
    if (strcmp(state_str, "ending") == 0) return HOWDYTTS_VA_STATE_ENDING;
    
    return HOWDYTTS_VA_STATE_WAITING;
}

static void set_connection_state(howdytts_connection_state_t new_state)
{
    if (xSemaphoreTake(s_howdytts_state.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_howdytts_state.connection_state != new_state) {
            howdytts_connection_state_t old_state = s_howdytts_state.connection_state;
            s_howdytts_state.connection_state = new_state;
            
            ESP_LOGI(TAG, "Connection state: %s -> %s", 
                    connection_state_to_string(old_state),
                    connection_state_to_string(new_state));
            
            // Notify callback with appropriate event type based on state
            if (s_howdytts_state.callbacks.event_callback) {
                howdytts_event_data_t event = {
                    .data.connection_state = new_state,
                    .timestamp = esp_timer_get_time() / 1000
                };
                
                // Set appropriate event type based on new state
                switch (new_state) {
                    case HOWDYTTS_STATE_DISCOVERING:
                        event.event_type = HOWDYTTS_EVENT_DISCOVERY_STARTED;
                        snprintf(event.message, sizeof(event.message), "Discovery started");
                        break;
                    case HOWDYTTS_STATE_CONNECTED:
                        event.event_type = HOWDYTTS_EVENT_CONNECTION_ESTABLISHED;
                        snprintf(event.message, sizeof(event.message), "Connection established");
                        break;
                    case HOWDYTTS_STATE_DISCONNECTED:
                    case HOWDYTTS_STATE_ERROR:
                        event.event_type = HOWDYTTS_EVENT_CONNECTION_LOST;
                        snprintf(event.message, sizeof(event.message), "Connection lost: %s", connection_state_to_string(new_state));
                        break;
                    default:
                        // For other states (CONNECTING, STREAMING), just log but don't send events
                        ESP_LOGD(TAG, "State transition to %s (no event sent)", connection_state_to_string(new_state));
                        xSemaphoreGive(s_howdytts_state.state_mutex);
                        return;
                }
                
                s_howdytts_state.callbacks.event_callback(&event, s_howdytts_state.callbacks.user_data);
            }
        }
        xSemaphoreGive(s_howdytts_state.state_mutex);
    }
}

static void set_va_state(howdytts_va_state_t new_state, const char *state_text)
{
    if (xSemaphoreTake(s_howdytts_state.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_howdytts_state.va_state != new_state) {
            howdytts_va_state_t old_state = s_howdytts_state.va_state;
            s_howdytts_state.va_state = new_state;
            
            ESP_LOGI(TAG, "Voice assistant state: %s -> %s", 
                    va_state_to_string(old_state),
                    va_state_to_string(new_state));
            
            // Notify callback
            if (s_howdytts_state.callbacks.va_state_callback) {
                s_howdytts_state.callbacks.va_state_callback(new_state, state_text, 
                                                           s_howdytts_state.callbacks.user_data);
            }
        }
        xSemaphoreGive(s_howdytts_state.state_mutex);
    }
}

// HTTP Server Handlers
static esp_err_t http_state_handler(httpd_req_t *req)
{
    char content[256];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    ESP_LOGI(TAG, "Received state update: %s", content);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (json) {
        cJSON *state_item = cJSON_GetObjectItem(json, "state");
        if (state_item && cJSON_IsString(state_item)) {
            howdytts_va_state_t new_state = string_to_va_state(state_item->valuestring);
            set_va_state(new_state, NULL);
        }
        cJSON_Delete(json);
    }
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t http_speak_handler(httpd_req_t *req)
{
    char content[512];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    int ret = httpd_req_recv(req, content, recv_size);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    ESP_LOGI(TAG, "Received speak update: %s", content);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (json) {
        cJSON *state_item = cJSON_GetObjectItem(json, "state");
        cJSON *text_item = cJSON_GetObjectItem(json, "text");
        
        if (state_item && cJSON_IsString(state_item)) {
            howdytts_va_state_t new_state = string_to_va_state(state_item->valuestring);
            const char *text = text_item && cJSON_IsString(text_item) ? text_item->valuestring : NULL;
            set_va_state(new_state, text);
        }
        cJSON_Delete(json);
    }
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t http_status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "device_id", s_howdytts_state.config.device_id);
    cJSON_AddStringToObject(json, "device_name", s_howdytts_state.config.device_name);
    cJSON_AddStringToObject(json, "room", s_howdytts_state.config.room);
    cJSON_AddStringToObject(json, "device_type", HOWDYTTS_DEVICE_TYPE);
    cJSON_AddStringToObject(json, "connection_state", connection_state_to_string(s_howdytts_state.connection_state));
    cJSON_AddStringToObject(json, "va_state", va_state_to_string(s_howdytts_state.va_state));
    cJSON_AddNumberToObject(json, "audio_level", s_howdytts_state.audio_stats.average_latency_ms);
    cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    
    cJSON *capabilities = cJSON_CreateArray();
    cJSON_AddItemToArray(capabilities, cJSON_CreateString("display"));
    cJSON_AddItemToArray(capabilities, cJSON_CreateString("touch"));
    cJSON_AddItemToArray(capabilities, cJSON_CreateString("audio"));
    cJSON_AddItemToArray(capabilities, cJSON_CreateString("tts"));
    cJSON_AddItemToArray(capabilities, cJSON_CreateString("lvgl"));
    cJSON_AddItemToObject(json, "capabilities", capabilities);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    return ESP_OK;
}

static esp_err_t http_health_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "status", "healthy");
    cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "min_free_heap", esp_get_minimum_free_heap_size());
    
    // Audio statistics
    cJSON *audio_stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(audio_stats, "packets_sent", s_howdytts_state.audio_stats.packets_sent);
    cJSON_AddNumberToObject(audio_stats, "packets_received", s_howdytts_state.audio_stats.packets_received);
    cJSON_AddNumberToObject(audio_stats, "packet_loss_rate", s_howdytts_state.audio_stats.packet_loss_rate);
    cJSON_AddNumberToObject(audio_stats, "average_latency_ms", s_howdytts_state.audio_stats.average_latency_ms);
    cJSON_AddItemToObject(json, "audio_stats", audio_stats);
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = HOWDYTTS_HTTP_PORT;
    config.max_uri_handlers = 8;
    
    if (httpd_start((httpd_handle_t*)&s_howdytts_state.http_server_handle, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    // Register handlers
    httpd_uri_t state_uri = {
        .uri = "/state",
        .method = HTTP_POST,
        .handler = http_state_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler((httpd_handle_t)s_howdytts_state.http_server_handle, &state_uri);
    
    httpd_uri_t speak_uri = {
        .uri = "/speak",
        .method = HTTP_POST,
        .handler = http_speak_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler((httpd_handle_t)s_howdytts_state.http_server_handle, &speak_uri);
    
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = http_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler((httpd_handle_t)s_howdytts_state.http_server_handle, &status_uri);
    
    httpd_uri_t health_uri = {
        .uri = "/health",
        .method = HTTP_GET,
        .handler = http_health_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler((httpd_handle_t)s_howdytts_state.http_server_handle, &health_uri);
    
    ESP_LOGI(TAG, "HTTP server started on port %d", HOWDYTTS_HTTP_PORT);
    return ESP_OK;
}

static esp_err_t stop_http_server(void)
{
    if (s_howdytts_state.http_server_handle) {
        httpd_stop((httpd_handle_t)s_howdytts_state.http_server_handle);
        s_howdytts_state.http_server_handle = 0;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    return ESP_OK;
}

// UDP Discovery Implementation
static esp_err_t send_discovery_request(void)
{
    if (s_howdytts_state.discovery_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    const char *discovery_msg = HOWDYTTS_DISCOVERY_REQUEST;
    struct sockaddr_in broadcast_addr;
    memset(&broadcast_addr, 0, sizeof(broadcast_addr));
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(HOWDYTTS_DISCOVERY_PORT);
    
    // Try multiple broadcast addresses to work around router restrictions
    // 1. Global broadcast (255.255.255.255)
    broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
    int sent = sendto(s_howdytts_state.discovery_socket, discovery_msg, strlen(discovery_msg), 0,
                     (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    if (sent > 0) {
        ESP_LOGI(TAG, "Sent discovery request to 255.255.255.255:8001");
    }
    
    // 2. Try subnet broadcast (e.g., 192.168.86.255 for 192.168.86.x network)
    // Get our IP to determine subnet
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            // Calculate subnet broadcast address
            uint32_t subnet_broadcast = (ip_info.ip.addr & ip_info.netmask.addr) | ~ip_info.netmask.addr;
            broadcast_addr.sin_addr.s_addr = subnet_broadcast;
            
            sent = sendto(s_howdytts_state.discovery_socket, discovery_msg, strlen(discovery_msg), 0,
                         (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
            if (sent > 0) {
                char addr_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &broadcast_addr.sin_addr, addr_str, sizeof(addr_str));
                ESP_LOGI(TAG, "Sent discovery request to subnet broadcast %s:8001", addr_str);
            }
        }
    }
    
    // Future: Could add direct unicast to known server IPs if needed
    return ESP_OK;
}

static esp_err_t handle_discovery_response(const char *response, const char *from_ip)
{
    ESP_LOGI(TAG, "🔍 Processing discovery response: '%s' from %s", response, from_ip);
    
    // Check if this is a HowdyTTS server response
    if (strncmp(response, "HOWDYTTS_SERVER", 15) != 0) {
        ESP_LOGW(TAG, "Not a HowdyTTS server response, ignoring: %s", response);
        return ESP_OK; // Not a HowdyTTS server, ignore
    }
    
    ESP_LOGI(TAG, "✅ Discovered HowdyTTS server at %s: %s", from_ip, response);
    
    // Parse server info from response
    howdytts_server_info_t server_info = {0};
    
    // Safe string copy with explicit null termination
    size_t ip_len = strlen(from_ip);
    size_t max_ip_len = sizeof(server_info.ip_address) - 1;
    if (ip_len > max_ip_len) {
        ip_len = max_ip_len;
    }
    memcpy(server_info.ip_address, from_ip, ip_len);
    server_info.ip_address[ip_len] = '\0';
    
    server_info.discovery_port = HOWDYTTS_DISCOVERY_PORT;
    server_info.audio_port = HOWDYTTS_AUDIO_PORT;
    server_info.http_port = HOWDYTTS_HTTP_PORT;
    server_info.is_available = true;
    server_info.last_seen = esp_timer_get_time() / 1000;
    
    // Extract hostname from response if available
    char *hostname_start = strchr(response, '_');
    if (hostname_start && hostname_start[1]) {
        size_t hostname_len = strlen(hostname_start + 1);
        size_t max_hostname_len = sizeof(server_info.hostname) - 1;
        if (hostname_len > max_hostname_len) {
            hostname_len = max_hostname_len;
        }
        memcpy(server_info.hostname, hostname_start + 1, hostname_len);
        server_info.hostname[hostname_len] = '\0';
    } else {
        snprintf(server_info.hostname, sizeof(server_info.hostname), "howdytts-%s", from_ip);
    }
    
    // Add to discovered servers list
    if (xSemaphoreTake(s_howdytts_state.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Check if server already exists
        bool found = false;
        for (size_t i = 0; i < s_howdytts_state.discovered_server_count; i++) {
            if (strcmp(s_howdytts_state.discovered_servers[i].ip_address, from_ip) == 0) {
                s_howdytts_state.discovered_servers[i] = server_info; // Update existing
                found = true;
                break;
            }
        }
        
        // Add new server if not found and we have space
        if (!found && s_howdytts_state.discovered_server_count < 8) {
            s_howdytts_state.discovered_servers[s_howdytts_state.discovered_server_count] = server_info;
            s_howdytts_state.discovered_server_count++;
            ESP_LOGI(TAG, "📥 Added new server to list: %s (total: %d)", from_ip, (int)s_howdytts_state.discovered_server_count);
        } else if (found) {
            ESP_LOGI(TAG, "🔄 Updated existing server: %s", from_ip);
        } else {
            ESP_LOGW(TAG, "Cannot add server %s: list full (%d/8)", from_ip, (int)s_howdytts_state.discovered_server_count);
        }
        
        xSemaphoreGive(s_howdytts_state.state_mutex);
    }
    
    // Notify callback
    if (s_howdytts_state.callbacks.event_callback) {
        howdytts_event_data_t event = {
            .event_type = HOWDYTTS_EVENT_SERVER_DISCOVERED,
            .data.server_info = server_info,
            .timestamp = esp_timer_get_time() / 1000
        };
        snprintf(event.message, sizeof(event.message), 
                "Discovered server %s at %s", server_info.hostname, from_ip);
        s_howdytts_state.callbacks.event_callback(&event, s_howdytts_state.callbacks.user_data);
    }
    
    return ESP_OK;
}

static void discovery_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Discovery task started");
    
    // Create UDP socket for discovery
    s_howdytts_state.discovery_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_howdytts_state.discovery_socket < 0) {
        ESP_LOGE(TAG, "Failed to create discovery socket");
        set_connection_state(HOWDYTTS_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    
    // Enable broadcast
    int broadcast_enable = 1;
    if (setsockopt(s_howdytts_state.discovery_socket, SOL_SOCKET, SO_BROADCAST,
                   &broadcast_enable, sizeof(broadcast_enable)) < 0) {
        ESP_LOGE(TAG, "Failed to enable broadcast");
        close(s_howdytts_state.discovery_socket);
        set_connection_state(HOWDYTTS_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    
    // Bind socket to ensure we can receive responses
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;  // Let system choose port
    
    if (bind(s_howdytts_state.discovery_socket, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind discovery socket: %s", strerror(errno));
        close(s_howdytts_state.discovery_socket);
        set_connection_state(HOWDYTTS_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Discovery socket bound and ready for responses");
    
    // Set receive timeout
    struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(s_howdytts_state.discovery_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    set_connection_state(HOWDYTTS_STATE_DISCOVERING);
    
    uint32_t discovery_start = esp_timer_get_time() / 1000;
    uint32_t last_broadcast = 0;
    
    while (s_howdytts_state.discovery_active && 
           (esp_timer_get_time() / 1000 - discovery_start) < s_howdytts_state.config.discovery_timeout_ms) {

        // Exit early if we connected meanwhile
        if (s_howdytts_state.connection_state == HOWDYTTS_STATE_CONNECTED ||
            s_howdytts_state.connection_state == HOWDYTTS_STATE_STREAMING) {
            ESP_LOGI(TAG, "Discovery aborted: already connected");
            break;
        }
        
        uint32_t now = esp_timer_get_time() / 1000;
        
        // Send discovery request every 2 seconds
        if (now - last_broadcast > 2000) {
            send_discovery_request();
            last_broadcast = now;
        }
        
        // Listen for responses
        char response_buffer[256];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        
        int received = recvfrom(s_howdytts_state.discovery_socket, response_buffer, 
                               sizeof(response_buffer) - 1, 0, 
                               (struct sockaddr*)&from_addr, &from_len);
        
        if (received > 0) {
            response_buffer[received] = '\0';
            char from_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, INET_ADDRSTRLEN);
            
            ESP_LOGI(TAG, "📡 Received discovery response from %s:%d - '%s'", 
                     from_ip, ntohs(from_addr.sin_port), response_buffer);
            
            handle_discovery_response(response_buffer, from_ip);
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGW(TAG, "Discovery recvfrom error: %s", strerror(errno));
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    close(s_howdytts_state.discovery_socket);
    s_howdytts_state.discovery_socket = -1;
    s_howdytts_state.discovery_active = false;
    
    ESP_LOGI(TAG, "Discovery task completed, found %d servers", 
             (int)s_howdytts_state.discovered_server_count);
    
    vTaskDelete(NULL);
}

// PCM Audio Packet Creation
static esp_err_t create_audio_packet(const int16_t *audio_data, size_t samples, 
                                   howdytts_pcm_packet_t **packet, size_t *packet_size)
{
    if (!audio_data || samples == 0 || !packet || !packet_size) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate packet size
    size_t data_size = samples * sizeof(int16_t);
    size_t total_size = sizeof(howdytts_pcm_packet_t) + data_size;
    
    // Allocate packet
    howdytts_pcm_packet_t *pkt = (howdytts_pcm_packet_t*)malloc(total_size);
    if (!pkt) {
        ESP_LOGE(TAG, "Failed to allocate audio packet memory");
        return ESP_ERR_NO_MEM;
    }
    
    // Fill packet header
    pkt->sequence = ++s_howdytts_state.sequence_number;
    pkt->timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    pkt->samples = (uint16_t)samples;
    pkt->reserved = 0;
    
    // Copy audio data
    memcpy(pkt->audio_data, audio_data, data_size);
    
    *packet = pkt;
    *packet_size = total_size;
    
    return ESP_OK;
}

// Audio Streaming Task
static void audio_streaming_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Audio streaming task started - capturing audio from I2S");
    
    const TickType_t frame_delay = pdMS_TO_TICKS(20); // 20ms frame interval (320 samples at 16kHz)
    TickType_t last_wake_time = xTaskGetTickCount();
    
    // Audio capture buffer - 320 samples for 20ms at 16kHz
    const size_t samples_per_frame = 320;
    int16_t audio_buffer[samples_per_frame];
    size_t bytes_read = 0;
    
    uint32_t packets_sent = 0;
    uint32_t capture_errors = 0;
    uint32_t send_errors = 0;
    
    ESP_LOGI(TAG, "Audio streaming: %zu samples per frame, %dms intervals", 
             samples_per_frame, 20);
    
    while (s_howdytts_state.streaming_active) {
        vTaskDelayUntil(&last_wake_time, frame_delay);
        
        // Check if we're still connected
        if (s_howdytts_state.connection_state != HOWDYTTS_STATE_STREAMING &&
            s_howdytts_state.connection_state != HOWDYTTS_STATE_CONNECTED) {
            ESP_LOGW(TAG, "Audio streaming task: connection lost");
            break;
        }
        
        // Capture audio from I2S microphone using Dual I2S Manager
        esp_err_t capture_ret = dual_i2s_read_mic(audio_buffer, samples_per_frame, &bytes_read, 10);
        
        if (capture_ret == ESP_OK && bytes_read > 0) {
            size_t samples_read = bytes_read / sizeof(int16_t);
            
            // Send ESP32-P4 basic UDP packet to server via UDP streamer
            esp_err_t send_ret = udp_audio_send(audio_buffer, samples_read);
            
            if (send_ret == ESP_OK) {
                packets_sent++;
                
                // Log progress every 5 seconds (250 packets)
                if (packets_sent % 250 == 0) {
                    ESP_LOGI(TAG, "📊 Audio streaming: %lu packets sent, %lu errors", 
                             packets_sent, capture_errors + send_errors);
                }
            } else {
                send_errors++;
                if (send_errors % 50 == 0) { // Log every 50 send errors
                    ESP_LOGW(TAG, "❌ Audio send error #%lu: %s", send_errors, esp_err_to_name(send_ret));
                }
            }
        } else {
            capture_errors++;
            if (capture_errors % 50 == 0) { // Log every 50 capture errors
                ESP_LOGW(TAG, "❌ Audio capture error #%lu: %s (bytes: %zu)", 
                         capture_errors, esp_err_to_name(capture_ret), bytes_read);
            }
        }
    }
    
    ESP_LOGI(TAG, "Audio streaming task ended");
    s_howdytts_state.streaming_active = false;
    s_howdytts_state.audio_streaming_task = NULL;
    
    // Notify that streaming stopped
    if (s_howdytts_state.callbacks.event_callback) {
        howdytts_event_data_t event = {
            .event_type = HOWDYTTS_EVENT_AUDIO_STREAMING_STOPPED,
            .timestamp = esp_timer_get_time() / 1000
        };
        snprintf(event.message, sizeof(event.message), "Audio streaming stopped");
        s_howdytts_state.callbacks.event_callback(&event, s_howdytts_state.callbacks.user_data);
    }
    
    vTaskDelete(NULL);
}

// Public API Implementation
esp_err_t howdytts_integration_init(const howdytts_integration_config_t *config,
                                   const howdytts_integration_callbacks_t *callbacks)
{
    if (!config || !callbacks) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_howdytts_state.initialized) {
        ESP_LOGW(TAG, "HowdyTTS integration already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing HowdyTTS integration for device: %s", config->device_id);
    
    // Initialize state
    memset(&s_howdytts_state, 0, sizeof(s_howdytts_state));
    s_howdytts_state.config = *config;
    s_howdytts_state.callbacks = *callbacks;
    s_howdytts_state.connection_state = HOWDYTTS_STATE_DISCONNECTED;
    s_howdytts_state.va_state = HOWDYTTS_VA_STATE_WAITING;
    s_howdytts_state.current_protocol = config->protocol_mode;
    s_howdytts_state.discovery_socket = -1;
    s_howdytts_state.audio_socket = -1;
    
    // Create mutex
    s_howdytts_state.state_mutex = xSemaphoreCreateMutex();
    if (!s_howdytts_state.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Start HTTP server for state management
    esp_err_t ret = start_http_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        vSemaphoreDelete(s_howdytts_state.state_mutex);
        return ret;
    }
    
    s_howdytts_state.initialized = true;
    
    ESP_LOGI(TAG, "HowdyTTS integration initialized successfully");
    return ESP_OK;
}

esp_err_t howdytts_discovery_start(uint32_t timeout_ms)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_howdytts_state.discovery_active) {
        ESP_LOGW(TAG, "Discovery already active");
        return ESP_OK;
    }
    
    // Update timeout if provided
    if (timeout_ms > 0) {
        s_howdytts_state.config.discovery_timeout_ms = timeout_ms;
    }
    
    s_howdytts_state.discovery_active = true;
    s_howdytts_state.discovered_server_count = 0;
    
    // Start discovery task
    BaseType_t result = xTaskCreate(discovery_task, "howdytts_discovery", 8192, NULL, 5, 
                                   &s_howdytts_state.discovery_task);
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create discovery task");
        s_howdytts_state.discovery_active = false;
        return ESP_ERR_NO_MEM;
    }
    
    // Notify callback
    if (s_howdytts_state.callbacks.event_callback) {
        howdytts_event_data_t event = {
            .event_type = HOWDYTTS_EVENT_DISCOVERY_STARTED,
            .timestamp = esp_timer_get_time() / 1000
        };
        snprintf(event.message, sizeof(event.message), "Discovery started with %d ms timeout", 
                (int)timeout_ms);
        s_howdytts_state.callbacks.event_callback(&event, s_howdytts_state.callbacks.user_data);
    }
    
    ESP_LOGI(TAG, "HowdyTTS discovery started (timeout: %d ms)", (int)timeout_ms);
    return ESP_OK;
}

esp_err_t howdytts_discovery_stop(void)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_howdytts_state.discovery_active = false;
    
    if (s_howdytts_state.discovery_task) {
        // Task will self-delete when discovery_active becomes false
        s_howdytts_state.discovery_task = NULL;
    }
    
    ESP_LOGI(TAG, "HowdyTTS discovery stopped");
    return ESP_OK;
}

// Getter functions
howdytts_connection_state_t howdytts_get_connection_state(void)
{
    return s_howdytts_state.connection_state;
}

howdytts_va_state_t howdytts_get_va_state(void)
{
    return s_howdytts_state.va_state;
}

howdytts_protocol_mode_t howdytts_get_protocol_mode(void)
{
    return s_howdytts_state.current_protocol;
}

bool howdytts_is_available(void)
{
    return s_howdytts_state.initialized && 
           s_howdytts_state.connection_state != HOWDYTTS_STATE_ERROR;
}

esp_err_t howdytts_get_discovered_servers(howdytts_server_info_t *servers, 
                                         size_t max_servers, 
                                         size_t *server_count)
{
    if (!servers || !server_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_howdytts_state.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t count = MIN(s_howdytts_state.discovered_server_count, max_servers);
        memcpy(servers, s_howdytts_state.discovered_servers, count * sizeof(howdytts_server_info_t));
        *server_count = count;
        xSemaphoreGive(s_howdytts_state.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t howdytts_integration_deinit(void)
{
    if (!s_howdytts_state.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing HowdyTTS integration");
    
    // Stop audio streaming
    howdytts_stop_audio_streaming();
    
    // Disconnect from server
    howdytts_disconnect();
    
    // Stop discovery
    howdytts_discovery_stop();
    
    // Stop HTTP server
    stop_http_server();
    
    // Clean up resources
    if (s_howdytts_state.state_mutex) {
        vSemaphoreDelete(s_howdytts_state.state_mutex);
    }
    
    // Reset state
    memset(&s_howdytts_state, 0, sizeof(s_howdytts_state));
    
    ESP_LOGI(TAG, "HowdyTTS integration deinitialized");
    return ESP_OK;
}

// Audio streaming implementation
esp_err_t howdytts_connect_to_server(const howdytts_server_info_t *server_info)
{
    if (!server_info || !s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop discovery if it's still running
    howdytts_discovery_stop();

    ESP_LOGI(TAG, "Connecting to HowdyTTS server %s at %s", 
             server_info->hostname, server_info->ip_address);
    
    // Create UDP socket for audio streaming
    s_howdytts_state.audio_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_howdytts_state.audio_socket < 0) {
        ESP_LOGE(TAG, "Failed to create audio streaming socket");
        return ESP_FAIL;
    }
    
    // Set socket timeout
    struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(s_howdytts_state.audio_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(s_howdytts_state.audio_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    // Store connected server info
    s_howdytts_state.connected_server = *server_info;
    
    // Reset sequence number and statistics
    s_howdytts_state.sequence_number = 0;
    memset(&s_howdytts_state.audio_stats, 0, sizeof(s_howdytts_state.audio_stats));
    s_howdytts_state.audio_stats.connection_count++;
    
    set_connection_state(HOWDYTTS_STATE_CONNECTED);
    
    ESP_LOGI(TAG, "✅ Connected to HowdyTTS server successfully");
    return ESP_OK;
}

esp_err_t howdytts_disconnect(void)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting from HowdyTTS server");
    
    // Stop audio streaming if active
    if (s_howdytts_state.streaming_active) {
        howdytts_stop_audio_streaming();
    }
    
    // Close audio socket
    if (s_howdytts_state.audio_socket >= 0) {
        close(s_howdytts_state.audio_socket);
        s_howdytts_state.audio_socket = -1;
    }
    
    // Clear server information
    memset(&s_howdytts_state.connected_server, 0, sizeof(s_howdytts_state.connected_server));
    
    set_connection_state(HOWDYTTS_STATE_DISCONNECTED);
    
    ESP_LOGI(TAG, "Disconnected from HowdyTTS server");
    return ESP_OK;
}

esp_err_t howdytts_stream_audio(const int16_t *audio_data, size_t samples)
{
    if (!audio_data || samples == 0 || !s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_howdytts_state.connection_state != HOWDYTTS_STATE_CONNECTED && 
        s_howdytts_state.connection_state != HOWDYTTS_STATE_STREAMING) {
        ESP_LOGW(TAG, "Cannot stream audio - not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_howdytts_state.audio_socket < 0) {
        ESP_LOGE(TAG, "Audio socket not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create PCM packet
    howdytts_pcm_packet_t *packet = NULL;
    size_t packet_size = 0;
    esp_err_t ret = create_audio_packet(audio_data, samples, &packet, &packet_size);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Send packet to server
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(s_howdytts_state.connected_server.audio_port);
    inet_pton(AF_INET, s_howdytts_state.connected_server.ip_address, &server_addr.sin_addr);
    
    int sent = sendto(s_howdytts_state.audio_socket, packet, packet_size, 0,
                     (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    free(packet);
    
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send audio packet: errno %d", errno);
        s_howdytts_state.audio_stats.packets_lost++;
        return ESP_FAIL;
    }
    
    // Update statistics
    s_howdytts_state.audio_stats.packets_sent++;
    s_howdytts_state.audio_stats.bytes_sent += sent;
    s_howdytts_state.audio_stats.last_update_time = esp_timer_get_time() / 1000;
    
    // Calculate packet loss rate
    uint32_t total_packets = s_howdytts_state.audio_stats.packets_sent + s_howdytts_state.audio_stats.packets_lost;
    if (total_packets > 0) {
        s_howdytts_state.audio_stats.packet_loss_rate = 
            (float)s_howdytts_state.audio_stats.packets_lost / total_packets;
    }
    
    // Update connection state if this is first packet
    if (s_howdytts_state.connection_state == HOWDYTTS_STATE_CONNECTED) {
        set_connection_state(HOWDYTTS_STATE_STREAMING);
    }
    
    return ESP_OK;
}

esp_err_t howdytts_start_audio_streaming(void)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_howdytts_state.connection_state != HOWDYTTS_STATE_CONNECTED) {
        ESP_LOGW(TAG, "Cannot start audio streaming - not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_howdytts_state.streaming_active) {
        ESP_LOGW(TAG, "Audio streaming already active");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🎵 Starting HowdyTTS audio streaming");
    
    s_howdytts_state.streaming_active = true;

    // Initialize and start UDP audio streamer with discovered server
    udp_audio_config_t udp_cfg = {
        .server_ip = s_howdytts_state.connected_server.ip_address,
        .server_port = s_howdytts_state.connected_server.audio_port,
        .local_port = 0,
        .buffer_size = 2048,
        .packet_size_ms = 20,
        .enable_compression = false
    };
    (void)udp_audio_deinit();
    if (udp_audio_init(&udp_cfg) == ESP_OK) {
        udp_audio_start(NULL, NULL);
    } else {
        ESP_LOGW(TAG, "UDP audio init failed; streaming may not send packets");
    }
    
    // Start audio streaming task with increased stack size to prevent overflow
    BaseType_t result = xTaskCreate(audio_streaming_task, "howdytts_audio", 8192, NULL, 6, 
                                   &s_howdytts_state.audio_streaming_task);
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio streaming task");
        s_howdytts_state.streaming_active = false;
        return ESP_ERR_NO_MEM;
    }
    
    // Notify that streaming started
    if (s_howdytts_state.callbacks.event_callback) {
        howdytts_event_data_t event = {
            .event_type = HOWDYTTS_EVENT_AUDIO_STREAMING_STARTED,
            .timestamp = esp_timer_get_time() / 1000
        };
        snprintf(event.message, sizeof(event.message), "Audio streaming started to %s", 
                s_howdytts_state.connected_server.hostname);
        s_howdytts_state.callbacks.event_callback(&event, s_howdytts_state.callbacks.user_data);
    }
    
    ESP_LOGI(TAG, "✅ Audio streaming started successfully");
    return ESP_OK;
}

esp_err_t howdytts_stop_audio_streaming(void)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_howdytts_state.streaming_active) {
        return ESP_OK; // Already stopped
    }
    
    ESP_LOGI(TAG, "🔇 Stopping HowdyTTS audio streaming");
    
    // Signal streaming task to stop
    s_howdytts_state.streaming_active = false;
    
    // Wait for task to complete (with timeout)
    if (s_howdytts_state.audio_streaming_task) {
        uint32_t timeout_start = esp_timer_get_time() / 1000;
        while (s_howdytts_state.audio_streaming_task && 
               (esp_timer_get_time() / 1000 - timeout_start) < 1000) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Force delete if task didn't exit cleanly
        if (s_howdytts_state.audio_streaming_task) {
            vTaskDelete(s_howdytts_state.audio_streaming_task);
            s_howdytts_state.audio_streaming_task = NULL;
        }
    }
    
    // Update connection state if we were streaming
    if (s_howdytts_state.connection_state == HOWDYTTS_STATE_STREAMING) {
        set_connection_state(HOWDYTTS_STATE_CONNECTED);
    }
    
    // Stop UDP audio streamer
    udp_audio_stop();
    ESP_LOGI(TAG, "Audio streaming stopped");
    return ESP_OK;
}

esp_err_t howdytts_get_audio_stats(howdytts_audio_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *stats = s_howdytts_state.audio_stats;
    return ESP_OK;
}

esp_err_t howdytts_update_device_status(float audio_level, int battery_level, int signal_strength)
{
    if (!s_howdytts_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update internal audio level for statistics
    if (audio_level >= 0.0f && audio_level <= 1.0f) {
        // Simple moving average for audio level
        static float avg_level = 0.0f;
        avg_level = (avg_level * 0.9f) + (audio_level * 0.1f);
        s_howdytts_state.audio_stats.average_latency_ms = avg_level; // Reuse this field for audio level
    }
    
    // Device status updates could be sent to server periodically
    // For now, just log the update
    ESP_LOGD(TAG, "Device status update: audio=%.2f, battery=%d%%, signal=%ddBm", 
             audio_level, battery_level, signal_strength);
    
    return ESP_OK;
}

esp_err_t howdytts_set_protocol_mode(howdytts_protocol_mode_t mode)
{
    s_howdytts_state.current_protocol = mode;
    return ESP_OK;
}
