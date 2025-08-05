#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

static const char *TAG = "HowdyDisplayTest";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 Waveshare 3.4\" Display Test Starting...");
    
    // CRITICAL: Power stabilization delay
    ESP_LOGI(TAG, "Waiting for power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Print basic system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    
    // Use the high-level BSP display initialization as recommended
    ESP_LOGI(TAG, "Starting BSP display initialization...");
    
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display!");
        
        // Fallback: Try manual initialization
        ESP_LOGW(TAG, "Attempting manual display initialization...");
        
        // Initialize I2C first
        esp_err_t ret = bsp_i2c_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "I2C initialized successfully");
        
        // Initialize display brightness control
        ret = bsp_display_brightness_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Display brightness init failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Display brightness control initialized");
        }
        
        // Manual reset sequence
        ESP_LOGI(TAG, "Performing display reset sequence...");
        gpio_set_direction(BSP_LCD_RST, GPIO_MODE_OUTPUT);
        gpio_set_level(BSP_LCD_RST, 0);  // Assert reset
        vTaskDelay(pdMS_TO_TICKS(10));   // Hold reset for 10ms
        gpio_set_level(BSP_LCD_RST, 1);  // Deassert reset  
        vTaskDelay(pdMS_TO_TICKS(120));  // Wait 120ms for panel to initialize
        
        // Manual display initialization
        esp_lcd_panel_handle_t panel = NULL;
        esp_lcd_panel_io_handle_t io = NULL;
        
        bsp_display_config_t config = {0};
        ret = bsp_display_new(&config, &panel, &io);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Manual display initialization failed: %s", esp_err_to_name(ret));
            return;
        }
        
        ESP_LOGI(TAG, "Manual display panel created successfully");
        
        // Turn on the display
        ret = esp_lcd_panel_disp_on_off(panel, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
            return;
        }
        
        ESP_LOGI(TAG, "Display turned on successfully");
        
        // Turn on backlight with multiple strategies
        ret = bsp_display_backlight_on();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Display backlight turned on via BSP");
            
            ret = bsp_display_brightness_set(100);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Display backlight set to 100%");
            } else {
                ESP_LOGW(TAG, "Failed to set backlight brightness: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGW(TAG, "BSP backlight failed: %s", esp_err_to_name(ret));
            
            // Force backlight GPIO directly
            ESP_LOGI(TAG, "Forcing backlight GPIO directly...");
            gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
            gpio_set_level(BSP_LCD_BACKLIGHT, 1);  // Active high
            ESP_LOGI(TAG, "Backlight GPIO set to HIGH");
        }
        
        // Fill the screen with test pattern
        ESP_LOGI(TAG, "Drawing test pattern...");
        
        uint16_t *color_buffer = malloc(100 * 100 * sizeof(uint16_t));
        if (color_buffer) {
            // Red rectangle (top-left)
            for (int i = 0; i < 100 * 100; i++) {
                color_buffer[i] = 0xF800;  // Red in RGB565
            }
            ret = esp_lcd_panel_draw_bitmap(panel, 0, 0, 100, 100, color_buffer);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Red rectangle drawn successfully");
            } else {
                ESP_LOGE(TAG, "Failed to draw red rectangle: %s", esp_err_to_name(ret));
            }
            
            // Green rectangle (top-right)
            for (int i = 0; i < 100 * 100; i++) {
                color_buffer[i] = 0x07E0;  // Green in RGB565
            }
            ret = esp_lcd_panel_draw_bitmap(panel, 700, 0, 800, 100, color_buffer);
            ESP_LOGI(TAG, "Green rectangle result: %s", esp_err_to_name(ret));
            
            // Blue rectangle (bottom-left)
            for (int i = 0; i < 100 * 100; i++) {
                color_buffer[i] = 0x001F;  // Blue in RGB565
            }
            ret = esp_lcd_panel_draw_bitmap(panel, 0, 700, 100, 800, color_buffer);
            ESP_LOGI(TAG, "Blue rectangle result: %s", esp_err_to_name(ret));
            
            // White rectangle (center)
            for (int i = 0; i < 100 * 100; i++) {
                color_buffer[i] = 0xFFFF;  // White in RGB565
            }
            ret = esp_lcd_panel_draw_bitmap(panel, 350, 350, 450, 450, color_buffer);
            ESP_LOGI(TAG, "White rectangle result: %s", esp_err_to_name(ret));
            
            free(color_buffer);
        }
        
        ESP_LOGI(TAG, "Manual display test pattern complete");
        return;
    }
    
    ESP_LOGI(TAG, "BSP display initialized successfully!");
    
    // Create a simple colored screen using LVGL
    ESP_LOGI(TAG, "Creating LVGL test screen...");
    
    lv_obj_t *scr = lv_obj_create(NULL);
    if (scr != NULL) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x00FF00), 0);  // Green background
        lv_scr_load(scr);
        
        ESP_LOGI(TAG, "Green screen should be visible now!");
        
        // Add some text
        lv_obj_t *label = lv_label_create(scr);
        if (label != NULL) {
            lv_label_set_text(label, "ESP32-P4 Display Test\nHowdyScreen Working!");
            lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);  // White text
            lv_obj_center(label);
            ESP_LOGI(TAG, "Test text added to screen");
        }
        
        // Add a circle in the center (for round display)
        lv_obj_t *circle = lv_obj_create(scr);
        if (circle != NULL) {
            lv_obj_set_size(circle, 200, 200);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0xFF0000), 0);  // Red circle
            lv_obj_set_style_border_width(circle, 5, 0);
            lv_obj_set_style_border_color(circle, lv_color_hex(0xFFFFFF), 0);  // White border
            lv_obj_center(circle);
            ESP_LOGI(TAG, "Red circle added to center");
        }
    } else {
        ESP_LOGE(TAG, "Failed to create LVGL screen");
    }
    
    ESP_LOGI(TAG, "Display test complete!");
    ESP_LOGI(TAG, "Expected result: Green background with white text and red circle in center");
    
    // Keep the display alive and log status
    int counter = 0;
    while (true) {
        ESP_LOGI(TAG, "Display test running - Counter: %d, Free heap: %lu", 
                 counter++, esp_get_free_heap_size());
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Update every 5 seconds
    }
}