#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"

// Waveshare BSP
#include "bsp/esp32_p4_nano.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "HowdyBSP";

// Global objects for HowdyTTS UI
static lv_obj_t *main_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *audio_arc = NULL;
static lv_obj_t *center_btn = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *title_label = NULL;

static void create_howdytts_ui(void);
static void update_demo(void *pvParameters);
static void touch_callback(lv_event_t *e);

static void touch_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Center button clicked!");
        
        // Change button color on click
        static bool toggled = false;
        if (toggled) {
            lv_obj_set_style_bg_color(center_btn, lv_color_hex(0x1a73e8), 0);
        } else {
            lv_obj_set_style_bg_color(center_btn, lv_color_hex(0x34a853), 0);
        }
        toggled = !toggled;
    }
}

static void create_howdytts_ui(void)
{
    ESP_LOGI(TAG, "Creating HowdyTTS UI with Waveshare BSP...");
    
    // Create main screen with dark theme
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x202124), 0);
    
    // Main container - sized for 800x800 display
    lv_obj_t *container = lv_obj_create(main_screen);
    lv_obj_set_size(container, 800, 800);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x202124), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 20, 0);
    
    // Title - HowdyTTS
    title_label = lv_label_create(container);
    lv_label_set_text(title_label, "HowdyTTS Assistant");
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 30);
    
    // Audio level arc - large circular meter
    audio_arc = lv_arc_create(container);
    lv_obj_set_size(audio_arc, 350, 350);
    lv_obj_center(audio_arc);
    lv_obj_set_style_arc_width(audio_arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x303134), LV_PART_MAIN);
    lv_obj_set_style_arc_width(audio_arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x1a73e8), LV_PART_INDICATOR);
    lv_arc_set_range(audio_arc, 0, 100);
    lv_arc_set_value(audio_arc, 0);
    lv_obj_remove_style(audio_arc, NULL, LV_PART_KNOB);
    
    // Center button - microphone/action button
    center_btn = lv_btn_create(container);
    lv_obj_set_size(center_btn, 120, 120);
    lv_obj_center(center_btn);
    lv_obj_set_style_bg_color(center_btn, lv_color_hex(0x1a73e8), 0);
    lv_obj_set_style_radius(center_btn, 60, 0);
    lv_obj_add_event_cb(center_btn, touch_callback, LV_EVENT_CLICKED, NULL);
    
    // Microphone icon in center button
    lv_obj_t *btn_icon = lv_label_create(center_btn);
    lv_label_set_text(btn_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(btn_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_icon, &lv_font_montserrat_32, 0);
    lv_obj_center(btn_icon);
    
    // Status label - below the arc
    status_label = lv_label_create(container);
    lv_label_set_text(status_label, "Tap to speak");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -120);
    
    // WiFi status - bottom left
    wifi_label = lv_label_create(container);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI " Connecting...");
    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xfbbc04), 0);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_16, 0);
    lv_obj_align(wifi_label, LV_ALIGN_BOTTOM_LEFT, 0, -30);
    
    // System info - bottom right
    lv_obj_t *system_info = lv_label_create(container);
    lv_label_set_text(system_info, "ESP32-P4");
    lv_obj_set_style_text_color(system_info, lv_color_hex(0xe8eaed), 0);
    lv_obj_set_style_text_font(system_info, &lv_font_montserrat_16, 0);
    lv_obj_align(system_info, LV_ALIGN_BOTTOM_RIGHT, 0, -30);
    
    // Load the screen
    lv_scr_load(main_screen);
    
    ESP_LOGI(TAG, "HowdyTTS UI created successfully for 800x800 display");
}

static void update_demo(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting HowdyTTS demo with BSP integration...");
    
    int counter = 0;
    int audio_level = 0;
    bool increasing = true;
    const char* states[] = {"Idle", "Listening", "Processing", "Speaking"};
    int current_state = 0;
    
    while (1) {
        counter++;
        
        // Update audio level animation
        if (increasing) {
            audio_level += 3;
            if (audio_level >= 85) increasing = false;
        } else {
            audio_level -= 3;
            if (audio_level <= 15) increasing = true;
        }
        
        // Update UI elements
        if (audio_arc) {
            lv_arc_set_value(audio_arc, audio_level);
        }
        
        // Change status every 100 iterations
        if (counter % 100 == 0) {
            current_state = (current_state + 1) % 4;
            if (status_label) {
                char status_text[64];
                snprintf(status_text, sizeof(status_text), "%s... (Level: %d%%)", states[current_state], audio_level);
                lv_label_set_text(status_label, status_text);
            }
        }
        
        // Change arc color based on state and level
        if (audio_arc) {
            if (current_state == 1) { // Listening
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x34a853), LV_PART_INDICATOR);
            } else if (current_state == 2) { // Processing
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0xfbbc04), LV_PART_INDICATOR);
            } else if (current_state == 3) { // Speaking
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0xea4335), LV_PART_INDICATOR);
            } else { // Idle
                lv_obj_set_style_arc_color(audio_arc, lv_color_hex(0x1a73e8), LV_PART_INDICATOR);
            }
        }
        
        // Update WiFi status
        if (wifi_label) {
            int wifi_strength = 30 + (counter % 70);
            char wifi_text[64];
            snprintf(wifi_text, sizeof(wifi_text), LV_SYMBOL_WIFI " %d%%", wifi_strength);
            lv_label_set_text(wifi_label, wifi_text);
        }
        
        // Log progress periodically
        if (counter % 200 == 0) {
            ESP_LOGI(TAG, "HowdyTTS Demo - Counter: %d, State: %s, Audio: %d%%, Free Heap: %lu", 
                     counter, states[current_state], audio_level, esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(50)); // 20 FPS update rate
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS ESP32-P4 BSP Test starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize BSP
    ESP_LOGI(TAG, "Initializing Waveshare ESP32-P4 BSP...");
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_LOGI(TAG, "BSP I2C initialized");
    
    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority = 4,
            .task_stack = 8192,
            .task_affinity = 0,
            .task_max_sleep_ms = 500,
            .timer_period_ms = 5
        },
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES / 10, // 1/10 of screen
        .double_buffer = true,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        }
    };
    
    bsp_display_start_with_config(&cfg);
    ESP_LOGI(TAG, "Display initialized - Resolution: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    
    // Initialize touch if available
    ESP_LOGI(TAG, "Initializing touch controller...");
    esp_err_t touch_ret = bsp_touch_start();
    if (touch_ret == ESP_OK) {
        ESP_LOGI(TAG, "Touch controller initialized successfully");
    } else {
        ESP_LOGW(TAG, "Touch controller initialization failed: %s", esp_err_to_name(touch_ret));
    }
    
    // Create HowdyTTS UI
    create_howdytts_ui();
    
    // Create demo update task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        update_demo,
        "howdy_demo",
        6144,
        NULL,
        5,
        NULL,
        0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create demo task");
        return;
    }
    
    ESP_LOGI(TAG, "HowdyTTS BSP test initialized successfully");
    ESP_LOGI(TAG, "Display should show HowdyTTS interface with 800x800 resolution");
    
    // Main monitoring loop
    while (1) {
        ESP_LOGI(TAG, "System running - Free heap: %lu bytes", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}