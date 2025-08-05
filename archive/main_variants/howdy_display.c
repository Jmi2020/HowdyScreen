#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"

// BSP and Display
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_lvgl_port.h" 
#include "lvgl.h"

// UI Management
#include "ui_manager.h"

static const char *TAG = "HowdyDisplay";

// System state
typedef enum {
    SYSTEM_STATE_INIT,
    SYSTEM_STATE_DISPLAY_READY,
    SYSTEM_STATE_IDLE,
    SYSTEM_STATE_ERROR
} system_state_t;

static system_state_t current_system_state = SYSTEM_STATE_INIT;

// Function prototypes
static void system_init_display(void);
static void system_init_touch(void);
static void demo_task(void *pvParameters);

static void system_init_display(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4 display (800x800)...");
    
    // Initialize the BSP display
    ESP_ERROR_CHECK(bsp_display_start());
    ESP_LOGI(TAG, "BSP display started");
    
    // Configure LVGL port
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         // LVGL task priority
        .task_stack = 8192,         // Stack size for LVGL task
        .task_affinity = 0,         // Run on core 0
        .task_max_sleep_ms = 500,   // Maximum sleep time
        .timer_period_ms = 5        // Timer period for LVGL tick
    };
    
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    ESP_LOGI(TAG, "LVGL port initialized");
    
    // Get display handle and verify
    lv_disp_t *disp = bsp_display_get_disp();
    if (disp != NULL) {
        ESP_LOGI(TAG, "Display initialized successfully - 800x800 round screen");
        
        // Log display driver info
        lv_disp_drv_t *driver = disp->driver;
        ESP_LOGI(TAG, "Display resolution: %dx%d", driver->hor_res, driver->ver_res);
    } else {
        ESP_LOGE(TAG, "Failed to get display handle");
        current_system_state = SYSTEM_STATE_ERROR;
        return;
    }
    
    current_system_state = SYSTEM_STATE_DISPLAY_READY;
}

static void system_init_touch(void)
{
    ESP_LOGI(TAG, "Initializing CST9217 touch controller...");
    
    // Initialize touch controller
    ESP_ERROR_CHECK(bsp_touch_start());
    ESP_LOGI(TAG, "Touch controller started");
    
    // Get touch input device and verify
    lv_indev_t *touch_indev = bsp_touch_get_indev();
    if (touch_indev != NULL) {
        ESP_LOGI(TAG, "Touch controller initialized successfully");
        ESP_LOGI(TAG, "Touch input device type: %d", touch_indev->driver->type);
    } else {
        ESP_LOGE(TAG, "Failed to get touch input device");
        // Don't set error state - touch is not critical for basic display testing
    }
}

static void demo_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting HowdyTTS display demo...");
    
    int counter = 0;
    int audio_level = 0;
    bool audio_increasing = true;
    
    while (current_system_state != SYSTEM_STATE_ERROR) {
        
        switch (current_system_state) {
            case SYSTEM_STATE_DISPLAY_READY:
                ESP_LOGI(TAG, "Display ready, transitioning to IDLE");
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_set_wifi_strength(75); // Simulate good WiFi
                current_system_state = SYSTEM_STATE_IDLE;
                break;
                
            case SYSTEM_STATE_IDLE:
                // Demo: Cycle through different UI states
                counter++;
                
                // Simulate audio level changes
                if (audio_increasing) {
                    audio_level += 5;
                    if (audio_level >= 90) audio_increasing = false;
                } else {
                    audio_level -= 5;
                    if (audio_level <= 10) audio_increasing = true;
                }
                ui_manager_update_audio_level(audio_level);
                
                // Cycle through states for demo
                if (counter % 20 == 0) {
                    ESP_LOGI(TAG, "Demo: Switching to LISTENING state");
                    ui_manager_set_state(UI_STATE_LISTENING);
                    ui_manager_update_status("Listening for voice...");
                } else if (counter % 20 == 5) {
                    ESP_LOGI(TAG, "Demo: Switching to PROCESSING state");
                    ui_manager_set_state(UI_STATE_PROCESSING);
                    ui_manager_update_status("Processing with HowdyTTS...");
                } else if (counter % 20 == 10) {
                    ESP_LOGI(TAG, "Demo: Switching to SPEAKING state");
                    ui_manager_set_state(UI_STATE_SPEAKING);
                    ui_manager_update_status("Playing response...");
                } else if (counter % 20 == 15) {
                    ESP_LOGI(TAG, "Demo: Back to IDLE state");
                    ui_manager_set_state(UI_STATE_IDLE);
                    ui_manager_update_status("Tap to speak");
                }
                
                // Simulate WiFi strength changes
                int wifi_strength = 50 + (counter % 50);
                ui_manager_set_wifi_strength(wifi_strength);
                
                vTaskDelay(pdMS_TO_TICKS(500)); // Update every 500ms
                break;
                
            case SYSTEM_STATE_ERROR:
                ESP_LOGE(TAG, "System in error state");
                ui_manager_set_state(UI_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
                
            default:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
        }
        
        // Log system status periodically
        if (counter % 40 == 0) {
            ESP_LOGI(TAG, "Demo running - Counter: %d, Audio Level: %d%%, State: %d", 
                     counter, audio_level, current_system_state);
        }
    }
    
    ESP_LOGI(TAG, "Demo task ended");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS ESP32-P4 Display Test starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize display subsystem
    system_init_display();
    
    if (current_system_state == SYSTEM_STATE_ERROR) {
        ESP_LOGE(TAG, "Display initialization failed");
        return;
    }
    
    // Initialize touch controller
    system_init_touch();
    
    // Initialize UI manager
    ESP_LOGI(TAG, "Initializing UI manager...");
    ESP_ERROR_CHECK(ui_manager_init());
    ESP_LOGI(TAG, "UI manager initialized");
    
    // Create demo task to show UI functionality
    xTaskCreatePinnedToCore(
        demo_task,
        "demo_task",
        8192,
        NULL,
        5,  // Lower priority than LVGL
        NULL,
        0   // Pin to core 0 with UI
    );
    
    ESP_LOGI(TAG, "HowdyTTS display test initialized successfully");
    ESP_LOGI(TAG, "Watch the 800x800 round display for HowdyTTS UI demo");
}