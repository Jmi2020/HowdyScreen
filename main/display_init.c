#include "display_init.h"
#include "esp_log.h"
#include "esp_check.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "DisplayInit";

// LVGL objects
static lv_obj_t *main_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *connection_label = NULL;
static lv_obj_t *spinner = NULL;

// Display state
static display_state_t current_display_state = DISPLAY_STATE_INIT;

esp_err_t display_init_hardware(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-P4 display hardware...");
    
    // Initialize BSP display
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BSP display started successfully");
    
    // Touch is initialized automatically with display
    lv_indev_t *touch_input = bsp_display_get_input_dev();
    if (touch_input == NULL) {
        ESP_LOGE(TAG, "Failed to get touch input device");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BSP touch input device ready");
    
    return ESP_OK;
}

esp_err_t display_init_lvgl(void)
{
    ESP_LOGI(TAG, "LVGL already initialized by BSP - ready to create UI");
    
    // BSP initializes LVGL automatically, so we just need to verify it's ready
    // The display and touch are already configured
    
    return ESP_OK;
}

void display_create_ready_screen(void)
{
    ESP_LOGI(TAG, "Creating 'Ready to Connect' screen...");
    
    // Create main screen
    main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1a1a1a), 0);  // Dark background
    
    // Create main container (centered for round display)
    lv_obj_t *main_container = lv_obj_create(main_screen);
    lv_obj_set_size(main_container, 760, 760);  // Slightly smaller than 800x800 for margins
    lv_obj_center(main_container);
    lv_obj_set_style_bg_color(main_container, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(main_container, 2, 0);
    lv_obj_set_style_border_color(main_container, lv_color_hex(0x4285f4), 0);  // Blue border
    lv_obj_set_style_radius(main_container, 380, 0);  // Round container for round display
    lv_obj_set_style_pad_all(main_container, 20, 0);
    
    // HowdyTTS Logo/Title
    lv_obj_t *title_label = lv_label_create(main_container);
    lv_label_set_text(title_label, "HowdyTTS");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x4285f4), 0);  // Blue text
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 60);
    
    // Subtitle
    lv_obj_t *subtitle_label = lv_label_create(main_container);
    lv_label_set_text(subtitle_label, "Voice Assistant Display");
    lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0xcccccc), 0);  // Light gray
    lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_14, 0);
    lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 100);
    
    // Status indicator (spinning loading indicator)
    spinner = lv_spinner_create(main_container, 1000, 60);  // 1s rotation, 60° arc
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x4285f4), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x34a853), LV_PART_INDICATOR);  // Green indicator
    
    // Status text
    status_label = lv_label_create(main_container);
    lv_label_set_text(status_label, "System Ready");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x34a853), 0);  // Green text
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 60);
    
    // Connection status
    connection_label = lv_label_create(main_container);
    lv_label_set_text(connection_label, "Ready to Connect");
    lv_obj_set_style_text_color(connection_label, lv_color_hex(0xffffff), 0);  // White text
    lv_obj_set_style_text_font(connection_label, &lv_font_montserrat_14, 0);
    lv_obj_align(connection_label, LV_ALIGN_CENTER, 0, 90);
    
    // Instructions
    lv_obj_t *instruction_label = lv_label_create(main_container);
    lv_label_set_text(instruction_label, "Touch screen to begin setup");
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0x888888), 0);  // Gray text
    lv_obj_set_style_text_font(instruction_label, &lv_font_montserrat_14, 0);
    lv_obj_align(instruction_label, LV_ALIGN_BOTTOM_MID, 0, -80);
    
    // Hardware info
    lv_obj_t *hw_info_label = lv_label_create(main_container);
    lv_label_set_text(hw_info_label, "ESP32-P4 | 800x800 Display | WiFi 6");
    lv_obj_set_style_text_color(hw_info_label, lv_color_hex(0x666666), 0);  // Dark gray
    lv_obj_set_style_text_font(hw_info_label, &lv_font_montserrat_14, 0);
    lv_obj_align(hw_info_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    
    // Load the screen
    lv_scr_load(main_screen);
    
    current_display_state = DISPLAY_STATE_READY;
    ESP_LOGI(TAG, "Ready screen created and loaded");
}

void display_update_status(const char *status_text, uint32_t color)
{
    if (status_label != NULL) {
        lv_label_set_text(status_label, status_text);
        lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
        ESP_LOGI(TAG, "Status updated: %s", status_text);
    }
}

void display_update_connection_status(const char *connection_text, uint32_t color)
{
    if (connection_label != NULL) {
        lv_label_set_text(connection_label, connection_text);
        lv_obj_set_style_text_color(connection_label, lv_color_hex(color), 0);
        ESP_LOGI(TAG, "Connection status updated: %s", connection_text);
    }
}

void display_show_wifi_connecting(void)
{
    display_update_status("Connecting...", 0xfbbc04);  // Yellow
    display_update_connection_status("WiFi Setup", 0xfbbc04);
    
    // Make spinner more prominent during connection
    if (spinner != NULL) {
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0xfbbc04), LV_PART_INDICATOR);
    }
}

void display_show_wifi_connected(void)
{
    display_update_status("Connected", 0x34a853);  // Green
    display_update_connection_status("WiFi Connected", 0x34a853);
    
    // Hide spinner when connected
    if (spinner != NULL) {
        lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

void display_show_error(const char *error_text)
{
    display_update_status("Error", 0xea4335);  // Red
    display_update_connection_status(error_text, 0xea4335);
    
    // Show error indication on spinner
    if (spinner != NULL) {
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0xea4335), LV_PART_INDICATOR);
    }
}

display_state_t display_get_state(void)
{
    return current_display_state;
}

esp_err_t display_init_complete(void)
{
    ESP_LOGI(TAG, "Starting complete display initialization...");
    
    // Step 1: Initialize hardware
    esp_err_t ret = display_init_hardware();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed");
        return ret;
    }
    
    // Step 2: Initialize LVGL
    ret = display_init_lvgl();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL initialization failed");
        return ret;
    }
    
    // Step 3: Create UI
    display_create_ready_screen();
    
    ESP_LOGI(TAG, "Display initialization completed successfully");
    return ESP_OK;
}