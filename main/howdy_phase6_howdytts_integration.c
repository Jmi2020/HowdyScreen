/**
 * @file howdy_phase6_howdytts_integration.c
 * @brief Phase 6A: HowdyTTS Native Integration Application
 * 
 * This application demonstrates the complete HowdyTTS native protocol integration
 * with UDP discovery, PCM audio streaming, and HTTP state management.
 * 
 * Features:
 * - Native HowdyTTS protocol support (UDP discovery, audio streaming, HTTP state)
 * - Raw PCM audio streaming for minimal latency and memory usage
 * - Automatic server discovery and connection
 * - Real-time UI updates based on voice assistant states
 * - Touch-to-talk interface with visual feedback
 * - ESP32-P4 optimized with <10KB audio streaming overhead
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
#include "esp_wifi.h"

// BSP includes for display initialization
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "lvgl.h"

// Component includes
#include "howdytts_network_integration.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "audio_processor.h"

static const char *TAG = "HowdyPhase6";

// Global state
typedef struct {
    bool wifi_connected;
    bool howdytts_connected;
    bool discovery_completed;
    howdytts_server_info_t selected_server;
    uint32_t audio_packets_sent;
    float current_audio_level;
} app_state_t;

static app_state_t s_app_state = {0};

// HowdyTTS integration callbacks
static esp_err_t howdytts_audio_callback(const int16_t *audio_data, size_t samples, void *user_data)
{
    ESP_LOGD(TAG, "Audio callback: streaming %d samples to HowdyTTS server", (int)samples);
    
    // Stream audio to HowdyTTS server
    esp_err_t ret = howdytts_stream_audio(audio_data, samples);
    if (ret == ESP_OK) {
        s_app_state.audio_packets_sent++;
        
        // Calculate audio level for UI feedback
        float level = 0.0f;
        for (size_t i = 0; i < samples; i++) {
            level += (float)(abs(audio_data[i])) / 32768.0f;
        }
        s_app_state.current_audio_level = level / samples;
        
        // Update UI with audio level
        ui_manager_update_audio_level((int)(s_app_state.current_audio_level * 100));
        
        // Update device status
        int signal_strength = -45; // Placeholder WiFi RSSI
        howdytts_update_device_status(s_app_state.current_audio_level, -1, signal_strength);
    }
    
    return ret;
}

static esp_err_t howdytts_tts_callback(const int16_t *tts_audio, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "TTS callback: received %d samples from HowdyTTS server", (int)samples);
    
    // Play TTS audio through speaker
    // TODO: Integrate with audio processor for speaker output
    
    return ESP_OK;
}

static void howdytts_event_callback(const howdytts_event_data_t *event, void *user_data)
{
    switch (event->event_type) {
        case HOWDYTTS_EVENT_DISCOVERY_STARTED:
            ESP_LOGI(TAG, "🔍 HowdyTTS discovery started");
            ui_manager_update_status("Discovering HowdyTTS servers...");
            break;
            
        case HOWDYTTS_EVENT_SERVER_DISCOVERED:
            ESP_LOGI(TAG, "🎯 Discovered HowdyTTS server: %s (%s)", 
                    event->data.server_info.hostname, 
                    event->data.server_info.ip_address);
            
            // Auto-select first discovered server
            if (!s_app_state.howdytts_connected) {
                s_app_state.selected_server = event->data.server_info;
                
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Found %s - connecting...", 
                        event->data.server_info.hostname);
                ui_manager_update_status(status_msg);
                
                // Connect to server
                howdytts_connect_to_server(&s_app_state.selected_server);
            }
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_ESTABLISHED:
            ESP_LOGI(TAG, "✅ Connected to HowdyTTS server");
            s_app_state.howdytts_connected = true;
            ui_manager_update_status("Connected to HowdyTTS");
            ui_manager_set_state(UI_STATE_IDLE);
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "❌ Lost connection to HowdyTTS server");
            s_app_state.howdytts_connected = false;
            ui_manager_update_status("Connection lost - reconnecting...");
            ui_manager_set_state(UI_STATE_ERROR);
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STARTED:
            ESP_LOGI(TAG, "🎵 Audio streaming started");
            ui_manager_set_state(UI_STATE_LISTENING);
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STOPPED:
            ESP_LOGI(TAG, "🔇 Audio streaming stopped");
            ui_manager_set_state(UI_STATE_IDLE);
            break;
            
        case HOWDYTTS_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ HowdyTTS error: %s", event->message);
            ui_manager_update_status("HowdyTTS Error");
            ui_manager_set_state(UI_STATE_ERROR);
            break;
            
        default:
            ESP_LOGD(TAG, "HowdyTTS event: %s", event->message);
            break;
    }
}

static void howdytts_va_state_callback(howdytts_va_state_t va_state, const char *state_text, void *user_data)
{
    ESP_LOGI(TAG, "🗣️ Voice assistant state changed: %s", 
            va_state == HOWDYTTS_VA_STATE_WAITING ? "waiting" :
            va_state == HOWDYTTS_VA_STATE_LISTENING ? "listening" :
            va_state == HOWDYTTS_VA_STATE_THINKING ? "thinking" :
            va_state == HOWDYTTS_VA_STATE_SPEAKING ? "speaking" : "ending");
    
    // Map HowdyTTS states to UI states
    switch (va_state) {
        case HOWDYTTS_VA_STATE_WAITING:
            ui_manager_set_state(UI_STATE_IDLE);
            ui_manager_update_status("Tap to speak");
            break;
            
        case HOWDYTTS_VA_STATE_LISTENING:
            ui_manager_set_state(UI_STATE_LISTENING);
            ui_manager_update_status("Listening...");
            break;
            
        case HOWDYTTS_VA_STATE_THINKING:
            ui_manager_set_state(UI_STATE_PROCESSING);
            ui_manager_update_status("Processing...");
            break;
            
        case HOWDYTTS_VA_STATE_SPEAKING:
            ui_manager_set_state(UI_STATE_SPEAKING);
            if (state_text) {
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Speaking: %.50s%s", 
                        state_text, strlen(state_text) > 50 ? "..." : "");
                ui_manager_update_status(status_msg);
            } else {
                ui_manager_update_status("Speaking...");
            }
            break;
            
        case HOWDYTTS_VA_STATE_ENDING:
            ui_manager_set_state(UI_STATE_IDLE);
            ui_manager_update_status("Conversation ended");
            break;
    }
}

// Voice activation callback from UI
static void voice_activation_callback(bool start_voice)
{
    if (start_voice) {
        ESP_LOGI(TAG, "🎤 Voice activation triggered by touch");
        
        if (s_app_state.howdytts_connected) {
            // Start audio streaming to HowdyTTS server
            howdytts_start_audio_streaming();
            ui_manager_set_state(UI_STATE_LISTENING);
        } else {
            ESP_LOGW(TAG, "Cannot start voice capture - not connected to HowdyTTS server");
            ui_manager_update_status("Not connected to server");
        }
    } else {
        ESP_LOGI(TAG, "🔇 Voice activation ended");
        howdytts_stop_audio_streaming();
    }
}

// WiFi connection monitoring task
static void wifi_monitor_task(void *pvParameters)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
        
        bool wifi_connected = wifi_manager_is_connected();
        
        if (wifi_connected != s_app_state.wifi_connected) {
            s_app_state.wifi_connected = wifi_connected;
            
            if (wifi_connected) {
                ESP_LOGI(TAG, "📶 WiFi connected successfully");
                ui_manager_set_wifi_strength(wifi_manager_get_signal_strength());
                ui_manager_update_status("WiFi connected - starting discovery...");
                
                // Start HowdyTTS server discovery
                if (!s_app_state.discovery_completed) {
                    howdytts_discovery_start(15000); // 15 second timeout
                    s_app_state.discovery_completed = true;
                }
            } else {
                ESP_LOGW(TAG, "📶 WiFi disconnected");
                s_app_state.howdytts_connected = false;
                ui_manager_set_wifi_strength(0);
                ui_manager_update_status("WiFi disconnected");
                ui_manager_set_state(UI_STATE_ERROR);
            }
        }
        
        // Update signal strength if connected
        if (wifi_connected) {
            ui_manager_set_wifi_strength(wifi_manager_get_signal_strength());
        }
    }
}

// System initialization
static esp_err_t system_init(void)
{
    ESP_LOGI(TAG, "🚀 Initializing HowdyTTS Phase 6 Application");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Note: WiFi events are handled by the WiFi manager component
    
    return ESP_OK;
}

static esp_err_t howdytts_integration_init_app(void)
{
    ESP_LOGI(TAG, "🔧 Initializing HowdyTTS integration");
    
    // Configure HowdyTTS integration
    howdytts_integration_config_t howdytts_config = {
        .device_id = "esp32p4-howdyscreen-001",
        .device_name = "Office HowdyScreen",
        .room = "office",
        .protocol_mode = HOWDYTTS_PROTOCOL_UDP_ONLY, // Start with UDP only
        .audio_format = HOWDYTTS_AUDIO_PCM_16,       // Raw PCM streaming
        .sample_rate = 16000,                        // 16kHz audio
        .frame_size = 320,                          // 20ms frames
        .enable_audio_stats = true,                 // Performance monitoring
        .enable_fallback = false,                   // No WebSocket fallback for now
        .discovery_timeout_ms = 15000,              // 15 second discovery
        .connection_retry_count = 3                 // 3 retry attempts
    };
    
    // Set up callbacks
    howdytts_integration_callbacks_t howdytts_callbacks = {
        .audio_callback = howdytts_audio_callback,
        .tts_callback = howdytts_tts_callback,
        .event_callback = howdytts_event_callback,
        .va_state_callback = howdytts_va_state_callback,
        .user_data = NULL
    };
    
    // Initialize HowdyTTS integration
    esp_err_t ret = howdytts_integration_init(&howdytts_config, &howdytts_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HowdyTTS integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ HowdyTTS integration initialized successfully");
    return ESP_OK;
}

// Statistics reporting task
static void stats_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (true) {
        // Update every 5 seconds
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(5000));
        
        if (s_app_state.howdytts_connected) {
            // Get audio statistics
            howdytts_audio_stats_t stats;
            if (howdytts_get_audio_stats(&stats) == ESP_OK) {
                ESP_LOGI(TAG, "📊 Audio Stats - Packets sent: %d, Loss rate: %.2f%%, Latency: %.1fms",
                        (int)stats.packets_sent, stats.packet_loss_rate * 100, stats.average_latency_ms);
            }
            
            // System health
            ESP_LOGI(TAG, "💾 System Health - Free heap: %d bytes, Min free: %d bytes",
                    (int)esp_get_free_heap_size(), (int)esp_get_minimum_free_heap_size());
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🎉 HowdyTTS Phase 6 - Native Protocol Integration");
    ESP_LOGI(TAG, "ESP32-P4 HowdyScreen with PCM Audio Streaming");
    
    // System initialization
    ESP_ERROR_CHECK(system_init());
    
    // Initialize BSP (Board Support Package) and display
    ESP_LOGI(TAG, "🔧 Initializing BSP and display...");
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "❌ BSP display initialization failed");
        return;
    }
    ESP_LOGI(TAG, "✅ BSP display initialized successfully");
    
    // Turn on display backlight
    ESP_LOGI(TAG, "💡 Turning on display backlight...");
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_LOGI(TAG, "✅ Display backlight enabled");
    
    // Initialize UI Manager
    ESP_LOGI(TAG, "🖥️ Initializing UI Manager");
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_set_voice_callback(voice_activation_callback);
    ui_manager_update_status("Initializing HowdyTTS...");
    
    // Initialize HowdyTTS integration
    ESP_ERROR_CHECK(howdytts_integration_init_app());
    
    // Initialize WiFi
    ESP_LOGI(TAG, "📶 Initializing WiFi");
    ESP_ERROR_CHECK(wifi_manager_init(NULL));
    
    ui_manager_update_status("Connecting to WiFi...");
    esp_err_t wifi_result = wifi_manager_auto_connect();
    if (wifi_result != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ WiFi auto-connect failed: %s", esp_err_to_name(wifi_result));
        ui_manager_update_status("WiFi connection failed - will retry");
    }
    
    // Start monitoring tasks
    xTaskCreate(stats_task, "stats_task", 3072, NULL, 2, NULL);
    xTaskCreate(wifi_monitor_task, "wifi_monitor", 2048, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "🎯 Phase 6 initialization complete!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== HowdyTTS Integration Ready ===");
    ESP_LOGI(TAG, "Protocol: Native UDP (PCM streaming)");
    ESP_LOGI(TAG, "Device: %s", "esp32p4-howdyscreen-001");
    ESP_LOGI(TAG, "Audio: 16kHz/16-bit PCM, 20ms frames");
    ESP_LOGI(TAG, "Memory: <10KB audio streaming overhead");
    ESP_LOGI(TAG, "UI: Touch-to-talk with visual feedback");
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "");
    
    // Main application loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Periodic health checks
        if (s_app_state.wifi_connected && !s_app_state.howdytts_connected) {
            // Try to reconnect to HowdyTTS if WiFi is up but HowdyTTS is down
            static uint32_t last_reconnect_attempt = 0;
            uint32_t now = esp_timer_get_time() / 1000;
            
            if (now - last_reconnect_attempt > 30000) { // Try every 30 seconds
                ESP_LOGI(TAG, "🔄 Attempting to reconnect to HowdyTTS servers");
                howdytts_discovery_start(10000);
                last_reconnect_attempt = now;
            }
        }
    }
}