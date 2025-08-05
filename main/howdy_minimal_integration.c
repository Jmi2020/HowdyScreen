#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_check.h" 
#include "nvs_flash.h"

// BSP and hardware components
#include "bsp/esp-bsp.h"  
#include "bsp/display.h"
#include "bsp/touch.h"

// Custom components
#include "audio_processor.h"

static const char *TAG = "HowdyMinimalIntegration";

// Thread-safe global state
static bool audio_muted = false;
static lv_obj_t *mute_button = NULL;
static lv_obj_t *audio_status_label = NULL;

// Thread safety protection
static SemaphoreHandle_t state_mutex = NULL;
static SemaphoreHandle_t ui_objects_mutex = NULL;

// Thread-safe getter/setter functions
static bool get_audio_muted_state(void)
{
    bool muted = false;
    if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        muted = audio_muted;
        xSemaphoreGive(state_mutex);
    }
    return muted;
}

static void set_audio_muted_state(bool muted)
{
    if (state_mutex && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        audio_muted = muted;
        xSemaphoreGive(state_mutex);
    }
}

// Thread-safe UI object access
static bool is_ui_ready(void)
{
    bool ready = false;
    if (ui_objects_mutex && xSemaphoreTake(ui_objects_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        ready = (mute_button != NULL && audio_status_label != NULL);
        xSemaphoreGive(ui_objects_mutex);
    }
    return ready;
}

static void set_ui_objects_ready(lv_obj_t *button, lv_obj_t *label)
{
    if (ui_objects_mutex && xSemaphoreTake(ui_objects_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        mute_button = button;
        audio_status_label = label;
        xSemaphoreGive(ui_objects_mutex);
    }
}

// Audio event callback
static void audio_event_handler(audio_event_t event, void *data, size_t len)
{
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            // Handle audio data - for now just log periodically
            static int audio_count = 0;
            if (++audio_count % 100 == 0) {  // Log every 100th audio event
                ESP_LOGI(TAG, "Audio data ready: %zu bytes (count: %d)", len, audio_count);
                
                // Update UI to show audio activity (thread-safe)
                if (is_ui_ready() && !get_audio_muted_state()) {
                    if (bsp_display_lock(10)) {  // 10ms timeout for non-blocking
                        lv_label_set_text(audio_status_label, "Capturing...");
                        lv_obj_set_style_text_color(audio_status_label, lv_color_hex(0x00FF00), 0);
                        bsp_display_unlock();
                    }
                }
            }
            break;
            
        case AUDIO_EVENT_STARTED:
            ESP_LOGI(TAG, "Audio processing started");
            break;
            
        case AUDIO_EVENT_STOPPED:
            ESP_LOGI(TAG, "Audio processing stopped");
            break;
            
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio error occurred");
            break;
    }
}

// Touch event handler for mute button
static void mute_button_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        // Thread-safe state toggle
        bool new_muted_state = !get_audio_muted_state();
        set_audio_muted_state(new_muted_state);
        
        // Update button appearance and label
        if (!bsp_display_lock(100)) {  // 100ms timeout for UI updates
            ESP_LOGW(TAG, "Failed to acquire display lock for mute button update");
            return;
        }
        if (new_muted_state) {
            lv_label_set_text(lv_obj_get_child(mute_button, 0), "Unmute");
            lv_obj_set_style_bg_color(mute_button, lv_color_hex(0xFF0000), 0);
            lv_label_set_text(audio_status_label, "Audio Muted");
            lv_obj_set_style_text_color(audio_status_label, lv_color_hex(0xFF0000), 0);
            ESP_LOGI(TAG, "Audio muted by user");
        } else {
            lv_label_set_text(lv_obj_get_child(mute_button, 0), "Mute");
            lv_obj_set_style_bg_color(mute_button, lv_color_hex(0x0080FF), 0);
            lv_label_set_text(audio_status_label, "Audio Active");
            lv_obj_set_style_text_color(audio_status_label, lv_color_hex(0x00FF00), 0);
            ESP_LOGI(TAG, "Audio unmuted by user");
        }
        bsp_display_unlock();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 HowdyScreen Minimal Integration Starting ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize thread safety mechanisms
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        goto error_loop;
    }
    
    ui_objects_mutex = xSemaphoreCreateMutex();
    if (ui_objects_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create UI objects mutex");
        goto error_loop;
    }
    
    ESP_LOGI(TAG, "Thread safety mutexes initialized successfully");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Phase 1: Initialize display with improved buffer size
    ESP_LOGI(TAG, "Phase 1: Initializing display subsystem...");
    
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 20, // 20 lines (40KB) - DMA capable internal memory
        .double_buffer = false,  // Single buffer to save memory
        .flags = {
            .buff_dma = true,     // Required for hardware acceleration
            .buff_spiram = false, // DMA buffers must be in internal memory
            .sw_rotate = false,
        }
    };
    
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        goto error_loop;
    }
    
    // Enable backlight
    ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable backlight: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Display initialized successfully with %d KB LVGL buffer in internal memory", 
             (BSP_LCD_H_RES * 20 * 2) / 1024);
    
    // Create startup screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x001122), 0);  // Dark blue background
    
    // Disable scrolling for the entire screen (important for touch)
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    
    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "HowdyScreen\\nMinimal Integration");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    // Use default font - lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 50);
    
    lv_obj_t *status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Display: Ready\\nAudio: Initializing...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
    
    // Phase 2: Initialize audio subsystem
    ESP_LOGI(TAG, "Phase 2: Initializing audio subsystem...");
    
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,
        .channels = 1,
        .bits_per_sample = 16,
        .dma_buf_count = 8,
        .dma_buf_len = 320,  // 20ms at 16kHz
        .task_priority = 20,
        .task_core = 1
    };
    
    ret = audio_processor_init(&audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio processor init failed: %s", esp_err_to_name(ret));
        
        // Update display to show audio failed
        bsp_display_lock(0);
        lv_label_set_text(status_label, "Display: Ready\\nAudio: FAILED");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        bsp_display_unlock();
        
        // Continue without audio
    } else {
        // Set audio callback
        ret = audio_processor_set_callback(audio_event_handler);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio callback set failed: %s", esp_err_to_name(ret));
        }
        
        // Start audio capture
        ret = audio_processor_start_capture();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Audio capture start failed: %s", esp_err_to_name(ret));
            
            // Update display
            bsp_display_lock(0);
            lv_label_set_text(status_label, "Display: Ready\\nAudio: Start Failed");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
            bsp_display_unlock();
        } else {
            ESP_LOGI(TAG, "Audio subsystem initialized successfully");
            
            // Update display
            bsp_display_lock(0);
            lv_label_set_text(status_label, "Display: Ready\\nAudio: Ready");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
            bsp_display_unlock();
        }
    }
    
    ESP_LOGI(TAG, "=== System Initialization Complete ===");
    
    // Add a runtime counter to show the system is alive
    lv_obj_t *counter_label = lv_label_create(scr);
    lv_obj_set_style_text_color(counter_label, lv_color_hex(0x00AAFF), 0);
    lv_obj_align(counter_label, LV_ALIGN_BOTTOM_MID, 0, -50);
    
    lv_obj_t *memory_label = lv_label_create(scr);
    lv_obj_set_style_text_color(memory_label, lv_color_hex(0xAAFFAA), 0);
    // lv_obj_set_style_text_font(memory_label, &lv_font_montserrat_14, 0);
    lv_obj_align(memory_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Create interactive mute button
    ESP_LOGI(TAG, "Creating interactive touch button...");
    lv_obj_t *temp_mute_button = lv_btn_create(scr);
    lv_obj_set_size(temp_mute_button, 200, 60);
    lv_obj_align(temp_mute_button, LV_ALIGN_CENTER, 0, 80);
    lv_obj_add_event_cb(temp_mute_button, mute_button_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(temp_mute_button, lv_color_hex(0x0080FF), 0);
    
    lv_obj_t *mute_label = lv_label_create(temp_mute_button);
    lv_label_set_text(mute_label, "Mute");
    lv_obj_center(mute_label);
    
    // Create audio status label
    lv_obj_t *temp_audio_status_label = lv_label_create(scr);
    lv_label_set_text(temp_audio_status_label, "Audio Active");
    lv_obj_set_style_text_color(temp_audio_status_label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(temp_audio_status_label, LV_ALIGN_CENTER, 0, 150);
    
    // Thread-safe assignment of UI objects
    set_ui_objects_ready(temp_mute_button, temp_audio_status_label);
    ESP_LOGI(TAG, "UI objects registered with thread-safe access");
    
    // Get touch input device from BSP
    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (touch_indev) {
        ESP_LOGI(TAG, "Touch input device ready");
    } else {
        ESP_LOGW(TAG, "Touch input device not available");
    }
    
    // Main application loop
    int counter = 0;
    while (true) {
        // Update system status every 2 seconds
        if (counter % 20 == 0) {
            size_t free_heap = esp_get_free_heap_size();
            
            ESP_LOGI(TAG, "System running - Counter: %d, Free heap: %zu bytes", 
                     counter / 10, free_heap);
            
            // Update display counter and memory (thread-safe)
            if (bsp_display_lock(50)) {  // 50ms timeout for periodic updates
                char counter_text[64];
                snprintf(counter_text, sizeof(counter_text), "Runtime: %d seconds", counter / 10);
                lv_label_set_text(counter_label, counter_text);
                
                char memory_text[64];
                snprintf(memory_text, sizeof(memory_text), "Free Heap: %zu KB", free_heap / 1024);
                lv_label_set_text(memory_label, memory_text);
                
                bsp_display_unlock();
            } else {
                ESP_LOGW(TAG, "Failed to acquire display lock for status update");
            }
            
            // Check system health
            if (free_heap < 50000) {  // Less than 50KB free
                ESP_LOGW(TAG, "Low memory warning: %zu bytes free", free_heap);
            }
        }
        
        counter++;
        vTaskDelay(pdMS_TO_TICKS(10));  // 10ms loop for responsive touch
    }

error_loop:
    ESP_LOGE(TAG, "Application entered error state");
    
    // Cleanup thread safety resources
    if (state_mutex != NULL) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
        ESP_LOGI(TAG, "State mutex cleaned up");
    }
    
    if (ui_objects_mutex != NULL) {
        vSemaphoreDelete(ui_objects_mutex);
        ui_objects_mutex = NULL;
        ESP_LOGI(TAG, "UI objects mutex cleaned up");
    }
    
    while (true) {
        ESP_LOGE(TAG, "System in error state - check logs above for details");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}