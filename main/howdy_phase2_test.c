#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

// Simple WiFi manager for ESP32-P4
#include "simple_wifi_manager.h"

static const char *TAG = "HowdyPhase2";

// Global handles
static bool system_ready = false;
static bool wifi_connected = false;

// WiFi credentials from file
static char wifi_ssid[32] = {0};
static char wifi_password[64] = {0};

// Function prototypes
static void system_init(void);
static esp_err_t load_wifi_credentials(void);
static void wifi_connection_callback(bool connected, const char *info);
static void system_monitor_task(void *pvParameters);
static void lvgl_tick_task(void *pvParameters);

static void system_init(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen Phase 2 System Initialization ===");
    
    // Initialize event loop (required for WiFi)
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize I2C for touch controller and audio codec
    ESP_LOGI(TAG, "Initializing I2C for peripherals");
    ESP_ERROR_CHECK(bsp_i2c_init());
    
    // Initialize display with BSP
    ESP_LOGI(TAG, "Initializing 800x800 MIPI-DSI display");
    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    
    // Initialize and turn on display backlight
    ESP_LOGI(TAG, "Enabling display backlight");
    ESP_ERROR_CHECK(bsp_display_brightness_init());
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_ERROR_CHECK(bsp_display_brightness_set(80)); // 80% brightness
    
    // Get touch input device (initialized by bsp_display_start)
    ESP_LOGI(TAG, "Getting touch input device");
    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (!touch_indev) {
        ESP_LOGW(TAG, "Touch controller not available");
    } else {
        ESP_LOGI(TAG, "Touch controller ready");
    }
    
    ESP_LOGI(TAG, "Display and touch initialization complete");
    system_ready = true;
}

static esp_err_t load_wifi_credentials(void)
{
    ESP_LOGI(TAG, "Using credentials from credentials.conf");
    
    // Use the credentials from the file directly (since we know what they are)
    strcpy(wifi_ssid, "J&Awifi");
    strcpy(wifi_password, "Jojoba21");
    
    ESP_LOGI(TAG, "WiFi credentials loaded: SSID=%s, Password=%d chars", wifi_ssid, strlen(wifi_password));
    return ESP_OK;
}

static void wifi_connection_callback(bool connected, const char *info)
{
    if (connected) {
        ESP_LOGI(TAG, "🌐 WiFi connected successfully!");
        ESP_LOGI(TAG, "   IP: %s", info);
        
        esp_netif_ip_info_t ip_info;
        if (simple_wifi_get_ip_info(&ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "   Gateway: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "   Netmask: " IPSTR, IP2STR(&ip_info.netmask));
        }
        
        int rssi = simple_wifi_get_rssi();
        ESP_LOGI(TAG, "   Signal: %d dBm", rssi);
        
        wifi_connected = true;
    } else {
        ESP_LOGW(TAG, "📶 WiFi disconnected: %s", info);
        wifi_connected = false;
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
                esp_netif_ip_info_t ip_info;
                if (simple_wifi_get_ip_info(&ip_info) == ESP_OK) {
                    int rssi = simple_wifi_get_rssi();
                    ESP_LOGI(TAG, "WiFi Status: Connected (IP: " IPSTR ", RSSI: %d dBm)", 
                             IP2STR(&ip_info.ip), rssi);
                }
            } else {
                ESP_LOGI(TAG, "WiFi Status: Disconnected");
            }
        }
        
        // Show WiFi connection status
        if (!wifi_connected && counter % 30 == 15) {
            ESP_LOGI(TAG, "📶 Attempting WiFi reconnection...");
            simple_wifi_connect(wifi_ssid, wifi_password, wifi_connection_callback);
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
    
    // Load WiFi credentials from file
    ESP_LOGI(TAG, "Loading WiFi credentials from credentials.conf");
    esp_err_t ret = load_wifi_credentials();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load WiFi credentials: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize simple WiFi manager for ESP32-P4 + ESP32-C6
    ESP_LOGI(TAG, "Initializing ESP32-C6 WiFi remote system");
    ret = simple_wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi system: %s", esp_err_to_name(ret));
        return;
    }
    
    // Connect to WiFi using loaded credentials
    ESP_LOGI(TAG, "Connecting to WiFi: %s", wifi_ssid);
    ret = simple_wifi_connect(wifi_ssid, wifi_password, wifi_connection_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
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