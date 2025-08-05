#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_event.h"

// BSP and display components
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "lvgl.h"

// HowdyScreen WiFi integration
#include "howdy_wifi_integration.h"
#include "wifi_provisioning.h"

static const char *TAG = "HowdyPhase2";

// Global handles
static bool system_ready = false;
static bool wifi_connected = false;

// Function prototypes
static void system_init(void);
static void wifi_integration_event_handler(wifi_integration_event_t event, void *data, void *user_data);
static void system_monitor_task(void *pvParameters);
static void lvgl_tick_task(void *pvParameters);

static void system_init(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen Phase 2 System Initialization ===");
    
    // Initialize event loop (required for WiFi)
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize BSP (Board Support Package)
    ESP_LOGI(TAG, "Initializing BSP for ESP32-P4 WiFi6 Touch LCD");
    ESP_ERROR_CHECK(bsp_init());
    
    // Initialize display
    ESP_LOGI(TAG, "Initializing 800x800 MIPI-DSI display");
    bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = CONFIG_BSP_LCD_DRAW_BUF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
        }
    };
    bsp_display_start_with_config(&display_cfg);
    
    // Initialize touch controller
    ESP_LOGI(TAG, "Initializing GT911 touch controller");
    bsp_touch_start();
    
    ESP_LOGI(TAG, "Display and touch initialization complete");
    system_ready = true;
}

static void wifi_integration_event_handler(wifi_integration_event_t event, void *data, void *user_data)
{
    ESP_LOGI(TAG, "WiFi Integration Event: %d", event);
    
    switch (event) {
        case WIFI_INTEGRATION_EVENT_INIT_DONE:
            ESP_LOGI(TAG, "✅ WiFi integration initialized");
            break;
            
        case WIFI_INTEGRATION_EVENT_CONNECTED:
            {
                wifi_connection_info_t *info = (wifi_connection_info_t*)data;
                if (info) {
                    ESP_LOGI(TAG, "🌐 WiFi connected successfully!");
                    ESP_LOGI(TAG, "   SSID: %s", info->connected_ssid);
                    ESP_LOGI(TAG, "   IP: %s", info->ip_address);
                    ESP_LOGI(TAG, "   Gateway: %s", info->gateway);
                    ESP_LOGI(TAG, "   Signal: %d dBm", info->rssi);
                    wifi_connected = true;
                }
            }
            break;
            
        case WIFI_INTEGRATION_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "📶 WiFi disconnected");
            wifi_connected = false;
            break;
            
        case WIFI_INTEGRATION_EVENT_CONNECTION_FAILED:
            ESP_LOGE(TAG, "❌ WiFi connection failed");
            break;
            
        case WIFI_INTEGRATION_EVENT_AP_MODE_STARTED:
            ESP_LOGI(TAG, "📡 AP mode started for configuration");
            break;
            
        case WIFI_INTEGRATION_EVENT_UI_SHOWN:
            ESP_LOGI(TAG, "📱 WiFi configuration UI shown");
            break;
            
        case WIFI_INTEGRATION_EVENT_UI_HIDDEN:
            ESP_LOGI(TAG, "📱 WiFi configuration UI hidden");
            break;
            
        default:
            ESP_LOGD(TAG, "Unhandled WiFi event: %d", event);
            break;
    }
}

static void system_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "System monitor task started");
    
    int counter = 0;
    
    while (1) {
        counter++;
        
        // System status every 10 seconds
        if (counter % 10 == 0) {
            ESP_LOGI(TAG, "=== System Status (t+%ds) ===", counter);
            ESP_LOGI(TAG, "System Ready: %s", system_ready ? "✅" : "❌");
            ESP_LOGI(TAG, "WiFi Connected: %s", wifi_connected ? "✅" : "❌");
            ESP_LOGI(TAG, "Free Heap: %lu bytes", esp_get_free_heap_size());
            
            if (wifi_connected) {
                wifi_connection_info_t conn_info;
                if (howdy_wifi_integration_get_connection_info(&conn_info) == ESP_OK) {
                    ESP_LOGI(TAG, "WiFi Status: %s (IP: %s, RSSI: %d dBm)", 
                             conn_info.connected_ssid, conn_info.ip_address, conn_info.rssi);
                }
            }
            
            wifi_integration_state_t wifi_state = howdy_wifi_integration_get_state();
            const char* state_names[] = {
                "INIT", "SETUP_REQUIRED", "CONNECTING", "CONNECTED", 
                "DISCONNECTED", "AP_MODE", "ERROR"
            };
            ESP_LOGI(TAG, "WiFi State: %s", 
                     (wifi_state < 7) ? state_names[wifi_state] : "UNKNOWN");
        }
        
        // Auto-show WiFi UI if setup is required (every 30 seconds)
        if (!wifi_connected && counter % 30 == 15) {
            wifi_integration_state_t state = howdy_wifi_integration_get_state();
            if (state == WIFI_INTEGRATION_STATE_SETUP_REQUIRED || 
                state == WIFI_INTEGRATION_STATE_ERROR) {
                ESP_LOGI(TAG, "📱 Auto-showing WiFi configuration UI");
                howdy_wifi_integration_show_ui();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Update every second
    }
}

static void lvgl_tick_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LVGL tick task started");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_task_handler();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen ESP32-P4 Phase 2 Starting ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Hardware: ESP32-P4 with %d cores, rev v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Memory: %lu bytes free heap", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    ESP_LOGI(TAG, "Target: WiFi provisioning and network connectivity");
    
    // Initialize core system
    system_init();
    
    // Initialize WiFi integration system
    ESP_LOGI(TAG, "Initializing WiFi integration system");
    esp_err_t ret = howdy_wifi_integration_init(wifi_integration_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi integration: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start WiFi integration
    ESP_LOGI(TAG, "Starting WiFi integration");
    ret = howdy_wifi_integration_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi integration: %s", esp_err_to_name(ret));
        return;
    }
    
    // Create system monitor task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        system_monitor_task,
        "sys_monitor",
        4096,
        NULL,
        5,
        NULL,
        0  // Pin to core 0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create system monitor task");
        return;
    }
    
    // Create LVGL tick task
    task_ret = xTaskCreatePinnedToCore(
        lvgl_tick_task,
        "lvgl_tick",
        4096,
        NULL,
        10,  // High priority for UI responsiveness
        NULL,
        1  // Pin to core 1
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL tick task");
        return;
    }
    
    ESP_LOGI(TAG, "🚀 HowdyScreen Phase 2 system ready!");
    ESP_LOGI(TAG, "Features enabled:");
    ESP_LOGI(TAG, "  ✅ 800x800 MIPI-DSI display with LVGL");
    ESP_LOGI(TAG, "  ✅ GT911 capacitive touch controller");
    ESP_LOGI(TAG, "  ✅ WiFi provisioning with NVS persistence");
    ESP_LOGI(TAG, "  ✅ Interactive WiFi configuration UI");
    ESP_LOGI(TAG, "  ✅ ESP32-C6 WiFi remote support");
    ESP_LOGI(TAG, "  ✅ Network state management");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Next steps:");
    ESP_LOGI(TAG, "  📶 Configure WiFi via touch interface");
    ESP_LOGI(TAG, "  🔍 Add mDNS service discovery");
    ESP_LOGI(TAG, "  🔊 Implement audio output pipeline");
    ESP_LOGI(TAG, "  🎤 Integrate HowdyTTS communication");
    
    // Main monitoring loop
    while (1) {
        // Periodic system health check
        if (esp_get_free_heap_size() < 50000) {  // Less than 50KB free
            ESP_LOGW(TAG, "⚠️  Low memory warning: %lu bytes free", esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // Check every minute
    }
}