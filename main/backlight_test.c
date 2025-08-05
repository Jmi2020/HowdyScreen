#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_chip_info.h"

static const char *TAG = "BacklightTest";

// Backlight pin from BSP - GPIO26
#define BACKLIGHT_GPIO 26

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 Backlight Test Starting ===");
    
    // Print system info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Configure backlight GPIO
    ESP_LOGI(TAG, "Configuring GPIO%d as output for backlight control", BACKLIGHT_GPIO);
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BACKLIGHT_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", BACKLIGHT_GPIO, esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "GPIO%d configured successfully", BACKLIGHT_GPIO);
    
    // Test both polarities
    ESP_LOGI(TAG, "Testing backlight polarities...");
    
    for (int cycle = 0; cycle < 10; cycle++) {
        ESP_LOGI(TAG, "Cycle %d/10:", cycle + 1);
        
        // Try LOW (active low - most likely correct)
        ESP_LOGI(TAG, "  Setting GPIO%d LOW (0) - Active Low Test", BACKLIGHT_GPIO);
        gpio_set_level(BACKLIGHT_GPIO, 0);
        ESP_LOGI(TAG, "  If backlight is ON now, this board uses ACTIVE LOW");
        vTaskDelay(pdMS_TO_TICKS(3000)); // 3 seconds
        
        // Try HIGH (active high)
        ESP_LOGI(TAG, "  Setting GPIO%d HIGH (1) - Active High Test", BACKLIGHT_GPIO);
        gpio_set_level(BACKLIGHT_GPIO, 1);
        ESP_LOGI(TAG, "  If backlight is ON now, this board uses ACTIVE HIGH");
        vTaskDelay(pdMS_TO_TICKS(3000)); // 3 seconds
        
        ESP_LOGI(TAG, "  Cycle %d complete. Watch the backlight behavior.", cycle + 1);
    }
    
    // Leave in most likely correct state (active low)
    ESP_LOGI(TAG, "Test complete. Setting to ACTIVE LOW (GPIO%d = 0)", BACKLIGHT_GPIO);
    gpio_set_level(BACKLIGHT_GPIO, 0);
    
    ESP_LOGI(TAG, "If you see the backlight turning on/off during this test,");
    ESP_LOGI(TAG, "the hardware is working and the issue is with display initialization.");
    ESP_LOGI(TAG, "If no backlight changes, check hardware connections or power supply.");
    
    // Keep alive
    while (1) {
        ESP_LOGI(TAG, "Backlight test running... GPIO%d = 0 (Active Low)", BACKLIGHT_GPIO);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}