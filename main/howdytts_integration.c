#include "howdytts_integration.h"
#include "service_discovery.h"
#include "websocket_client.h"
#include "udp_audio_streamer.h"
#include "howdytts_http_server.h"
#include "continuous_audio_processor.h"
#include "voice_assistant_ui.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "HowdyIntegration";

// Integration state
static struct {
    howdytts_integration_config_t config;
    network_manager_t network_manager;
    howdytts_connection_status_t status;
    bool is_initialized;
    bool is_running;
    
    // Discovered server info
    char discovered_server_ip[16];
    uint16_t discovered_ws_port;
    uint16_t discovered_udp_port;
} s_integration = {
    .is_initialized = false,
    .is_running = false
};

// Forward declarations
static void handle_server_discovered(const char *server_ip, uint16_t server_port);
static void handle_websocket_state(ws_client_state_t state, ws_message_type_t msg_type, 
                                  const uint8_t *data, size_t len);
static void handle_http_state_change(howdytts_state_t state, const char *text);
static void handle_http_discovery(const char *server_ip, uint16_t server_port);
static esp_err_t setup_audio_streaming(void);

esp_err_t howdytts_integration_init(const howdytts_integration_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_integration.is_initialized) {
        ESP_LOGI(TAG, "HowdyTTS integration already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing HowdyTTS integration");
    
    // Copy configuration
    s_integration.config = *config;
    
    // Initialize network manager with WiFi credentials
    esp_err_t ret = network_manager_init(&s_integration.network_manager, 
                                         config->wifi_ssid, 
                                         config->wifi_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize WebSocket client if enabled
    if (config->enable_websocket) {
        ws_client_config_t ws_config = {
            .server_uri = "",  // Will be set when server is discovered
            .reconnect_timeout_ms = 5000,
            .keepalive_idle_sec = 30,
            .keepalive_interval_sec = 10,
            .keepalive_count = 3,
            .event_callback = handle_websocket_state
        };
        
        ret = ws_client_init(&ws_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Initialize UDP audio streamer if enabled
    if (config->enable_udp_streaming) {
        udp_audio_config_t udp_config = {
            .server_ip = config->server_ip ? config->server_ip : "0.0.0.0",
            .server_port = config->server_udp_port ? config->server_udp_port : 8000,
            .local_port = config->local_udp_port ? config->local_udp_port : 8001,
            .buffer_size = 2048,
            .packet_size_ms = 20,  // 20ms packets
            .enable_compression = false
        };
        
        ret = udp_audio_init(&udp_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize UDP audio: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Initialize HTTP server if enabled
    if (config->enable_http_server) {
        howdytts_http_config_t http_config = {
            .port = config->local_http_port ? config->local_http_port : 80,
            .max_open_sockets = 7,
            .lru_purge_enable = true
        };
        
        strncpy(http_config.device_id, config->device_id, sizeof(http_config.device_id) - 1);
        strncpy(http_config.room, config->room, sizeof(http_config.room) - 1);
        
        ret = howdytts_http_server_init(&http_config, handle_http_state_change, handle_http_discovery);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize HTTP server: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Initialize mDNS service discovery if enabled
    if (config->enable_mdns_discovery) {
        ret = service_discovery_init(handle_server_discovered);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize service discovery: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    s_integration.is_initialized = true;
    
    ESP_LOGI(TAG, "HowdyTTS integration initialized successfully");
    ESP_LOGI(TAG, "Features - WebSocket: %s, UDP: %s, HTTP: %s, mDNS: %s",
             config->enable_websocket ? "ON" : "OFF",
             config->enable_udp_streaming ? "ON" : "OFF",
             config->enable_http_server ? "ON" : "OFF",
             config->enable_mdns_discovery ? "ON" : "OFF");
    
    return ESP_OK;
}

esp_err_t howdytts_integration_start(void)
{
    if (!s_integration.is_initialized) {
        ESP_LOGE(TAG, "HowdyTTS integration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_integration.is_running) {
        ESP_LOGI(TAG, "HowdyTTS integration already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting HowdyTTS integration");
    
    // Connect to WiFi
    esp_err_t ret = network_manager_connect(&s_integration.network_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int retry_count = 0;
    while (!network_manager_is_connected(&s_integration.network_manager) && retry_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }
    
    if (!network_manager_is_connected(&s_integration.network_manager)) {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    s_integration.status.wifi_connected = true;
    ESP_LOGI(TAG, "WiFi connected successfully");
    
    // Start HTTP server
    if (s_integration.config.enable_http_server) {
        ret = howdytts_http_server_start();
        if (ret == ESP_OK) {
            s_integration.status.http_server_running = true;
            
            // Register device via mDNS
            if (s_integration.config.enable_mdns_discovery) {
                howdytts_register_device();
            }
        }
    }
    
    // Start service discovery or use manual server
    if (s_integration.config.enable_mdns_discovery) {
        ESP_LOGI(TAG, "Starting mDNS service discovery...");
        ret = service_discovery_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start service discovery: %s", esp_err_to_name(ret));
        }
    } else if (s_integration.config.server_ip) {
        // Use manual server configuration
        ESP_LOGI(TAG, "Using manual server: %s", s_integration.config.server_ip);
        handle_server_discovered(s_integration.config.server_ip, 
                               s_integration.config.server_ws_port ? s_integration.config.server_ws_port : 8765);
    }
    
    s_integration.is_running = true;
    
    ESP_LOGI(TAG, "HowdyTTS integration started");
    return ESP_OK;
}

esp_err_t howdytts_integration_stop(void)
{
    if (!s_integration.is_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping HowdyTTS integration");
    
    // Stop UDP audio
    if (s_integration.config.enable_udp_streaming) {
        udp_audio_stop();
    }
    
    // Stop WebSocket
    if (s_integration.config.enable_websocket) {
        ws_client_stop();
    }
    
    // Stop HTTP server
    if (s_integration.config.enable_http_server) {
        howdytts_http_server_stop();
        if (s_integration.config.enable_mdns_discovery) {
            howdytts_unregister_device();
        }
    }
    
    // Stop service discovery
    if (s_integration.config.enable_mdns_discovery) {
        service_discovery_stop();
    }
    
    // Disconnect WiFi
    network_manager_disconnect(&s_integration.network_manager);
    
    s_integration.is_running = false;
    memset(&s_integration.status, 0, sizeof(s_integration.status));
    
    ESP_LOGI(TAG, "HowdyTTS integration stopped");
    return ESP_OK;
}

static void handle_server_discovered(const char *server_ip, uint16_t server_port)
{
    ESP_LOGI(TAG, "HowdyTTS server discovered: %s:%d", server_ip, server_port);
    
    // Store discovered server info
    strncpy(s_integration.discovered_server_ip, server_ip, sizeof(s_integration.discovered_server_ip) - 1);
    s_integration.discovered_ws_port = server_port;
    s_integration.discovered_udp_port = 8000;  // Standard UDP port
    
    s_integration.status.server_discovered = true;
    strncpy(s_integration.status.server_ip, server_ip, sizeof(s_integration.status.server_ip) - 1);
    s_integration.status.server_ws_port = server_port;
    s_integration.status.server_udp_port = 8000;
    
    // Connect WebSocket if enabled
    if (s_integration.config.enable_websocket) {
        char ws_uri[128];
        snprintf(ws_uri, sizeof(ws_uri), "ws://%s:%d/audio", server_ip, server_port);
        
        ESP_LOGI(TAG, "Connecting WebSocket to %s", ws_uri);
        esp_err_t ret = ws_client_set_uri(ws_uri);
        if (ret == ESP_OK) {
            ret = ws_client_start();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
            }
        }
    }
    
    // Setup UDP audio streaming if enabled
    if (s_integration.config.enable_udp_streaming) {
        setup_audio_streaming();
    }
    
    // Update UI
    va_ui_set_wifi_status(true, 85, server_ip);
}

static esp_err_t setup_audio_streaming(void)
{
    if (!s_integration.status.server_discovered) {
        ESP_LOGE(TAG, "No server discovered for audio streaming");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Update UDP server endpoint
    esp_err_t ret = udp_audio_set_server(s_integration.status.server_ip, 
                                        s_integration.status.server_udp_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UDP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start UDP streaming
    ret = udp_audio_start(NULL, NULL);  // No receive callback for now
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start UDP audio: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_integration.status.udp_ready = true;
    ESP_LOGI(TAG, "UDP audio streaming ready - Server: %s:%d",
             s_integration.status.server_ip, s_integration.status.server_udp_port);
    
    return ESP_OK;
}

static void handle_websocket_state(ws_client_state_t state, ws_message_type_t msg_type, 
                                  const uint8_t *data, size_t len)
{
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to HowdyTTS server");
            s_integration.status.websocket_connected = true;
            va_ui_set_wifi_status(true, 85, s_integration.status.server_ip);
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected from server");
            s_integration.status.websocket_connected = false;
            va_ui_set_wifi_status(false, 0, NULL);
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "WebSocket error occurred");
            s_integration.status.websocket_connected = false;
            break;
            
        default:
            break;
    }
    
    // Handle incoming messages
    if (state == WS_CLIENT_STATE_CONNECTED && msg_type == WS_MSG_TYPE_TTS_RESPONSE && data && len > 0) {
        ESP_LOGI(TAG, "Received WebSocket message: %.*s", (int)len, data);
        // Parse and handle HowdyTTS protocol messages
    }
}

static void handle_http_state_change(howdytts_state_t state, const char *text)
{
    ESP_LOGI(TAG, "HowdyTTS state change: %s", howdytts_state_to_string(state));
    
    // Update UI based on state
    switch (state) {
        case HOWDYTTS_STATE_WAITING:
            va_ui_set_state(VA_UI_STATE_IDLE, true);
            break;
            
        case HOWDYTTS_STATE_LISTENING:
            va_ui_set_state(VA_UI_STATE_LISTENING, true);
            break;
            
        case HOWDYTTS_STATE_THINKING:
            va_ui_set_state(VA_UI_STATE_PROCESSING, true);
            break;
            
        case HOWDYTTS_STATE_SPEAKING:
            va_ui_set_state(VA_UI_STATE_SPEAKING, true);
            if (text) {
                va_ui_show_message(text, 3000, 0xffffff);
            }
            break;
            
        case HOWDYTTS_STATE_ENDING:
            va_ui_set_state(VA_UI_STATE_IDLE, true);
            break;
    }
}

static void handle_http_discovery(const char *server_ip, uint16_t server_port)
{
    ESP_LOGI(TAG, "HowdyTTS server discovered via HTTP: %s:%d", server_ip, server_port);
    
    // Use the discovered server (same as mDNS discovery)
    handle_server_discovered(server_ip, server_port);
}

esp_err_t howdytts_integration_get_status(howdytts_connection_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *status = s_integration.status;
    return ESP_OK;
}

void howdytts_integration_handle_state(howdytts_state_t state, const char *text)
{
    handle_http_state_change(state, text);
}

esp_err_t howdytts_integration_update_audio_level(float level)
{
    if (s_integration.config.enable_http_server) {
        return howdytts_http_update_status(NULL, level, -1, -1);
    }
    return ESP_OK;
}

bool howdytts_integration_is_audio_ready(void)
{
    return s_integration.status.udp_ready || s_integration.status.websocket_connected;
}