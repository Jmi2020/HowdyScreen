#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "nvs_flash.h"

// BSP and hardware components
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

// Custom components
#include "audio_processor.h"
#include "ui_manager.h"
#include "websocket_client.h"
#include "service_discovery.h"
#include "network_manager.h"

static const char *TAG = "HowdyVoiceAssistant";

// Application state machine
typedef enum {
    APP_STATE_INIT,
    APP_STATE_DISPLAY_INIT,
    APP_STATE_NETWORK_INIT,
    APP_STATE_DISCOVERING_SERVER,
    APP_STATE_CONNECTING_SERVER,
    APP_STATE_READY,
    APP_STATE_LISTENING,
    APP_STATE_PROCESSING,
    APP_STATE_SPEAKING,
    APP_STATE_ERROR
} app_state_t;

// Application context structure
typedef struct {
    // Component handles
    ui_manager_t ui;
    network_manager_t network;
    
    // System state
    app_state_t state;
    SemaphoreHandle_t state_mutex;
    EventGroupHandle_t system_events;
    
    // Configuration
    char wifi_ssid[32];
    char wifi_password[64];
    char server_ip[16];
    uint16_t server_port;
    
    // System status
    bool display_ready;
    bool network_ready;
    bool audio_ready;
    bool server_connected;
} howdy_app_t;

// Event bits for system coordination
#define DISPLAY_READY_BIT   BIT0
#define NETWORK_READY_BIT   BIT1
#define AUDIO_READY_BIT     BIT2
#define SERVER_READY_BIT    BIT3

static howdy_app_t s_app = {0};

// Forward declarations
static esp_err_t app_transition_to(app_state_t new_state);
static void audio_event_handler(audio_event_t event, void *data, size_t len);
static void websocket_event_handler(ws_client_state_t state, ws_message_type_t msg_type, const uint8_t *data, size_t len);

// Application state transition with thread safety
static esp_err_t app_transition_to(app_state_t new_state)
{
    if (xSemaphoreTake(s_app.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take state mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    app_state_t old_state = s_app.state;
    s_app.state = new_state;
    
    xSemaphoreGive(s_app.state_mutex);
    
    ESP_LOGI(TAG, "State transition: %d -> %d", old_state, new_state);
    
    // Update UI state based on application state
    switch (new_state) {
        case APP_STATE_INIT:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_INIT);
            }
            break;
        case APP_STATE_NETWORK_INIT:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_CONNECTING);
            }
            break;
        case APP_STATE_READY:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_IDLE);
            }
            break;
        case APP_STATE_LISTENING:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_LISTENING);
            }
            break;
        case APP_STATE_PROCESSING:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_PROCESSING);
            }
            break;
        case APP_STATE_SPEAKING:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_SPEAKING);
            }
            break;
        case APP_STATE_ERROR:
            if (s_app.display_ready) {
                ui_manager_set_state(&s_app.ui, UI_STATE_ERROR);
            }
            break;
    }
    
    return ESP_OK;
}

// Audio event callback - handles audio data and events
static void audio_event_handler(audio_event_t event, void *data, size_t len, void *ctx)
{
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            // Update UI with audio level
            if (s_app.display_ready && data && len > 0) {
                // Calculate RMS level for visual feedback
                int16_t *samples = (int16_t*)data;
                size_t sample_count = len / sizeof(int16_t);
                float level = 0.0f;
                
                for (size_t i = 0; i < sample_count; i++) {
                    float sample = (float)samples[i] / 32768.0f;
                    level += sample * sample;
                }
                level = sqrtf(level / sample_count);
                
                ui_manager_update_audio_level(&s_app.ui, level);
            }
            
            // Send audio data to server if connected
            if (s_app.server_connected && s_app.state == APP_STATE_LISTENING) {
                if (data && len > 0) {
                    int16_t *audio_samples = (int16_t*)data;
                    size_t sample_count = len / sizeof(int16_t);
                    
                    esp_err_t ret = ws_client_send_binary_audio(s_app.websocket, 
                                                              audio_samples, 
                                                              sample_count);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to send audio data: %s", esp_err_to_name(ret));
                    }
                }
            }
            break;
            
        case AUDIO_EVENT_VOICE_START:
            ESP_LOGI(TAG, "Voice activity detected");
            app_transition_to(APP_STATE_LISTENING);
            break;
            
        case AUDIO_EVENT_VOICE_END:
            ESP_LOGI(TAG, "Voice activity ended");
            if (s_app.state == APP_STATE_LISTENING) {
                app_transition_to(APP_STATE_PROCESSING);
            }
            break;
            
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio error occurred");
            app_transition_to(APP_STATE_ERROR);
            break;
    }
}

// WebSocket event callback - handles server communication
static void websocket_event_handler(ws_event_t event, void *data, size_t len, void *ctx)
{
    switch (event) {
        case WS_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to HowdyTTS server");
            s_app.server_connected = true;
            xEventGroupSetBits(s_app.system_events, SERVER_READY_BIT);
            app_transition_to(APP_STATE_READY);
            break;
            
        case WS_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected from server");
            s_app.server_connected = false;
            xEventGroupClearBits(s_app.system_events, SERVER_READY_BIT);
            app_transition_to(APP_STATE_ERROR);
            break;
            
        case WS_EVENT_DATA_RECEIVED:
            ESP_LOGI(TAG, "Received response from HowdyTTS server");
            app_transition_to(APP_STATE_SPEAKING);
            
            // TODO: Handle TTS audio playback
            // For now, just transition back to ready after a delay
            vTaskDelay(pdMS_TO_TICKS(2000));
            app_transition_to(APP_STATE_READY);
            break;
            
        case WS_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error occurred");
            s_app.server_connected = false;
            app_transition_to(APP_STATE_ERROR);
            break;
    }
}

// UI event callback - handles user interactions
static void ui_event_handler(ui_event_t event, void *data, void *ctx)
{
    switch (event) {
        case UI_EVENT_MUTE_TOGGLE:
            ESP_LOGI(TAG, "User toggled mute");
            // TODO: Implement mute functionality
            break;
            
        case UI_EVENT_WAKE_WORD:
            ESP_LOGI(TAG, "Wake word detected via UI");
            if (s_app.state == APP_STATE_READY) {
                app_transition_to(APP_STATE_LISTENING);
            }
            break;
    }
}

// Initialize display subsystem
static esp_err_t init_display_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing display subsystem...");
    
    // Configure BSP display
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
    
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to start BSP display");
        return ESP_FAIL;
    }
    
    // Enable backlight
    esp_err_t ret = bsp_display_backlight_on();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable backlight: %s", esp_err_to_name(ret));
    }
    
    // Initialize UI manager
    ui_manager_config_t ui_config = {
        .display = disp,
        .event_callback = ui_event_handler,
        .callback_ctx = &s_app
    };
    
    ESP_RETURN_ON_ERROR(ui_manager_init(&s_app.ui, &ui_config), TAG, "UI manager init failed");
    
    s_app.display_ready = true;
    xEventGroupSetBits(s_app.system_events, DISPLAY_READY_BIT);
    
    ESP_LOGI(TAG, "Display subsystem initialized successfully");
    return ESP_OK;
}

// Initialize network subsystem
static esp_err_t init_network_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing network subsystem...");
    
    // Initialize network manager
    ESP_RETURN_ON_ERROR(network_manager_init(&s_app.network, 
                                           s_app.wifi_ssid, 
                                           s_app.wifi_password,
                                           s_app.server_ip,
                                           s_app.server_port), 
                       TAG, "Network manager init failed");
    
    // Connect to WiFi
    ESP_RETURN_ON_ERROR(network_manager_connect(&s_app.network), TAG, "WiFi connection failed");
    
    s_app.network_ready = true;
    xEventGroupSetBits(s_app.system_events, NETWORK_READY_BIT);
    
    ESP_LOGI(TAG, "Network subsystem initialized successfully");
    return ESP_OK;
}

// Initialize audio subsystem
static esp_err_t init_audio_subsystem(void)
{
    ESP_LOGI(TAG, "Initializing audio subsystem...");
    
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,
        .channels = 1,
        .bits_per_sample = 16,
        .frame_size = 320,  // 20ms at 16kHz
        .task_priority = 20,
        .task_core = 1,
        .event_callback = audio_event_handler,
        .callback_ctx = &s_app
    };
    
    ESP_RETURN_ON_ERROR(audio_processor_init(&s_app.audio, &audio_config), 
                       TAG, "Audio processor init failed");
    
    ESP_RETURN_ON_ERROR(audio_processor_start(&s_app.audio), TAG, "Audio processor start failed");
    
    s_app.audio_ready = true;
    xEventGroupSetBits(s_app.system_events, AUDIO_READY_BIT);
    
    ESP_LOGI(TAG, "Audio subsystem initialized successfully");
    return ESP_OK;
}

// Discover and connect to HowdyTTS server
static esp_err_t init_server_connection(void)
{
    ESP_LOGI(TAG, "Discovering HowdyTTS servers...");
    
    // Start service discovery
    ESP_RETURN_ON_ERROR(service_discovery_init(), TAG, "Service discovery init failed");
    ESP_RETURN_ON_ERROR(service_discovery_start_scan(5000), TAG, "Service discovery scan failed");
    
    // Wait for server discovery
    vTaskDelay(pdMS_TO_TICKS(6000));
    
    // Get best server
    howdytts_server_info_t server_info;
    esp_err_t ret = service_discovery_get_best_server(&server_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No HowdyTTS servers discovered");
        return ret;
    }
    
    ESP_LOGI(TAG, "Found HowdyTTS server: %s:%d", server_info.ip, server_info.port);
    
    // Update network manager with discovered server
    network_manager_set_server(&s_app.network, server_info.ip, server_info.port);
    strncpy(s_app.server_ip, server_info.ip, sizeof(s_app.server_ip) - 1);
    s_app.server_port = server_info.port;
    
    // Initialize WebSocket client
    ws_client_config_t ws_config = {
        .uri = "ws://192.168.1.100:8080/ws",  // Will be updated by service discovery
        .event_callback = websocket_event_handler,
        .callback_ctx = &s_app
    };
    
    // Update URI with discovered server
    snprintf((char*)ws_config.uri, sizeof(ws_config.uri), "ws://%s:%d/ws", 
             server_info.ip, server_info.port);
    
    ESP_RETURN_ON_ERROR(ws_client_init(&s_app.websocket, &ws_config), 
                       TAG, "WebSocket client init failed");
    
    ESP_RETURN_ON_ERROR(ws_client_start(s_app.websocket), TAG, "WebSocket client start failed");
    
    ESP_LOGI(TAG, "Server connection initialized");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 HowdyScreen Voice Assistant Starting ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize application context
    s_app.state_mutex = xSemaphoreCreateMutex();
    s_app.system_events = xEventGroupCreate();
    s_app.state = APP_STATE_INIT;
    
    // TODO: Load configuration from NVS
    // For now, use hardcoded values
    strncpy(s_app.wifi_ssid, CONFIG_HOWDY_WIFI_SSID, sizeof(s_app.wifi_ssid) - 1);
    strncpy(s_app.wifi_password, CONFIG_HOWDY_WIFI_PASSWORD, sizeof(s_app.wifi_password) - 1);
    strncpy(s_app.server_ip, "192.168.1.100", sizeof(s_app.server_ip) - 1);
    s_app.server_port = 8080;
    
    // Phase 1: Initialize display (highest priority for user feedback)
    app_transition_to(APP_STATE_DISPLAY_INIT);
    ret = init_display_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(ret));
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    // Phase 2: Initialize network
    app_transition_to(APP_STATE_NETWORK_INIT);
    ret = init_network_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(ret));
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    // Phase 3: Initialize audio subsystem
    ret = init_audio_subsystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio initialization failed: %s", esp_err_to_name(ret));
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    // Phase 4: Discover and connect to HowdyTTS server
    app_transition_to(APP_STATE_DISCOVERING_SERVER);
    ret = init_server_connection();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Server connection failed: %s", esp_err_to_name(ret));
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    // Wait for all subsystems to be ready
    EventBits_t ready_bits = DISPLAY_READY_BIT | NETWORK_READY_BIT | AUDIO_READY_BIT | SERVER_READY_BIT;
    EventBits_t bits = xEventGroupWaitBits(s_app.system_events, ready_bits, 
                                          pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    
    if ((bits & ready_bits) != ready_bits) {
        ESP_LOGE(TAG, "Not all subsystems ready: 0x%08lx", bits);
        app_transition_to(APP_STATE_ERROR);
        goto error_loop;
    }
    
    app_transition_to(APP_STATE_READY);
    ESP_LOGI(TAG, "=== HowdyScreen Voice Assistant Ready ===");
    
    // Main application loop
    while (true) {
        // Update system status
        if (s_app.display_ready) {
            // Update WiFi signal strength
            int rssi = network_get_rssi();
            ui_manager_update_wifi_signal(&s_app.ui, rssi);
            
            // Update connection status
            ui_manager_update_connection_status(&s_app.ui, s_app.server_connected);
        }
        
        // Check system health
        size_t free_heap = esp_get_free_heap_size();
        if (free_heap < 50000) {  // Less than 50KB free
            ESP_LOGW(TAG, "Low memory warning: %zu bytes free", free_heap);
        }
        
        // Sleep for a bit
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

error_loop:
    ESP_LOGE(TAG, "Application entered error state");
    while (true) {
        ESP_LOGE(TAG, "System in error state - check logs above for details");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}