#include "howdytts_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "HowdyTTSClient";

// Client state
static struct {
    howdytts_client_config_t config;
    howdytts_client_state_t state;
    howdytts_server_info_t current_server;
    howdytts_client_stats_t stats;
    SemaphoreHandle_t state_mutex;
    bool initialized;
    uint32_t last_ping_time;
    TaskHandle_t connection_task_handle;
} s_client = {0};

// Forward declarations
static void websocket_event_callback(ws_client_state_t ws_state, ws_message_type_t msg_type, 
                                    const uint8_t *data, size_t len);
static void service_discovered_callback(const howdytts_server_info_t *server_info);
static void connection_task(void *pvParameters);
static esp_err_t set_client_state(howdytts_client_state_t new_state);
static esp_err_t connect_to_server(const howdytts_server_info_t *server_info);

esp_err_t howdytts_client_init(const howdytts_client_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_client.initialized) {
        ESP_LOGI(TAG, "HowdyTTS client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing HowdyTTS client");
    ESP_LOGI(TAG, "Auto-discovery: %s", config->auto_discover ? "enabled" : "disabled");
    if (!config->auto_discover) {
        ESP_LOGI(TAG, "Manual server URI: %s", config->server_uri);
    }

    // Copy configuration
    s_client.config = *config;

    // Create state mutex
    s_client.state_mutex = xSemaphoreCreateMutex();
    if (!s_client.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize service discovery if auto-discovery is enabled
    if (s_client.config.auto_discover) {
        esp_err_t ret = service_discovery_init(service_discovered_callback);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize service discovery: %s", esp_err_to_name(ret));
            vSemaphoreDelete(s_client.state_mutex);
            return ret;
        }

        // Advertise this client on the network
        ret = service_discovery_advertise_client("HowdyTTS ESP32-P4", "display,audio,voice");
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to advertise client: %s", esp_err_to_name(ret));
            // Not critical, continue
        }
    }

    // Initialize WebSocket client configuration
    ws_client_config_t ws_config = {
        .server_uri = "",  // Will be set when server is discovered/configured
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 30,
        .keepalive_interval_sec = 5,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = 4096
    };

    // Use manual URI if auto-discovery is disabled
    if (!s_client.config.auto_discover) {
        snprintf(ws_config.server_uri, sizeof(ws_config.server_uri), "%s", s_client.config.server_uri);
    }

    esp_err_t ret = ws_client_init(&ws_config, websocket_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s", esp_err_to_name(ret));
        if (s_client.config.auto_discover) {
            service_discovery_stop_advertising();
        }
        vSemaphoreDelete(s_client.state_mutex);
        return ret;
    }

    // Initialize statistics
    memset(&s_client.stats, 0, sizeof(s_client.stats));
    
    set_client_state(HOWDYTTS_STATE_DISCONNECTED);
    s_client.initialized = true;

    ESP_LOGI(TAG, "HowdyTTS client initialized successfully");
    return ESP_OK;
}

esp_err_t howdytts_client_start(void)
{
    if (!s_client.initialized) {
        ESP_LOGE(TAG, "Client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting HowdyTTS client");

    // Create connection management task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        connection_task,
        "howdy_conn",
        4096,
        NULL,
        5,  // Medium priority
        &s_client.connection_task_handle,
        0   // Core 0 (with network tasks)
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create connection task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t howdytts_client_stop(void)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping HowdyTTS client");

    // Stop connection task
    if (s_client.connection_task_handle) {
        vTaskDelete(s_client.connection_task_handle);
        s_client.connection_task_handle = NULL;
    }

    // Stop WebSocket client
    ws_client_stop();

    // Stop service discovery
    if (s_client.config.auto_discover) {
        service_discovery_stop_scan();
        service_discovery_stop_advertising();
    }

    set_client_state(HOWDYTTS_STATE_DISCONNECTED);
    return ESP_OK;
}

esp_err_t howdytts_client_send_voice(const int16_t *audio_data, size_t samples, 
                                    bool voice_detected, float confidence)
{
    if (!s_client.initialized || !audio_data || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_client.state != HOWDYTTS_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ws_client_send_audio(audio_data, samples, 16000);  // 16kHz sample rate
    if (ret == ESP_OK) {
        s_client.stats.audio_messages_sent++;
    }

    return ret;
}

esp_err_t howdytts_client_send_text(const char *text)
{
    if (!s_client.initialized || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_client.state != HOWDYTTS_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    return ws_client_send_text(text);
}

howdytts_client_state_t howdytts_client_get_state(void)
{
    return s_client.state;
}

esp_err_t howdytts_client_get_stats(howdytts_client_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_client.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *stats = s_client.stats;
        
        // Get WebSocket statistics
        uint32_t ws_bytes_sent, ws_bytes_received, ws_reconnect_count;
        if (ws_client_get_stats(&ws_bytes_sent, &ws_bytes_received, &ws_reconnect_count) == ESP_OK) {
            stats->total_bytes_sent = ws_bytes_sent;
            stats->total_bytes_received = ws_bytes_received;
        }
        
        xSemaphoreGive(s_client.state_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t howdytts_client_get_server_info(howdytts_server_info_t *server_info)
{
    if (!server_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_client.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *server_info = s_client.current_server;
        xSemaphoreGive(s_client.state_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t howdytts_client_reconnect(void)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Forcing reconnection to HowdyTTS server");

    // Stop current connection
    ws_client_stop();
    
    // Reset state to trigger reconnection
    set_client_state(HOWDYTTS_STATE_DISCONNECTED);
    
    return ESP_OK;
}

static esp_err_t set_client_state(howdytts_client_state_t new_state)
{
    if (xSemaphoreTake(s_client.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        howdytts_client_state_t old_state = s_client.state;
        s_client.state = new_state;
        xSemaphoreGive(s_client.state_mutex);

        if (old_state != new_state) {
            ESP_LOGI(TAG, "HowdyTTS client state changed: %d -> %d", old_state, new_state);

            // Update UI based on state
            switch (new_state) {
                case HOWDYTTS_STATE_DISCONNECTED:
                    va_ui_set_state(VA_UI_STATE_CONNECTING, true);
                    va_ui_show_message("Searching for HowdyTTS server...", 0, 0xfbbc04);
                    break;
                case HOWDYTTS_STATE_DISCOVERING:
                    va_ui_set_state(VA_UI_STATE_CONNECTING, true);
                    va_ui_show_message("Discovering servers...", 0, 0xfbbc04);
                    break;
                case HOWDYTTS_STATE_CONNECTING:
                    va_ui_set_state(VA_UI_STATE_CONNECTING, true);
                    va_ui_show_message("Connecting to server...", 0, 0xfbbc04);
                    break;
                case HOWDYTTS_STATE_CONNECTED:
                    va_ui_set_state(VA_UI_STATE_IDLE, true);
                    va_ui_show_message("Connected to HowdyTTS", 3000, 0x34a853);
                    va_ui_set_wifi_status(true, 90, s_client.current_server.hostname);
                    break;
                case HOWDYTTS_STATE_ERROR:
                    va_ui_set_state(VA_UI_STATE_ERROR, true);
                    va_ui_show_message("Connection failed", 0, 0xea4335);
                    va_ui_set_wifi_status(false, 0, NULL);
                    break;
            }
        }
        
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

static void websocket_event_callback(ws_client_state_t ws_state, ws_message_type_t msg_type, 
                                    const uint8_t *data, size_t len)
{
    switch (ws_state) {
        case WS_CLIENT_STATE_CONNECTED:
            set_client_state(HOWDYTTS_STATE_CONNECTED);
            s_client.stats.successful_connections++;
            ESP_LOGI(TAG, "Successfully connected to HowdyTTS server");
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            if (s_client.state == HOWDYTTS_STATE_CONNECTED) {
                ESP_LOGI(TAG, "Disconnected from HowdyTTS server");
                set_client_state(HOWDYTTS_STATE_DISCONNECTED);
            }
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "WebSocket error occurred");
            set_client_state(HOWDYTTS_STATE_ERROR);
            break;
            
        default:
            break;
    }

    // Handle incoming messages
    if (msg_type == WS_MSG_TYPE_TTS_RESPONSE && data && len > 0) {
        ESP_LOGI(TAG, "Received TTS audio response: %zu bytes", len);
        s_client.stats.tts_responses_received++;
        
        // TODO: Play TTS audio through speaker
        // For now, just update UI to show speaking state
        va_ui_set_state(VA_UI_STATE_SPEAKING, true);
        
        // Simulate TTS playback duration
        vTaskDelay(pdMS_TO_TICKS(2000));
        va_ui_set_state(VA_UI_STATE_IDLE, true);
    }
}

static void service_discovered_callback(const howdytts_server_info_t *server_info)
{
    if (!server_info) {
        return;
    }

    ESP_LOGI(TAG, "HowdyTTS server discovered: %s (%s:%d)", 
             server_info->hostname, server_info->ip_addr, server_info->port);

    // Test server connectivity
    uint32_t latency_ms = 0;
    esp_err_t ret = service_discovery_test_server(server_info, 3000, &latency_ms);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Server connectivity test passed - latency: %lu ms", latency_ms);
        
        // Use this server if we don't have one or if it's better
        if (s_client.state == HOWDYTTS_STATE_DISCOVERING || 
            s_client.state == HOWDYTTS_STATE_DISCONNECTED) {
            
            s_client.current_server = *server_info;
            s_client.stats.current_latency_ms = latency_ms;
            
            // Trigger connection
            connect_to_server(server_info);
        }
    } else {
        ESP_LOGW(TAG, "Server connectivity test failed: %s", esp_err_to_name(ret));
    }
}

static esp_err_t connect_to_server(const howdytts_server_info_t *server_info)
{
    if (!server_info) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Connecting to HowdyTTS server: %s:%d", server_info->ip_addr, server_info->port);
    
    set_client_state(HOWDYTTS_STATE_CONNECTING);
    s_client.stats.connection_attempts++;

    // Build WebSocket URI
    char ws_uri[256];
    const char *protocol = server_info->secure ? "wss" : "ws";
    snprintf(ws_uri, sizeof(ws_uri), "%s://%s:%d/howdytts", 
             protocol, server_info->ip_addr, server_info->port);

    // Update WebSocket client configuration
    ws_client_config_t ws_config = {
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 30,
        .keepalive_interval_sec = 5,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = 4096
    };
    snprintf(ws_config.server_uri, sizeof(ws_config.server_uri), "%s", ws_uri);

    // Re-initialize WebSocket client with new URI
    ws_client_stop();
    esp_err_t ret = ws_client_init(&ws_config, websocket_event_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-initialize WebSocket client: %s", esp_err_to_name(ret));
        set_client_state(HOWDYTTS_STATE_ERROR);
        return ret;
    }

    // Start connection
    ret = ws_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket connection: %s", esp_err_to_name(ret));
        set_client_state(HOWDYTTS_STATE_ERROR);
        return ret;
    }

    return ESP_OK;
}

static void connection_task(void *pvParameters)
{
    ESP_LOGI(TAG, "HowdyTTS connection management task started");

    while (s_client.initialized) {
        switch (s_client.state) {
            case HOWDYTTS_STATE_DISCONNECTED:
                if (s_client.config.auto_discover) {
                    ESP_LOGI(TAG, "Starting server discovery");
                    set_client_state(HOWDYTTS_STATE_DISCOVERING);
                    
                    esp_err_t ret = service_discovery_start_scan(s_client.config.discovery_timeout_ms);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start server discovery: %s", esp_err_to_name(ret));
                        set_client_state(HOWDYTTS_STATE_ERROR);
                    }
                } else {
                    // Use manual server URI
                    ESP_LOGI(TAG, "Using manual server configuration");
                    howdytts_server_info_t manual_server = {0};
                    strncpy(manual_server.hostname, "manual-server", sizeof(manual_server.hostname) - 1);
                    // Parse URI to extract IP and port
                    // For now, assume direct IP:port format
                    manual_server.port = 8080;  // Default port
                    
                    connect_to_server(&manual_server);
                }
                break;
                
            case HOWDYTTS_STATE_DISCOVERING:
                // Wait for service discovery to find servers
                vTaskDelay(pdMS_TO_TICKS(1000));
                
                // Check if discovery timeout reached
                static uint32_t discovery_start_time = 0;
                if (discovery_start_time == 0) {
                    discovery_start_time = esp_timer_get_time() / 1000;
                }
                
                uint32_t elapsed = (esp_timer_get_time() / 1000) - discovery_start_time;
                if (elapsed > s_client.config.discovery_timeout_ms) {
                    ESP_LOGW(TAG, "Server discovery timeout");
                    service_discovery_stop_scan();
                    set_client_state(HOWDYTTS_STATE_ERROR);
                    discovery_start_time = 0;
                }
                break;
                
            case HOWDYTTS_STATE_CONNECTING:
            case HOWDYTTS_STATE_CONNECTED:
                // Periodically send ping to maintain connection
                if (s_client.state == HOWDYTTS_STATE_CONNECTED) {
                    uint32_t now = esp_timer_get_time() / 1000;
                    if (now - s_client.last_ping_time > 30000) {  // Ping every 30 seconds
                        ws_client_ping();
                        s_client.last_ping_time = now;
                    }
                }
                
                vTaskDelay(pdMS_TO_TICKS(5000));  // Check every 5 seconds
                break;
                
            case HOWDYTTS_STATE_ERROR:
                ESP_LOGI(TAG, "In error state, retrying connection in 10 seconds");
                vTaskDelay(pdMS_TO_TICKS(10000));
                set_client_state(HOWDYTTS_STATE_DISCONNECTED);
                break;
        }
    }

    ESP_LOGI(TAG, "HowdyTTS connection management task ended");
    s_client.connection_task_handle = NULL;
    vTaskDelete(NULL);
}