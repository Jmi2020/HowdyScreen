#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

// BSP and Display
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

// Audio
#include "audio_processor.h"

// UI and State Management
#include "ui_manager.h"

static const char *TAG = "HowdyScreen";

// Event group for synchronization
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// System state
typedef enum {
    HOWDY_STATE_INIT,
    HOWDY_STATE_IDLE,
    HOWDY_STATE_LISTENING,
    HOWDY_STATE_PROCESSING,
    HOWDY_STATE_SPEAKING,
    HOWDY_STATE_ERROR
} howdy_state_t;

static howdy_state_t current_state = HOWDY_STATE_INIT;

// Function prototypes
static void howdy_display_init(void);
static void howdy_touch_init(void);
static void howdy_wifi_init(void);
static void howdy_audio_init(void);
static void howdy_main_task(void *pvParameters);

static void howdy_display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");
    
    // Initialize the BSP
    ESP_ERROR_CHECK(bsp_display_start());
    
    // Initialize LVGL port
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    
    // Add display to LVGL
    lv_disp_t * disp = bsp_display_get_disp();
    if (disp != NULL) {
        ESP_LOGI(TAG, "Display initialized successfully - 800x800");
    } else {
        ESP_LOGE(TAG, "Failed to initialize display");
    }
}

static void howdy_touch_init(void)
{
    ESP_LOGI(TAG, "Initializing touch interface...");
    
    // Initialize touch controller
    ESP_ERROR_CHECK(bsp_touch_start());
    
    // Add touch input to LVGL
    lv_indev_t * touch_indev = bsp_touch_get_indev();
    if (touch_indev != NULL) {
        ESP_LOGI(TAG, "Touch controller initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize touch controller");
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void howdy_wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi via ESP32-C6...");
    
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with remote configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YourWiFiNetwork",      // TODO: Make configurable
            .password = "YourPassword",     // TODO: Make configurable
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization complete");
}

static void howdy_audio_init(void)
{
    ESP_LOGI(TAG, "Initializing audio pipeline...");
    
    // Initialize audio processor component
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,
        .bits_per_sample = 16,
        .channels = 1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .task_priority = CONFIG_HOWDY_AUDIO_TASK_PRIORITY,
        .task_core = 1
    };
    
    ESP_ERROR_CHECK(audio_processor_init(&audio_config));
    ESP_LOGI(TAG, "Audio pipeline initialized");
}

static void howdy_main_task(void *pvParameters)
{
    ESP_LOGI(TAG, "HowdyTTS main task started");
    
    while (1) {
        switch (current_state) {
            case HOWDY_STATE_INIT:
                ESP_LOGI(TAG, "System initializing...");
                // Wait for WiFi connection
                EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                      WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                      pdFALSE,
                                                      pdFALSE,
                                                      portMAX_DELAY);
                
                if (bits & WIFI_CONNECTED_BIT) {
                    ESP_LOGI(TAG, "Connected to WiFi, transitioning to IDLE");
                    current_state = HOWDY_STATE_IDLE;
                    ui_manager_set_state(UI_STATE_IDLE);
                } else if (bits & WIFI_FAIL_BIT) {
                    ESP_LOGE(TAG, "Failed to connect to WiFi");
                    current_state = HOWDY_STATE_ERROR;
                    ui_manager_set_state(UI_STATE_ERROR);
                }
                break;
                
            case HOWDY_STATE_IDLE:
                // Wait for touch or voice activation
                // This will be handled by touch and audio callbacks
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case HOWDY_STATE_LISTENING:
                ESP_LOGI(TAG, "Listening for audio...");
                // Audio capture is handled by audio processor
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case HOWDY_STATE_PROCESSING:
                ESP_LOGI(TAG, "Processing audio on HowdyTTS server...");
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case HOWDY_STATE_SPEAKING:
                ESP_LOGI(TAG, "Playing TTS response...");
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case HOWDY_STATE_ERROR:
                ESP_LOGE(TAG, "System in error state");
                vTaskDelay(pdMS_TO_TICKS(1000));
                // Attempt recovery
                current_state = HOWDY_STATE_INIT;
                break;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "HowdyTTS ESP32-P4 Screen starting...");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    
    // Initialize all subsystems
    howdy_display_init();
    howdy_touch_init();
    howdy_wifi_init();
    howdy_audio_init();
    
    // Initialize UI manager
    ESP_ERROR_CHECK(ui_manager_init());
    
    // Create main application task
    xTaskCreatePinnedToCore(
        howdy_main_task,
        "howdy_main",
        8192,
        NULL,
        20,
        NULL,
        0  // Pin to core 0 (core 1 for audio)
    );
    
    ESP_LOGI(TAG, "HowdyTTS initialization complete");
}