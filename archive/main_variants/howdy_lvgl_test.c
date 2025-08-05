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

static const char *TAG = "HowdyLVGL";

// Global objects
static lv_obj_t *main_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *audio_arc = NULL;
static lv_obj_t *center_btn = NULL;

static void create_ui(void);
static void update_demo(void *pvParameters);

static void create_ui(void)
{
    ESP_LOGI(TAG, "Creating LVGL UI...");
    
    // Create main screen
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x202124), 0);
    
    // Main container
    lv_obj_t *container = lv_obj_create(main_screen);
    lv_obj_set_size(container, 400, 400);  // Smaller for testing
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x202124), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    
    // Title
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "HowdyTTS");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Audio level arc
    audio_arc = lv_arc_create(container);
    lv_obj_set_size(audio_arc, 200, 200);
    lv_obj_center(audio_arc);
    lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x1a73e8), LV_PART_INDICATOR);
    lv_arc_set_range(audio_arc, 0, 100);
    lv_arc_set_value(audio_arc, 0);
    lv_obj_remove_style(audio_arc, NULL, LV_PART_KNOB);
    
    // Center button
    center_btn = lv_btn_create(container);
    lv_obj_set_size(center_btn, 80, 80);
    lv_obj_center(center_btn);
    lv_obj_set_style_bg_color(center_btn, lv_color_hex(0x1a73e8), 0);
    lv_obj_set_style_radius(center_btn, 40, 0);
    
    lv_obj_t *btn_label = lv_label_create(center_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_center(btn_label);
    
    // Status label
    status_label = lv_label_create(container);
    lv_label_set_text(status_label, "LVGL Test Running");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Load the screen
    lv_scr_load(main_screen);
    
    ESP_LOGI(TAG, "LVGL UI created successfully");
}

static void update_demo(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting LVGL demo update task...");
    
    int counter = 0;
    int audio_level = 0;
    bool increasing = true;
    
    while (1) {
        counter++;
        
        // Update audio level animation
        if (increasing) {
            audio_level += 2;
            if (audio_level >= 90) increasing = false;
        } else {
            audio_level -= 2;
            if (audio_level <= 10) increasing = true;
        }
        
        // Update UI elements
        if (audio_arc) {
            lv_arc_set_value(audio_arc, audio_level);
        }
        
        if (status_label) {
            char status_text[64];
            snprintf(status_text, sizeof(status_text), "Level: %d%% Counter: %d", audio_level, counter);
            lv_label_set_text(status_label, status_text);
        }
        
        // Change arc color based on level
        if (audio_arc) {
            if (audio_level > 70) {
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x34a853), LV_PART_INDICATOR);
            } else if (audio_level > 40) {
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0xfbbc04), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x1a73e8), LV_PART_INDICATOR);
            }
        }
        
        // Log progress
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "Demo running - Counter: %d, Audio Level: %d%%, Free Heap: %lu", 
                     counter, audio_level, esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS LVGL Test starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize LVGL port
    ESP_LOGI(TAG, "Initializing LVGL port...");
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 0,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    
    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "LVGL port initialized successfully");
    
    // Create UI
    create_ui();
    
    // Create demo update task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        update_demo,
        "lvgl_demo",
        4096,
        NULL,
        5,
        NULL,
        0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create demo task");
        return;
    }
    
    ESP_LOGI(TAG, "LVGL test initialized successfully");
    ESP_LOGI(TAG, "Demo is running - UI should be updating");
    
    // Main monitoring loop
    while (1) {
        ESP_LOGI(TAG, "System running - Free heap: %lu bytes", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}