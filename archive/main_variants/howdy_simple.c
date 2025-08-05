#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

static const char *TAG = "HowdyScreen";

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS ESP32-P4 Screen starting (Simple Test)...");
    
    // Print system information
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "This is %s chip with %d CPU cores, silicon revision v%d.%d", 
             CONFIG_IDF_TARGET,
             chip_info.cores, 
             chip_info.revision / 100, 
             chip_info.revision % 100);
    
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Get flash size failed");
        return;
    }
    
    ESP_LOGI(TAG, "%lu MB %s flash", 
             flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    
    ESP_LOGI(TAG, "Minimum free heap size: %lu bytes", esp_get_minimum_free_heap_size());
    
    // Simple task loop
    int counter = 0;
    while (1) {
        ESP_LOGI(TAG, "HowdyScreen running... Counter: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        if (counter > 20) {
            ESP_LOGI(TAG, "Basic test completed - ESP32-P4 is working correctly");
            break;
        }
    }
    
    ESP_LOGI(TAG, "HowdyScreen test completed");
}