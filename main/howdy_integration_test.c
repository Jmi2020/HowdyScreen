/**
 * @file howdy_integration_test.c
 * @brief HowdyTTS Integration Test Application
 * 
 * Simple test application to validate HowdyTTS integration components:
 * - UDP discovery protocol
 * - HTTP state server
 * - Basic connectivity and callback functionality
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "howdytts_network_integration.h"
#include "wifi_manager.h"

static const char *TAG = "HowdyTest";

// Test state
static struct {
    bool wifi_connected;
    int servers_discovered;
    bool test_completed;
} test_state = {0};

// Test callbacks
static esp_err_t test_audio_callback(const int16_t *audio_data, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "✅ Audio callback triggered with %d samples", (int)samples);
    return ESP_OK;
}

static esp_err_t test_tts_callback(const int16_t *tts_audio, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "✅ TTS callback triggered with %d samples", (int)samples);
    return ESP_OK;
}

static void test_event_callback(const howdytts_event_data_t *event, void *user_data)
{
    switch (event->event_type) {
        case HOWDYTTS_EVENT_DISCOVERY_STARTED:
            ESP_LOGI(TAG, "🔍 Test: Discovery started - %s", event->message);
            break;
            
        case HOWDYTTS_EVENT_SERVER_DISCOVERED:
            test_state.servers_discovered++;
            ESP_LOGI(TAG, "🎯 Test: Server discovered #%d - %s (%s)", 
                    test_state.servers_discovered,
                    event->data.server_info.hostname, 
                    event->data.server_info.ip_address);
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_ESTABLISHED:
            ESP_LOGI(TAG, "✅ Test: Connection established");
            break;
            
        case HOWDYTTS_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ Test: Error - %s", event->message);
            break;
            
        default:
            ESP_LOGD(TAG, "Test event: %s", event->message);
            break;
    }
}

static void test_va_state_callback(howdytts_va_state_t va_state, const char *state_text, void *user_data)
{
    const char *state_name = 
        va_state == HOWDYTTS_VA_STATE_WAITING ? "waiting" :
        va_state == HOWDYTTS_VA_STATE_LISTENING ? "listening" :
        va_state == HOWDYTTS_VA_STATE_THINKING ? "thinking" :
        va_state == HOWDYTTS_VA_STATE_SPEAKING ? "speaking" : "ending";
        
    ESP_LOGI(TAG, "🗣️ Test: VA state changed to %s%s%s", 
            state_name,
            state_text ? " - " : "",
            state_text ? state_text : "");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "📶 WiFi connected - starting HowdyTTS integration test");
        test_state.wifi_connected = true;
        
        // Start discovery test after WiFi connection
        vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for IP assignment
        
        ESP_LOGI(TAG, "🧪 Starting HowdyTTS discovery test...");
        howdytts_discovery_start(15000); // 15 second discovery
        
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "📶 WiFi disconnected");
        test_state.wifi_connected = false;
    }
}

// Test task to demonstrate API functionality
static void test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🧪 HowdyTTS Integration Test Task Started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (!test_state.test_completed) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10000)); // Every 10 seconds
        
        if (test_state.wifi_connected) {
            // Test getting discovered servers
            howdytts_server_info_t servers[8];
            size_t server_count = 0;
            
            esp_err_t ret = howdytts_get_discovered_servers(servers, 8, &server_count);
            if (ret == ESP_OK && server_count > 0) {
                ESP_LOGI(TAG, "📋 Test: Found %d servers in list:", (int)server_count);
                for (size_t i = 0; i < server_count; i++) {
                    ESP_LOGI(TAG, "  - %s (%s) - last seen %d ms ago", 
                            servers[i].hostname, servers[i].ip_address,
                            (int)(esp_timer_get_time() / 1000 - servers[i].last_seen));
                }
            } else {
                ESP_LOGI(TAG, "📋 Test: No servers in discovered list yet");
            }
            
            // Test connection state
            howdytts_connection_state_t conn_state = howdytts_get_connection_state();
            howdytts_va_state_t va_state = howdytts_get_va_state();
            
            ESP_LOGI(TAG, "📊 Test: Connection=%s, VA State=%s", 
                    conn_state == HOWDYTTS_STATE_DISCONNECTED ? "DISCONNECTED" :
                    conn_state == HOWDYTTS_STATE_DISCOVERING ? "DISCOVERING" :
                    conn_state == HOWDYTTS_STATE_CONNECTING ? "CONNECTING" :
                    conn_state == HOWDYTTS_STATE_CONNECTED ? "CONNECTED" :
                    conn_state == HOWDYTTS_STATE_STREAMING ? "STREAMING" : "ERROR",
                    
                    va_state == HOWDYTTS_VA_STATE_WAITING ? "waiting" :
                    va_state == HOWDYTTS_VA_STATE_LISTENING ? "listening" :
                    va_state == HOWDYTTS_VA_STATE_THINKING ? "thinking" :
                    va_state == HOWDYTTS_VA_STATE_SPEAKING ? "speaking" : "ending");
            
            // Test audio statistics
            howdytts_audio_stats_t stats;
            if (howdytts_get_audio_stats(&stats) == ESP_OK) {
                ESP_LOGI(TAG, "📈 Test: Audio stats - Packets sent: %d, Loss: %.2f%%", 
                        (int)stats.packets_sent, stats.packet_loss_rate * 100);
            }
        }
    }
    
    ESP_LOGI(TAG, "🧪 Test task completed");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "🧪 HowdyTTS Integration Test Application");
    ESP_LOGI(TAG, "Testing UDP discovery, HTTP state server, and basic connectivity");
    ESP_LOGI(TAG, "");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize network
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Register WiFi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                              &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                              &wifi_event_handler, NULL));
    
    // Configure HowdyTTS integration
    howdytts_integration_config_t config = {
        .device_id = "esp32p4-test-device",
        .device_name = "Test HowdyScreen",
        .room = "test-room",
        .protocol_mode = HOWDYTTS_PROTOCOL_UDP_ONLY,
        .audio_format = HOWDYTTS_AUDIO_PCM_16,
        .sample_rate = 16000,
        .frame_size = 320,
        .enable_audio_stats = true,
        .enable_fallback = false,
        .discovery_timeout_ms = 15000,
        .connection_retry_count = 3
    };
    
    // Set up callbacks
    howdytts_integration_callbacks_t callbacks = {
        .audio_callback = test_audio_callback,
        .tts_callback = test_tts_callback,
        .event_callback = test_event_callback,
        .va_state_callback = test_va_state_callback,
        .user_data = NULL
    };
    
    // Initialize HowdyTTS integration
    ESP_LOGI(TAG, "🔧 Initializing HowdyTTS integration...");
    ret = howdytts_integration_init(&config, &callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to initialize HowdyTTS integration: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✅ HowdyTTS integration initialized");
    
    // Initialize WiFi
    ESP_LOGI(TAG, "📶 Initializing WiFi...");
    ESP_ERROR_CHECK(wifi_manager_init(NULL));
    ESP_ERROR_CHECK(wifi_manager_auto_connect());
    
    // Start test task
    xTaskCreate(test_task, "test_task", 4096, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== Test Configuration ===");
    ESP_LOGI(TAG, "Device ID: %s", config.device_id);
    ESP_LOGI(TAG, "Room: %s", config.room);
    ESP_LOGI(TAG, "Protocol: UDP only");
    ESP_LOGI(TAG, "Audio: 16kHz/16-bit PCM");
    ESP_LOGI(TAG, "Discovery timeout: %d ms", (int)config.discovery_timeout_ms);
    ESP_LOGI(TAG, "=========================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎯 Test will run automatically after WiFi connection");
    ESP_LOGI(TAG, "📡 HTTP state server available on port 8080:");
    ESP_LOGI(TAG, "   GET  http://<device-ip>:8080/status");
    ESP_LOGI(TAG, "   GET  http://<device-ip>:8080/health");
    ESP_LOGI(TAG, "   POST http://<device-ip>:8080/state");
    ESP_LOGI(TAG, "   POST http://<device-ip>:8080/speak");
    ESP_LOGI(TAG, "");
    
    // Main loop - just log periodic status
    uint32_t loop_count = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds
        loop_count++;
        
        ESP_LOGI(TAG, "🔄 Test running for %d minutes - WiFi: %s, Servers found: %d", 
                loop_count / 2, 
                test_state.wifi_connected ? "connected" : "disconnected",
                test_state.servers_discovered);
                
        // System health check
        ESP_LOGI(TAG, "💾 Free heap: %d bytes, Min free: %d bytes",
                (int)esp_get_free_heap_size(), (int)esp_get_minimum_free_heap_size());
    }
}