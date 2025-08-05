#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_check.h"
#include "nvs_flash.h"

// BSP and hardware components
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// Custom components (only the ones that work)
#include "audio_processor.h"
#include "network_manager.h"

static const char *TAG = "HowdyBasicIntegration";

// Application state machine
typedef enum {
    APP_STATE_INIT,
    APP_STATE_DISPLAY_INIT,
    APP_STATE_NETWORK_INIT,
    APP_STATE_AUDIO_INIT,
    APP_STATE_READY,
    APP_STATE_ERROR
} app_state_t;

// Simplified application context
typedef struct {
    // Component handles
    network_manager_t network;
    
    // System state
    app_state_t state;
    SemaphoreHandle_t state_mutex;
    EventGroupHandle_t system_events;
    
    // Configuration
    char wifi_ssid[32];
    char wifi_password[64];
    
    // System status
    bool display_ready;
    bool network_ready;
    bool audio_ready;
} howdy_app_t;

// Event bits for system coordination
#define DISPLAY_READY_BIT   BIT0
#define NETWORK_READY_BIT   BIT1
#define AUDIO_READY_BIT     BIT2

static howdy_app_t s_app = {0};

// Audio event callback
static void audio_event_handler(audio_event_t event, void *data, size_t len)
{
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            // Handle audio data - for now just log
            if (data && len > 0) {
                ESP_LOGI(TAG, "Audio data ready: %zu bytes", len);
            }
            break;
            
        case AUDIO_EVENT_STARTED:
            ESP_LOGI(TAG, "Audio processing started");
            break;
            
        case AUDIO_EVENT_STOPPED:
            ESP_LOGI(TAG, "Audio processing stopped");
            break;
            
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio error occurred");
            break;
    }
}

// Application state transition with thread safety
static esp_err_t app_transition_to(app_state_t new_state)
{
    if (xSemaphoreTake(s_app.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take state mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    app_state_t old_state = s_app.state;
    s_app.state = new_state;
    
    xSemaphoreGive(s_app.state_mutex);
    
    ESP_LOGI(TAG, "State transition: %d -> %d", old_state, new_state);
    return ESP_OK;
}

// Initialize display subsystem
static esp_err_t init_display_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing display subsystem...");
    
    // Configure BSP display with improved buffer size
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 100, // Increase buffer size significantly
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
    
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        return ESP_FAIL;
    }
    
    // Enable backlight
    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable backlight: %s", esp_err_to_name(ret));
    }
    
    // Create simple startup screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "HowdyScreen\\nBasic Integration\\nRunning...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    s_app.display_ready = true;
    xEventGroupSetBits(s_app.system_events, DISPLAY_READY_BIT);
    
    ESP_LOGI(TAG, "Display subsystem initialized successfully");
    return ESP_OK;
}

// Initialize network subsystem
static esp_err_t init_network_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing network subsystem...");
    
    // Initialize network manager
    ESP_RETURN_ON_ERROR(network_manager_init(&s_app.network, 
                                           s_app.wifi_ssid, 
                                           s_app.wifi_password,
                                           "192.168.1.100",  // Default server IP
                                           8080), 
                       TAG, "Network manager init failed");
    
    // Connect to WiFi
    esp_err_t ret = network_manager_connect(&s_app.network);
    if (ret == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "WiFi connection timeout - continuing without network");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connection failed: %s", esp_err_to_name(ret));
        return ret;
    } else {
        ESP_LOGI(TAG, "WiFi connected successfully");
    }
    
    s_app.network_ready = true;
    xEventGroupSetBits(s_app.system_events, NETWORK_READY_BIT);
    
    ESP_LOGI(TAG, "Network subsystem initialized");
    return ESP_OK;
}

// Initialize audio subsystem
static esp_err_t init_audio_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing audio subsystem...");
    
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,
        .channels = 1,
        .bits_per_sample = 16,
        .dma_buf_count = 8,
        .dma_buf_len = 320,  // 20ms at 16kHz
        .task_priority = 20,
        .task_core = 1
    };
    
    ESP_RETURN_ON_ERROR(audio_processor_init(&audio_config), TAG, "Audio processor init failed");
    
    // Set the audio event callback
    ESP_RETURN_ON_ERROR(audio_processor_set_callback(audio_event_handler), TAG, "Audio callback set failed");
    
    ESP_RETURN_ON_ERROR(audio_processor_start_capture(), TAG, "Audio processor start failed");
    
    s_app.audio_ready = true;
    xEventGroupSetBits(s_app.system_events, AUDIO_READY_BIT);
    
    ESP_LOGI(TAG, "Audio subsystem initialized successfully");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 HowdyScreen Basic Integration Starting ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize application context
    s_app.state_mutex = xSemaphoreCreateMutex();
    s_app.system_events = xEventGroupCreate();
    s_app.state = APP_STATE_INIT;
    
    // Load configuration - using default values for now
    strncpy(s_app.wifi_ssid, "YourWiFiSSID", sizeof(s_app.wifi_ssid) - 1);
    strncpy(s_app.wifi_password, "YourWiFiPassword", sizeof(s_app.wifi_password) - 1);
    
    // Phase 1: Initialize display (highest priority for user feedback)
    app_transition_to(APP_STATE_DISPLAY_INIT);
    ret = init_display_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    // Phase 2: Initialize network
    app_transition_to(APP_STATE_NETWORK_INIT);
    ret = init_network_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(ret));
        // Continue without network for now
    }
    
    // Phase 3: Initialize audio subsystem
    app_transition_to(APP_STATE_AUDIO_INIT);
    ret = init_audio_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio initialization failed: %s", esp_err_to_name(ret));
        // Continue without audio for now
    }
    
    // Wait for display to be ready (minimum requirement)
    EventBits_t bits = xEventGroupWaitBits(s_app.system_events, DISPLAY_READY_BIT, 
                                          pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
    
    if (!(bits & DISPLAY_READY_BIT)) {
        ESP_LOGE(TAG, "Display not ready - cannot continue");
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    app_transition_to(APP_STATE_READY);
    ESP_LOGI(TAG, "=== HowdyScreen Basic Integration Ready ===");
    ESP_LOGI(TAG, "Display: %s", s_app.display_ready ? "Ready" : "Failed");
    ESP_LOGI(TAG, "Network: %s", s_app.network_ready ? "Ready" : "Failed");
    ESP_LOGI(TAG, "Audio: %s", s_app.audio_ready ? "Ready" : "Failed");
    
    // Update display with status
    if (s_app.display_ready) {
        bsp_display_lock(0);
        lv_obj_t *scr = lv_scr_act();
        lv_obj_clean(scr);
        
        lv_obj_t *status_label = lv_label_create(scr);
        char status_text[256];
        snprintf(status_text, sizeof(status_text), 
                "HowdyScreen Status:\\n"
                "Display: %s\\n"
                "Network: %s\\n" 
                "Audio: %s\\n"
                "Free Heap: %lu KB",
                s_app.display_ready ? "OK" : "FAIL",
                s_app.network_ready ? "OK" : "FAIL",
                s_app.audio_ready ? "OK" : "FAIL",
                esp_get_free_heap_size() / 1024);
        
        lv_label_set_text(status_label, status_text);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
        bsp_display_unlock();
    }
    
    // Main application loop
    int counter = 0;
    while (true) {
        // Update system status every 5 seconds
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "System running - Counter: %d, Free heap: %lu bytes", 
                     counter / 10, esp_get_free_heap_size());
            
            // Update display counter
            if (s_app.display_ready) {
                bsp_display_lock(0);
                lv_obj_t *scr = lv_scr_act();
                
                // Find and update counter label or create new one
                lv_obj_t *counter_label = lv_label_create(scr);
                char counter_text[64];
                snprintf(counter_text, sizeof(counter_text), "Runtime: %d sec", counter / 10);
                lv_label_set_text(counter_label, counter_text);
                lv_obj_set_style_text_color(counter_label, lv_color_hex(0x00FF00), 0);
                lv_obj_align(counter_label, LV_ALIGN_BOTTOM_MID, 0, -20);
                bsp_display_unlock();
            }
        }
        
        // Check system health
        size_t free_heap = esp_get_free_heap_size();
        if (free_heap < 50000) {  // Less than 50KB free
            ESP_LOGW(TAG, "Low memory warning: %zu bytes free", free_heap);
        }
        
        counter++;
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms loop
    }

error_loop:
    ESP_LOGE(TAG, "Application entered error state");
    while (true) {
        ESP_LOGE(TAG, "System in error state - check logs above for details");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}