#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "HowdyScreen";

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS Wireless Microphone Starting...");
    ESP_LOGI(TAG, "Basic system loaded successfully!");
    ESP_LOGI(TAG, "cJSON Version: %s", cJSON_Version());
    
    while(1) {
        ESP_LOGI(TAG, "System running...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
