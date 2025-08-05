#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"

static const char *TAG = "MinimalTest";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-P4 Minimal Test Starting...");
    
    // Print basic system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    ESP_LOGI(TAG, "Device is alive! Board: ESP32-P4-WIFI6-Touch-LCD-3.4C");
    
    // Simple alive counter
    int counter = 0;
    while (true) {
        ESP_LOGI(TAG, "Alive counter: %d", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}