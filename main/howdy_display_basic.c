#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

static const char *TAG = "HowdyDisplayBasic";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 Basic Display Test Starting...");
    
    // CRITICAL: Power stabilization delay (from diagnostic guide)
    ESP_LOGI(TAG, "Waiting for power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second delay for power stabilization
    
    // Print basic system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    
    // Initialize I2C for display communication
    ESP_LOGI(TAG, "Initializing I2C...");
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C initialized successfully");
    
    // Initialize display brightness control
    ESP_LOGI(TAG, "Initializing display brightness control...");
    ret = bsp_display_brightness_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Display brightness init failed: %s", esp_err_to_name(ret));
        // Continue anyway
    } else {
        ESP_LOGI(TAG, "Display brightness control initialized");
    }
    
    // Initialize the display
    ESP_LOGI(TAG, "Initializing display...");
    
    // CRITICAL: Proper display reset sequence (from diagnostic guide)
    ESP_LOGI(TAG, "Performing display reset sequence...");
    gpio_set_direction(BSP_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_RST, 0);  // Assert reset
    vTaskDelay(pdMS_TO_TICKS(10));   // Hold reset for 10ms
    gpio_set_level(BSP_LCD_RST, 1);  // Deassert reset
    vTaskDelay(pdMS_TO_TICKS(120));  // Wait 120ms for panel to initialize
    
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    
    bsp_display_config_t config = {0};
    ret = bsp_display_new(&config, &panel, &io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Display panel created successfully");
    
    // Turn on the display
    ret = esp_lcd_panel_disp_on_off(panel, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Display turned on successfully");
    
    // Turn on backlight using BSP functions
    ret = bsp_display_backlight_on();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Display backlight turned on via BSP");
        
        // Set brightness to 100%
        ret = bsp_display_brightness_set(100);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Display backlight set to 100%");
        } else {
            ESP_LOGW(TAG, "Failed to set backlight brightness: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "BSP backlight failed: %s", esp_err_to_name(ret));
        
        // CRITICAL: Force backlight GPIO directly (from diagnostic guide)
        ESP_LOGI(TAG, "Forcing backlight GPIO directly...");
        gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
        gpio_set_level(BSP_LCD_BACKLIGHT, 1);  // Try active high first
        ESP_LOGI(TAG, "Backlight GPIO set to HIGH (active high attempt)");
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Also try active low in case the logic is inverted
        gpio_set_level(BSP_LCD_BACKLIGHT, 0);
        ESP_LOGI(TAG, "Backlight GPIO set to LOW (active low attempt)");
        
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Set back to high for final attempt
        gpio_set_level(BSP_LCD_BACKLIGHT, 1);
        ESP_LOGI(TAG, "Backlight GPIO final: HIGH");
    }
    
    // Fill the screen with a simple pattern to test display
    ESP_LOGI(TAG, "Drawing test pattern...");
    
    // Create a simple buffer for a small area (100x100 pixels)
    uint16_t *color_buffer = malloc(100 * 100 * sizeof(uint16_t));
    if (color_buffer) {
        // Fill with red color (RGB565 format: 0xF800)
        for (int i = 0; i < 100 * 100; i++) {
            color_buffer[i] = 0xF800;  // Red in RGB565
        }
        
        // Draw red rectangle in top-left corner
        ret = esp_lcd_panel_draw_bitmap(panel, 0, 0, 100, 100, color_buffer);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Red test rectangle drawn successfully");
        } else {
            ESP_LOGE(TAG, "Failed to draw test rectangle: %s", esp_err_to_name(ret));
        }
        
        // Fill with green color (RGB565 format: 0x07E0)  
        for (int i = 0; i < 100 * 100; i++) {
            color_buffer[i] = 0x07E0;  // Green in RGB565
        }
        
        // Draw green rectangle in top-right corner
        ret = esp_lcd_panel_draw_bitmap(panel, 700, 0, 800, 100, color_buffer);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Green test rectangle drawn successfully");
        } else {
            ESP_LOGE(TAG, "Failed to draw green rectangle: %s", esp_err_to_name(ret));
        }
        
        // Fill with blue color (RGB565 format: 0x001F)
        for (int i = 0; i < 100 * 100; i++) {
            color_buffer[i] = 0x001F;  // Blue in RGB565
        }
        
        // Draw blue rectangle in bottom-left corner  
        ret = esp_lcd_panel_draw_bitmap(panel, 0, 700, 100, 800, color_buffer);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Blue test rectangle drawn successfully");
        } else {
            ESP_LOGE(TAG, "Failed to draw blue rectangle: %s", esp_err_to_name(ret));
        }
        
        // Fill with white color for center (RGB565 format: 0xFFFF)
        for (int i = 0; i < 100 * 100; i++) {
            color_buffer[i] = 0xFFFF;  // White in RGB565
        }
        
        // Draw white rectangle in center
        ret = esp_lcd_panel_draw_bitmap(panel, 350, 350, 450, 450, color_buffer);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "White center rectangle drawn successfully");
        } else {
            ESP_LOGE(TAG, "Failed to draw white rectangle: %s", esp_err_to_name(ret));
        }
        
        free(color_buffer);
    } else {
        ESP_LOGE(TAG, "Failed to allocate color buffer");
    }
    
    ESP_LOGI(TAG, "Display test pattern complete!");
    ESP_LOGI(TAG, "If you see colored rectangles on the display, the hardware is working!");
    ESP_LOGI(TAG, "Expected: Red (top-left), Green (top-right), Blue (bottom-left), White (center)");
    
    // Simple loop to keep the system alive and log status
    int counter = 0;
    while (true) {
        ESP_LOGI(TAG, "Display test running - Counter: %d, Free heap: %lu", 
                 counter++, esp_get_free_heap_size());
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Update every 5 seconds
    }
}