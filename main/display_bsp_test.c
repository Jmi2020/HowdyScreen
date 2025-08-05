/*
 * BSP-based Display Test for ESP32-P4
 * Uses the Board Support Package initialization
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "lvgl.h"
#include "driver/gpio.h"

static const char *TAG = "DisplayBSPTest";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 BSP Display Test ===");
    
    // First ensure backlight is OFF during init
    gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_BACKLIGHT, 1); // OFF (active low)
    ESP_LOGI(TAG, "Backlight OFF during initialization");
    
    // Wait for power stabilization
    ESP_LOGI(TAG, "Waiting for power stabilization...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Try basic BSP display initialization
    ESP_LOGI(TAG, "Attempting bsp_display_start()...");
    lv_display_t *disp = bsp_display_start();
    
    if (disp == NULL) {
        ESP_LOGE(TAG, "FAILED: bsp_display_start() returned NULL!");
        
        // Try with configuration
        ESP_LOGI(TAG, "Trying bsp_display_start_with_config()...");
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
        
        disp = bsp_display_start_with_config(&cfg);
        
        if (disp == NULL) {
            ESP_LOGE(TAG, "FAILED: bsp_display_start_with_config() also returned NULL!");
            ESP_LOGE(TAG, "Display initialization completely failed.");
            
            // Still turn on backlight to see if anything appears
            ESP_LOGI(TAG, "Turning backlight ON anyway...");
            gpio_set_level(BSP_LCD_BACKLIGHT, 0); // ON
            
            while(1) {
                ESP_LOGE(TAG, "Display init failed - check hardware/BSP configuration");
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
            return;
        }
    }
    
    ESP_LOGI(TAG, "SUCCESS: Display initialized!");
    ESP_LOGI(TAG, "Display handle: %p", disp);
    
    // Get display info (LVGL 8.x API)
    ESP_LOGI(TAG, "Display initialized successfully");
    
    // Turn on backlight
    ESP_LOGI(TAG, "Turning backlight ON...");
    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BSP backlight control failed: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "Using manual backlight control...");
        gpio_set_level(BSP_LCD_BACKLIGHT, 0); // ON
    }
    
    // Lock display for drawing
    ESP_LOGI(TAG, "Creating test pattern...");
    if (bsp_display_lock(0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock display!");
        return;
    }
    
    // Get active screen using LVGL 8.x API
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        ESP_LOGE(TAG, "Failed to get active screen!");
        bsp_display_unlock();
        return;
    }
    
    // Create a simple test pattern
    // 1. Set background to red
    lv_obj_set_style_bg_color(scr, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    
    // 2. Create a white box in the center
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, 200, 200);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_border_width(box, 5, 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    
    // 3. Add text
    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, "ESP32-P4\nDisplay\nWorking!");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);
    
    // 4. Create color bars at the top
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_create(scr);
        lv_obj_set_size(bar, 200, 50);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, (i - 1.5) * 210, 10);
        
        lv_color_t colors[] = {
            lv_palette_main(LV_PALETTE_RED),
            lv_palette_main(LV_PALETTE_GREEN),
            lv_palette_main(LV_PALETTE_BLUE),
            lv_palette_main(LV_PALETTE_YELLOW)
        };
        lv_obj_set_style_bg_color(bar, colors[i], 0);
        lv_obj_set_style_border_width(bar, 0, 0);
    }
    
    // Unlock display
    bsp_display_unlock();
    
    ESP_LOGI(TAG, "Test pattern created!");
    ESP_LOGI(TAG, "Expected display:");
    ESP_LOGI(TAG, "  - Red background");
    ESP_LOGI(TAG, "  - White box in center with 'ESP32-P4 Display Working!' text");
    ESP_LOGI(TAG, "  - 4 color bars at top (Red, Green, Blue, Yellow)");
    
    // Keep updating display
    uint32_t counter = 0;
    while (1) {
        counter++;
        
        // Every 5 seconds, change something to show it's alive
        if (counter % 25 == 0) {
            bsp_display_lock(0);
            
            // Toggle background between red and blue
            static bool is_red = true;
            is_red = !is_red;
            lv_obj_set_style_bg_color(scr, 
                is_red ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_BLUE), 0);
            
            ESP_LOGI(TAG, "Changed background to %s", is_red ? "RED" : "BLUE");
            
            bsp_display_unlock();
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
        
        if (counter % 50 == 0) {
            ESP_LOGI(TAG, "Display test running... Counter: %lu, Free heap: %lu", 
                     counter/5, esp_get_free_heap_size());
        }
    }
}