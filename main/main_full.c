#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_clk_tree.h"
#include "nvs_flash.h"

#include "howdy_config.h"
#include "audio_pipeline.h"
#include "network_manager.h"
#include "display_manager.h"
#include "led_controller.h"
#include "server_discovery.h"
#include "wifi_provisioning.h"

static const char *TAG = "HowdyTTS";

// Fallback server IPs - will try these in order if mDNS fails
static const char *FALLBACK_SERVERS[] = {
    "192.168.1.100",   // Home network
    "192.168.0.100",   // Alternative home network
    "10.0.0.100",      // Some routers use 10.x.x.x
    "172.16.0.100"     // Another common range
};
#define FALLBACK_SERVER_COUNT (sizeof(FALLBACK_SERVERS) / sizeof(FALLBACK_SERVERS[0]))

// Global system components
static audio_pipeline_t g_audio_pipeline;
static network_manager_t g_network_manager;
static display_manager_t g_display_manager;
static led_controller_t g_led_controller;
static server_discovery_t g_server_discovery;
static wifi_provision_config_t g_wifi_config;

// Processing state tracking
static bool g_voice_active = false;
static bool g_awaiting_response = false;
static TickType_t g_last_voice_time = 0;

// Task handles
static TaskHandle_t audio_task_handle = NULL;
static TaskHandle_t network_task_handle = NULL;
static TaskHandle_t display_task_handle = NULL;
static TaskHandle_t led_task_handle = NULL;

// Inter-task communication
static QueueHandle_t audio_data_queue = NULL;
static SemaphoreHandle_t display_mutex = NULL;

typedef struct {
    audio_analysis_t analysis;
    int16_t audio_samples[FRAME_SIZE];
    size_t sample_count;
} audio_message_t;

// Audio processing task - Core 0 (high priority, real-time)
static void audio_task(void *param) {
    ESP_LOGI(TAG, "Audio task started on core %d", xPortGetCoreID());
    
    int16_t audio_buffer[FRAME_SIZE];
    audio_message_t audio_msg;
    
    while (1) {
        // Read audio from microphone
        size_t samples_read = audio_pipeline_read(&g_audio_pipeline, audio_buffer, FRAME_SIZE);
        
        if (samples_read > 0 && !display_is_muted(&g_display_manager)) {
            // Analyze audio for visualization
            audio_analyze_buffer(audio_buffer, samples_read, &audio_msg.analysis);
            
            // Copy samples for network transmission
            memcpy(audio_msg.audio_samples, audio_buffer, samples_read * sizeof(int16_t));
            audio_msg.sample_count = samples_read;
            
            // Send to other tasks (non-blocking)
            xQueueSend(audio_data_queue, &audio_msg, 0);
            
            // Stream audio to network
            if (network_manager_get_state(&g_network_manager) == NETWORK_STATE_CONNECTED) {
                network_send_audio(&g_network_manager, audio_buffer, samples_read);
                
                // Check voice activity for processing animation
                audio_analysis_t analysis;
                audio_analyze_buffer(audio_buffer, samples_read, &analysis);
                
                if (analysis.voice_detected && !g_voice_active) {
                    // Voice started
                    g_voice_active = true;
                    g_last_voice_time = xTaskGetTickCount();
                } else if (!analysis.voice_detected && g_voice_active) {
                    // Voice ended - check if enough silence passed
                    if ((xTaskGetTickCount() - g_last_voice_time) > pdMS_TO_TICKS(500)) {
                        g_voice_active = false;
                        g_awaiting_response = true;
                        // Show processing animation
                        display_show_processing(&g_display_manager, true);
                    }
                } else if (analysis.voice_detected) {
                    g_last_voice_time = xTaskGetTickCount();
                }
            }
        }
        
        // Check for incoming audio from network
        if (network_manager_get_state(&g_network_manager) == NETWORK_STATE_CONNECTED) {
            int16_t incoming_audio[FRAME_SIZE];
            int frames_received = network_receive_audio(&g_network_manager, incoming_audio, FRAME_SIZE);
            
            if (frames_received > 0) {
                // Play received audio through speaker
                audio_pipeline_write(&g_audio_pipeline, incoming_audio, frames_received);
                
                // Hide processing animation when response is received
                if (g_awaiting_response) {
                    g_awaiting_response = false;
                    display_show_processing(&g_display_manager, false);
                }
            }
        }
        
        // Small delay to prevent watchdog issues
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Network management task - Core 1
static void network_task(void *param) {
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());
    
    // Connect to WiFi
    esp_err_t ret = network_manager_connect(&g_network_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
    
    uint32_t status_update_counter = 0;
    
    while (1) {
        // Update network status display every 5 seconds
        if (status_update_counter++ % 5000 == 0) {
            network_state_t state = network_manager_get_state(&g_network_manager);
            int rssi = (state == NETWORK_STATE_CONNECTED) ? network_get_rssi() : -100;
            
            if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                display_update_network_status(&g_display_manager, state, rssi);
                xSemaphoreGive(display_mutex);
            }
        }
        
        // Handle network reconnection if needed
        if (network_manager_get_state(&g_network_manager) == NETWORK_STATE_ERROR) {
            ESP_LOGW(TAG, "Network error detected, attempting reconnection...");
            vTaskDelay(pdMS_TO_TICKS(5000));  // Wait before retry
            network_manager_connect(&g_network_manager);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Display update task - Core 1
static void display_task(void *param) {
    ESP_LOGI(TAG, "Display task started on core %d", xPortGetCoreID());
    
    audio_message_t audio_msg;
    TickType_t last_update = xTaskGetTickCount();
    
    while (1) {
        // Update display at ~60 FPS
        TickType_t now = xTaskGetTickCount();
        if (now - last_update >= pdMS_TO_TICKS(16)) {  // ~60 FPS
            
            // Check for new audio data
            if (xQueueReceive(audio_data_queue, &audio_msg, 0) == pdTRUE) {
                if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    display_update_audio_level(&g_display_manager, &audio_msg.analysis);
                    xSemaphoreGive(display_mutex);
                }
            }
            
            // Handle LVGL timer
            if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                display_task_handler(&g_display_manager);
                xSemaphoreGive(display_mutex);
            }
            
            last_update = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// LED animation task - Core 1
static void led_task(void *param) {
    ESP_LOGI(TAG, "LED task started on core %d", xPortGetCoreID());
    
    audio_message_t audio_msg;
    TickType_t last_animation = xTaskGetTickCount();
    
    while (1) {
        // Update LEDs at ~120 FPS for smooth animations
        TickType_t now = xTaskGetTickCount();
        if (now - last_animation >= pdMS_TO_TICKS(8)) {  // ~120 FPS
            
            // Check for new audio data
            if (xQueuePeek(audio_data_queue, &audio_msg, 0) == pdTRUE) {
                if (!display_is_muted(&g_display_manager)) {
                    led_controller_update_audio(&g_led_controller, &audio_msg.analysis);
                } else {
                    led_controller_clear(&g_led_controller);
                }
            } else {
                // No audio data, run regular animation
                led_controller_update_animation(&g_led_controller);
            }
            
            last_animation = now;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// System monitoring task
static void monitor_task(void *param) {
    ESP_LOGI(TAG, "Monitor task started");
    
    while (1) {
        // Print system stats every 30 seconds
        ESP_LOGI(TAG, "=== System Status ===");
        ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
        uint32_t cpu_freq_hz = 0;
        esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &cpu_freq_hz);
        ESP_LOGI(TAG, "CPU frequency: %lu MHz", cpu_freq_hz / 1000000);
        
        // Task stack monitoring
        if (audio_task_handle) {
            ESP_LOGI(TAG, "Audio task free stack: %d", uxTaskGetStackHighWaterMark(audio_task_handle));
        }
        if (display_task_handle) {
            ESP_LOGI(TAG, "Display task free stack: %d", uxTaskGetStackHighWaterMark(display_task_handle));
        }
        
        // Network status
        network_state_t net_state = network_manager_get_state(&g_network_manager);
        ESP_LOGI(TAG, "Network state: %d", net_state);
        if (net_state == NETWORK_STATE_CONNECTED) {
            ESP_LOGI(TAG, "WiFi RSSI: %d dBm", network_get_rssi());
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "HowdyTTS Wireless Microphone Starting...");
    ESP_LOGI(TAG, "ESP32-P4 @ %lu MHz", esp_clk_cpu_freq() / 1000000);
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create inter-task communication primitives
    audio_data_queue = xQueueCreate(10, sizeof(audio_message_t));
    display_mutex = xSemaphoreCreateMutex();
    
    if (!audio_data_queue || !display_mutex) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        return;
    }
    
    // Initialize hardware components
    ESP_LOGI(TAG, "Initializing hardware components...");
    
    // Initialize audio pipeline
    ret = audio_pipeline_init(&g_audio_pipeline);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio pipeline: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize server discovery
    ret = server_discovery_init(&g_server_discovery, FALLBACK_SERVERS, FALLBACK_SERVER_COUNT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize server discovery: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize network manager with first fallback server
    ret = network_manager_init(&g_network_manager, WIFI_SSID, WIFI_PASSWORD, FALLBACK_SERVERS[0], UDP_PORT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize display manager
    ret = display_manager_init(&g_display_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize LED controller
    ret = led_controller_init(&g_led_controller);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED controller: %s", esp_err_to_name(ret));
        return;
    }
    
    // Create UI
    ret = display_create_ui(&g_display_manager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create UI: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start audio pipeline
    ret = audio_pipeline_start(&g_audio_pipeline);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio pipeline: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Hardware initialization complete");
    
    // Set initial status
    display_set_status(&g_display_manager, "Ready", lv_color_hex(0x34a853));
    
    // Create tasks with core affinity
    ESP_LOGI(TAG, "Creating tasks...");
    
    // Audio task - Core 0 (real-time audio processing)
    xTaskCreatePinnedToCore(audio_task, "audio_task", AUDIO_TASK_STACK_SIZE, NULL, 
                           AUDIO_TASK_PRIORITY, &audio_task_handle, 0);
    
    // Network task - Core 1
    xTaskCreatePinnedToCore(network_task, "network_task", NETWORK_TASK_STACK_SIZE, NULL,
                           NETWORK_TASK_PRIORITY, &network_task_handle, 1);
    
    // Display task - Core 1
    xTaskCreatePinnedToCore(display_task, "display_task", DISPLAY_TASK_STACK_SIZE, NULL,
                           DISPLAY_TASK_PRIORITY, &display_task_handle, 1);
    
    // LED task - Core 1  
    xTaskCreatePinnedToCore(led_task, "led_task", LED_TASK_STACK_SIZE, NULL,
                           LED_TASK_PRIORITY, &led_task_handle, 1);
    
    // System monitor task - Core 1 (low priority)
    xTaskCreatePinnedToCore(monitor_task, "monitor_task", 2048, NULL, 1, NULL, 1);
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "HowdyTTS Wireless Microphone is running!");
    
    // Main task can now idle - all work is done in other tasks
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}