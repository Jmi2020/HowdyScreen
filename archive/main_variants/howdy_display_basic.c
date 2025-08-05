#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"

// LVGL
#include "esp_lvgl_port.h"
#include "lvgl.h"

// UI Management
#include "ui_manager.h"

static const char *TAG = "HowdyDisplay";

// System state
typedef enum {
    SYSTEM_STATE_INIT,
    SYSTEM_STATE_UI_READY,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_ERROR
} system_state_t;

static system_state_t current_system_state = SYSTEM_STATE_INIT;

// Function prototypes
static void system_init_lvgl(void);
static void demo_task(void *pvParameters);

static void system_init_lvgl(void)
{
    ESP_LOGI(TAG, "Initializing LVGL for ESP32-P4...");
    
    // Basic LVGL port configuration
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         // LVGL task priority
        .task_stack = 8192,         // Stack size for LVGL task
        .task_affinity = 0,         // Run on core 0
        .task_max_sleep_ms = 500,   // Maximum sleep time
        .timer_period_ms = 5        // Timer period for LVGL tick
    };
    
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    ESP_LOGI(TAG, "LVGL port initialized");
    
    // For now, we'll create a basic display without hardware
    // This will allow us to test the UI framework
    current_system_state = SYSTEM_STATE_UI_READY;
}

static void demo_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting HowdyTTS UI demo task...");
    
    int counter = 0;
    int audio_level = 0;
    bool audio_increasing = true;
    
    while (current_system_state != SYSTEM_STATE_ERROR) {
        
        switch (current_system_state) {
            case SYSTEM_STATE_UI_READY:
                ESP_LOGI(TAG, "UI ready, transitioning to RUNNING");
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_set_wifi_strength(75); // Simulate good WiFi
                current_system_state = SYSTEM_STATE_RUNNING;
                break;
                
            case SYSTEM_STATE_RUNNING:
                // Demo: Cycle through different UI states
                counter++;
                
                // Simulate audio level changes
                if (audio_increasing) {
                    audio_level += 3;
                    if (audio_level >= 85) audio_increasing = false;
                } else {
                    audio_level -= 3;
                    if (audio_level <= 15) audio_increasing = true;
                }
                ui_manager_update_audio_level(audio_level);
                
                // Cycle through states for demo
                if (counter % 30 == 0) {
                    ESP_LOGI(TAG, "Demo: Switching to LISTENING state");
                    ui_manager_set_state(UI_STATE_LISTENING);
                    ui_manager_update_status("Listening for voice...");
                } else if (counter % 30 == 8) {
                    ESP_LOGI(TAG, "Demo: Switching to PROCESSING state");
                    ui_manager_set_state(UI_STATE_PROCESSING);
                    ui_manager_update_status("Processing with HowdyTTS...");
                } else if (counter % 30 == 16) {
                    ESP_LOGI(TAG, "Demo: Switching to SPEAKING state");
                    ui_manager_set_state(UI_STATE_SPEAKING);
                    ui_manager_update_status("Playing response...");
                } else if (counter % 30 == 24) {
                    ESP_LOGI(TAG, "Demo: Back to IDLE state");
                    ui_manager_set_state(UI_STATE_IDLE);
                    ui_manager_update_status("Tap to speak");
                }
                
                // Simulate WiFi strength changes
                int wifi_strength = 30 + (counter % 60);
                ui_manager_set_wifi_strength(wifi_strength);
                
                vTaskDelay(pdMS_TO_TICKS(300)); // Update every 300ms
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
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "Demo running - Counter: %d, Audio Level: %d%%, WiFi: %d%%, Free Heap: %lu", 
                     counter, audio_level, 30 + (counter % 60), esp_get_free_heap_size());
        }
        
        // Test mute toggle every 100 iterations
        if (counter % 100 == 50) {
            bool current_mute = ui_manager_is_muted();
            ui_manager_set_mute(!current_mute);
            ESP_LOGI(TAG, "Demo: Toggled mute to %s", !current_mute ? "ON" : "OFF");
        }
    }
    
    ESP_LOGI(TAG, "Demo task ended");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS ESP32-P4 UI Framework Test starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize LVGL without hardware BSP for now
    system_init_lvgl();
    
    if (current_system_state == SYSTEM_STATE_ERROR) {
        ESP_LOGE(TAG, "LVGL initialization failed");
        return;
    }
    
    // Initialize UI manager
    ESP_LOGI(TAG, "Initializing UI manager...");
    esp_err_t ui_ret = ui_manager_init();
    if (ui_ret != ESP_OK) {
        ESP_LOGE(TAG, "UI manager initialization failed: %s", esp_err_to_name(ui_ret));
        current_system_state = SYSTEM_STATE_ERROR;
        return;
    }
    ESP_LOGI(TAG, "UI manager initialized successfully");
    
    // Create demo task to show UI functionality
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        demo_task,
        "ui_demo",
        8192,
        NULL,
        5,  // Lower priority than LVGL
        NULL,
        0   // Pin to core 0 with UI
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create demo task");
        current_system_state = SYSTEM_STATE_ERROR;
        return;
    }
    
    ESP_LOGI(TAG, "HowdyTTS UI framework test initialized successfully");
    ESP_LOGI(TAG, "UI demo is running - check logs for state transitions");
    
    // Main loop - just monitor system
    while (1) {
        if (current_system_state == SYSTEM_STATE_ERROR) {
            ESP_LOGE(TAG, "System error detected, restarting in 10 seconds...");
            vTaskDelay(pdMS_TO_TICKS(10000));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}