#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "HowdyDisplayWorking";

// Global display variable
static lv_display_t *g_disp = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 HowdyScreen Startup Beginning ===");
    
    // CRITICAL: Power stabilization delay
    ESP_LOGI(TAG, "Step 1/6: Power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Print basic system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Step 2/6: System Info - ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    
    // Step 3: Initialize display with extensive logging
    ESP_LOGI(TAG, "Step 3/6: Configuring BSP display...");
    
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        }
    };
    
    ESP_LOGI(TAG, "Calling bsp_display_start_with_config...");
    g_disp = bsp_display_start_with_config(&cfg);
    
    if (g_disp == NULL) {
        ESP_LOGE(TAG, "FAILED: bsp_display_start_with_config returned NULL!");
        
        // Try basic BSP display start
        ESP_LOGI(TAG, "Trying basic bsp_display_start...");
        g_disp = bsp_display_start();
        
        if (g_disp == NULL) {
            ESP_LOGE(TAG, "FAILED: bsp_display_start also returned NULL!");
            ESP_LOGE(TAG, "Display initialization completely failed. Check hardware connections.");
            return;
        }
    }
    
    ESP_LOGI(TAG, "SUCCESS: Display handle obtained: %p", g_disp);
    
    // Step 4: Backlight control with extensive logging
    ESP_LOGI(TAG, "Step 4/6: Configuring backlight...");
    ESP_LOGI(TAG, "Trying BSP backlight control...");
    
    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BSP backlight failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Attempting manual backlight control...");
        
        // Manual backlight - try both polarities
        gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
        
        ESP_LOGI(TAG, "Setting backlight LOW (active low)...");
        gpio_set_level(BSP_LCD_BACKLIGHT, 0); // Try active low first
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "If no display, will try HIGH in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ESP_LOGI(TAG, "Setting backlight HIGH (active high)...");
        gpio_set_level(BSP_LCD_BACKLIGHT, 1); // Try active high
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ESP_LOGI(TAG, "Back to LOW (most likely correct for this board)...");
        gpio_set_level(BSP_LCD_BACKLIGHT, 0); // Back to active low
        
    } else {
        ESP_LOGI(TAG, "SUCCESS: BSP backlight enabled");
    }
    
    // Step 5: Create very simple text display
    ESP_LOGI(TAG, "Step 5/6: Creating simple startup text...");
    ESP_LOGI(TAG, "Getting active screen...");
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        ESP_LOGE(TAG, "FAILED: lv_scr_act() returned NULL!");
        bsp_display_unlock();
        return;
    }
    ESP_LOGI(TAG, "Active screen obtained: %p", scr);
    
    // Set a simple background color
    ESP_LOGI(TAG, "Setting background to black...");
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    // Create very simple text
    ESP_LOGI(TAG, "Creating startup label...");
    lv_obj_t *label = lv_label_create(scr);
    if (label == NULL) {
        ESP_LOGE(TAG, "FAILED: lv_label_create returned NULL!");
        bsp_display_unlock();
        return;
    }
    
    lv_label_set_text(label, "HowdyScreen\nStarting...");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    ESP_LOGI(TAG, "Label created and positioned");
    
    // Unlock display
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Step 6/6: Startup screen should now be visible!");
    ESP_LOGI(TAG, "Expected: Black background with white 'HowdyScreen Starting...' text");
    
    // Add a blinking effect to show it's alive
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
        
        bsp_display_lock(0);
        if (i % 2 == 0) {
            lv_label_set_text(label, "HowdyScreen\nStarting...");
        } else {
            lv_label_set_text(label, "HowdyScreen\nStarting.");
        }
        bsp_display_unlock();
        
        ESP_LOGI(TAG, "Blink %d/20 - Display should be visible", i + 1);
    }
    
    // Keep the display alive and animate
    int counter = 0;
    while (true) {
        ESP_LOGI(TAG, "Display running - Counter: %d, Free heap: %lu", 
                 counter++, esp_get_free_heap_size());
        
        // Update counter on screen every 10 seconds
        if (counter % 50 == 0) {  // Every ~10 seconds
            bsp_display_lock(0);
            char counter_text[64];
            snprintf(counter_text, sizeof(counter_text), "Count: %d", counter / 5);
            lv_obj_t *counter_label = lv_label_create(scr);
            lv_label_set_text(counter_label, counter_text);
            lv_obj_set_style_text_color(counter_label, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_14, 0);
            lv_obj_align(counter_label, LV_ALIGN_BOTTOM_MID, 0, -20);
            bsp_display_unlock();
        }
        
        vTaskDelay(pdMS_TO_TICKS(200)); // Update every 200ms
    }
}