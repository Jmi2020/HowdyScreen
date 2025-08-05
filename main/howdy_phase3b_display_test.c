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

// UI Manager for enhanced voice assistant interface
#include "ui_manager.h"
// WiFi connectivity
#include "wifi_manager.h"
// Service discovery for HowdyTTS servers
#include "service_discovery.h"

static const char *TAG = "HowdyDisplayTest";

// Global handles
static bool system_ready = false;
static bool wifi_connected = false;

// WiFi credentials from menuconfig
#define WIFI_SSID CONFIG_HOWDY_WIFI_SSID
#define WIFI_PASSWORD CONFIG_HOWDY_WIFI_PASSWORD

// Function prototypes
static void system_init(void);
static void lvgl_tick_task(void *pvParameters);
static void wifi_event_handler(wifi_event_id_t event_id, void *event_data);
static void service_discovered_handler(const howdytts_server_info_t *server_info);
static void network_init(void);

static void system_init(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen Display Test System Initialization ===");
    
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
    
    // Initialize UI Manager for enhanced voice assistant interface
    ESP_LOGI(TAG, "Initializing UI Manager with Howdy character animations");
    esp_err_t ui_ret = ui_manager_init();
    if (ui_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI Manager: %s", esp_err_to_name(ui_ret));
        return;
    }
    
    // Set initial UI state
    ui_manager_set_state(UI_STATE_INIT);
    ui_manager_update_status("System starting...");
    ESP_LOGI(TAG, "UI Manager initialized successfully");
    
    system_ready = true;
}

static void wifi_event_handler(wifi_event_id_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_CONNECTED_ID:
            ESP_LOGI(TAG, "WiFi connected to AP");
            ui_manager_update_status("WiFi connected");
            break;
            
        case WIFI_EVENT_DISCONNECTED_ID:
            ESP_LOGW(TAG, "WiFi disconnected from AP");
            wifi_connected = false;
            ui_manager_set_wifi_strength(0);
            ui_manager_update_status("WiFi disconnected");
            break;
            
        case WIFI_EVENT_GOT_IP_ID:
            ESP_LOGI(TAG, "WiFi got IP address");
            wifi_connected = true;
            ui_manager_update_status("Connected - Searching for HowdyTTS...");
            
            // Update WiFi signal strength
            int signal_strength = wifi_manager_get_signal_strength();
            ui_manager_set_wifi_strength(signal_strength);
            
            // Start scanning for HowdyTTS servers
            ESP_LOGI(TAG, "Starting mDNS scan for HowdyTTS servers...");
            service_discovery_start_scan(0);  // Continuous scan
            break;
            
        case WIFI_EVENT_SCAN_DONE_ID:
            ESP_LOGI(TAG, "WiFi scan completed");
            break;
            
        default:
            break;
    }
}

static void service_discovered_handler(const howdytts_server_info_t *server_info)
{
    if (server_info) {
        ESP_LOGI(TAG, "HowdyTTS server discovered!");
        ESP_LOGI(TAG, "  Address: %s:%d", server_info->ip_addr, server_info->port);
        ESP_LOGI(TAG, "  Hostname: %s", server_info->hostname);
        ESP_LOGI(TAG, "  Version: %s", server_info->version);
        
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "HowdyTTS found: %s", server_info->hostname);
        ui_manager_update_status(status_msg);
        
        // In a real implementation, we would connect to the WebSocket here
        // For now, just update the UI to show we found a server
        vTaskDelay(pdMS_TO_TICKS(2000));
        ui_manager_set_state(UI_STATE_IDLE);
        ui_manager_update_status("Ready - Tap to speak");
    }
}

static void network_init(void)
{
    ESP_LOGI(TAG, "Initializing network components...");
    
    // Initialize WiFi manager
    esp_err_t ret = wifi_manager_init(wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize service discovery
    ret = service_discovery_init(service_discovered_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize service discovery: %s", esp_err_to_name(ret));
        return;
    }
    
    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
    ui_manager_update_status("Connecting to WiFi...");
    
    ret = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        ui_manager_set_state(UI_STATE_ERROR);
        ui_manager_update_status("WiFi connection failed");
    }
}

static void lvgl_tick_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LVGL tick task started");
    
    while (1) {
        lv_tick_inc(5);  // 5ms tick
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen ESP32-P4 Display Test ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Hardware: ESP32-P4 with %d cores, rev v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Memory: %lu bytes free heap", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    ESP_LOGI(TAG, "Target: Display initialization test");
    
    // Initialize core system
    system_init();
    
    if (!system_ready) {
        ESP_LOGE(TAG, "System initialization failed!");
        return;
    }
    
    // Create LVGL tick task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
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
    
    ESP_LOGI(TAG, "🚀 UI Manager initialized - starting voice assistant demo!");
    
    // Initialize network components
    network_init();
    
    // Wait for network initialization
    vTaskDelay(pdMS_TO_TICKS(3000));  // Give WiFi time to connect
    
    // If not connected to WiFi, run in demo mode
    if (!wifi_connected) {
        ESP_LOGW(TAG, "No network connection, running in demo mode");
        ui_manager_set_state(UI_STATE_IDLE);
        ui_manager_update_status("Demo mode - Cycling states");
    }
    
    // Demo loop to showcase voice assistant states and Howdy character animations
    int demo_cycle = 0;
    while (1) {
        ESP_LOGI(TAG, "Voice assistant demo running... Free heap: %lu bytes", esp_get_free_heap_size());
        
        // Update WiFi signal strength periodically
        if (wifi_connected) {
            int signal_strength = wifi_manager_get_signal_strength();
            ui_manager_set_wifi_strength(signal_strength);
        }
        
        // Cycle through different voice assistant states every 10 seconds
        switch (demo_cycle % 5) {
            case 0:
                ESP_LOGI(TAG, "Demo: IDLE state - Howdy greeting pose");
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_update_status("Ready to speak - Tap Howdy to test!");
                ui_manager_update_audio_level(0);
                break;
                
            case 1:
                ESP_LOGI(TAG, "Demo: LISTENING state - Howdy listening pose");
                ui_manager_set_state(UI_STATE_LISTENING);
                ui_manager_update_status("Listening...");
                // Simulate audio levels
                for (int i = 0; i < 5; i++) {
                    ui_manager_update_audio_level(20 + (i * 15));
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
                break;
                
            case 2:
                ESP_LOGI(TAG, "Demo: PROCESSING state - Howdy thinking pose");
                ui_manager_set_state(UI_STATE_PROCESSING);
                ui_manager_update_status("Processing your request...");
                ui_manager_update_audio_level(0);
                break;
                
            case 3:
                ESP_LOGI(TAG, "Demo: SPEAKING state - Howdy response pose");
                ui_manager_set_state(UI_STATE_SPEAKING);
                ui_manager_update_status("Speaking response...");
                // Simulate speaking audio levels
                for (int i = 0; i < 5; i++) {
                    ui_manager_update_audio_level(30 + (i * 10));
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
                break;
                
            case 4:
                ESP_LOGI(TAG, "Demo: ERROR state - System error");
                ui_manager_set_state(UI_STATE_ERROR);
                ui_manager_update_status("Connection error - retrying...");
                ui_manager_update_audio_level(0);
                break;
        }
        
        demo_cycle++;
        vTaskDelay(pdMS_TO_TICKS(8000));  // Hold each state for 8 seconds
    }
}