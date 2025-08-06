#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_event.h"

// BSP and display components
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "lvgl.h"

// UI Manager for enhanced voice assistant interface
#include "ui_manager.h"
// WiFi connectivity
#include "wifi_manager.h"
// Service discovery for HowdyTTS servers
#include "service_discovery.h"
// WebSocket client for HowdyTTS communication
#include "websocket_client.h"
// Audio processing for I2S capture and playback
#include "audio_processor.h"

static const char *TAG = "HowdyDisplayTest";

// Global handles
static bool system_ready = false;
static bool wifi_connected = false;
static bool howdytts_connected = false;
static bool audio_initialized = false;
static bool voice_active = false;
static howdytts_server_info_t discovered_server = {0};

// WiFi credentials from menuconfig
#define WIFI_SSID CONFIG_HOWDY_WIFI_SSID
#define WIFI_PASSWORD CONFIG_HOWDY_WIFI_PASSWORD

// Function prototypes
static void system_init(void);
static void lvgl_tick_task(void *pvParameters);
static void wifi_event_handler(wifi_event_id_t event_id, void *event_data);
static void service_discovered_handler(const howdytts_server_info_t *server_info);
static void network_init(void);
static void websocket_event_handler(ws_client_state_t state, ws_message_type_t msg_type, const uint8_t *data, size_t len);
static void connect_to_howdytts(void);
static void audio_event_handler(audio_event_t event, void *data, size_t len);
static void init_audio_system(void);
static void start_voice_capture(void);
static void stop_voice_capture(void);
static void voice_activation_callback(bool start_voice);

static void system_init(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen Display Test System Initialization ===");
    
    // Initialize event loop (required for WiFi)
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Initialize I2C for touch controller and audio codec
    ESP_LOGI(TAG, "Initializing I2C for peripherals");
    ESP_ERROR_CHECK(bsp_i2c_init());
    
    // Initialize display with BSP
    ESP_LOGI(TAG, "Initializing 800x800 MIPI-DSI display");
    lv_display_t *display = bsp_display_start();
    if (!display) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }
    
    // Initialize and turn on display backlight
    ESP_LOGI(TAG, "Enabling display backlight");
    ESP_ERROR_CHECK(bsp_display_brightness_init());
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_ERROR_CHECK(bsp_display_brightness_set(80)); // 80% brightness
    
    // Get touch input device (initialized by bsp_display_start)
    ESP_LOGI(TAG, "Getting touch input device");
    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (!touch_indev) {
        ESP_LOGW(TAG, "Touch controller not available");
    } else {
        ESP_LOGI(TAG, "Touch controller ready");
    }
    
    ESP_LOGI(TAG, "Display and touch initialization complete");
    
    // Initialize UI Manager for enhanced voice assistant interface
    ESP_LOGI(TAG, "Initializing UI Manager with Howdy character animations");
    esp_err_t ui_ret = ui_manager_init();
    if (ui_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI Manager: %s", esp_err_to_name(ui_ret));
        return;
    }
    
    // Set initial UI state
    ui_manager_set_state(UI_STATE_INIT);
    ui_manager_update_status("System starting...");
    
    // Set voice activation callback
    ui_manager_set_voice_callback(voice_activation_callback);
    
    ESP_LOGI(TAG, "UI Manager initialized successfully");
    
    system_ready = true;
}

static void audio_event_handler(audio_event_t event, void *data, size_t len)
{
    switch (event) {
        case AUDIO_EVENT_STARTED:
            ESP_LOGI(TAG, "Audio capture started");
            voice_active = true;
            ui_manager_set_state(UI_STATE_LISTENING);
            ui_manager_update_status("Listening...");
            break;
            
        case AUDIO_EVENT_STOPPED:
            ESP_LOGI(TAG, "Audio capture stopped");
            voice_active = false;
            if (howdytts_connected) {
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_update_status("Connected - Tap to speak");
            }
            break;
            
        case AUDIO_EVENT_DATA_READY:
            if (data && len > 0 && howdytts_connected) {
                // Send audio data to HowdyTTS server via WebSocket
                esp_err_t ret = ws_client_send_binary_audio((const int16_t*)data, len / sizeof(int16_t));
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send audio data: %s", esp_err_to_name(ret));
                }
                
                // Update audio level meter
                int16_t *samples = (int16_t*)data;
                size_t num_samples = len / sizeof(int16_t);
                
                // Calculate RMS level for visual feedback
                int64_t sum = 0;
                for (size_t i = 0; i < num_samples; i++) {
                    sum += samples[i] * samples[i];
                }
                int level = (int)(100.0 * sqrt((double)sum / num_samples) / 32768.0);
                ui_manager_update_audio_level(level > 100 ? 100 : level);
            }
            break;
            
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio capture error");
            voice_active = false;
            ui_manager_set_state(UI_STATE_ERROR);
            ui_manager_update_status("Audio error");
            break;
    }
}

static void init_audio_system(void)
{
    ESP_LOGI(TAG, "Initializing audio system...");
    
    // Configure audio processor
    audio_processor_config_t audio_config = {
        .sample_rate = 16000,        // 16kHz for voice
        .bits_per_sample = 16,       // 16-bit samples
        .channels = 1,               // Mono
        .dma_buf_count = 4,          // 4 DMA buffers
        .dma_buf_len = 512,          // 512 samples per buffer
        .task_priority = 23,         // High priority for audio
        .task_core = 0               // Run on core 0
    };
    
    esp_err_t ret = audio_processor_init(&audio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio processor: %s", esp_err_to_name(ret));
        return;
    }
    
    // Set audio event callback
    ret = audio_processor_set_callback(audio_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set audio callback: %s", esp_err_to_name(ret));
        return;
    }
    
    audio_initialized = true;
    ESP_LOGI(TAG, "Audio system initialized successfully");
}

static void start_voice_capture(void)
{
    if (!audio_initialized) {
        ESP_LOGW(TAG, "Audio system not initialized");
        return;
    }
    
    if (voice_active) {
        ESP_LOGW(TAG, "Voice capture already active");
        return;
    }
    
    ESP_LOGI(TAG, "Starting voice capture...");
    esp_err_t ret = audio_processor_start_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio capture: %s", esp_err_to_name(ret));
        ui_manager_set_state(UI_STATE_ERROR);
        ui_manager_update_status("Audio start failed");
    }
}

static void stop_voice_capture(void)
{
    if (!voice_active) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping voice capture...");
    esp_err_t ret = audio_processor_stop_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop audio capture: %s", esp_err_to_name(ret));
    }
}

static void voice_activation_callback(bool start_voice)
{
    if (start_voice) {
        ESP_LOGI(TAG, "Touch activated voice capture");
        if (howdytts_connected && !voice_active) {
            start_voice_capture();
        } else if (!howdytts_connected) {
            ESP_LOGW(TAG, "Cannot start voice capture - not connected to HowdyTTS");
            ui_manager_update_status("Not connected to server");
        } else {
            ESP_LOGW(TAG, "Voice capture already active");
        }
    } else {
        ESP_LOGI(TAG, "Touch deactivated voice capture");
        if (voice_active) {
            stop_voice_capture();
        }
    }
}

static void wifi_event_handler(wifi_event_id_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_CONNECTED_ID:
            ESP_LOGI(TAG, "WiFi connected to AP");
            ui_manager_update_status("WiFi connected");
            break;
            
        case WIFI_EVENT_DISCONNECTED_ID:
            ESP_LOGW(TAG, "WiFi disconnected from AP");
            wifi_connected = false;
            ui_manager_set_wifi_strength(0);
            ui_manager_update_status("WiFi disconnected");
            break;
            
        case WIFI_EVENT_GOT_IP_ID:
            ESP_LOGI(TAG, "WiFi got IP address");
            wifi_connected = true;
            ui_manager_update_status("Connected - Searching for HowdyTTS...");
            
            // Update WiFi signal strength
            int signal_strength = wifi_manager_get_signal_strength();
            ui_manager_set_wifi_strength(signal_strength);
            
            // Start scanning for HowdyTTS servers
            ESP_LOGI(TAG, "Starting mDNS scan for HowdyTTS servers...");
            service_discovery_start_scan(0);  // Continuous scan
            break;
            
        case WIFI_EVENT_SCAN_DONE_ID:
            ESP_LOGI(TAG, "WiFi scan completed");
            break;
            
        default:
            break;
    }
}

static void service_discovered_handler(const howdytts_server_info_t *server_info)
{
    if (server_info) {
        ESP_LOGI(TAG, "HowdyTTS server discovered!");
        ESP_LOGI(TAG, "  Address: %s:%d", server_info->ip_addr, server_info->port);
        ESP_LOGI(TAG, "  Hostname: %s", server_info->hostname);
        ESP_LOGI(TAG, "  Version: %s", server_info->version);
        
        // Store the discovered server info
        memcpy(&discovered_server, server_info, sizeof(howdytts_server_info_t));
        
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "HowdyTTS found: %s", server_info->hostname);
        ui_manager_update_status(status_msg);
        
        // Connect to the WebSocket server
        connect_to_howdytts();
    }
}

static void websocket_event_handler(ws_client_state_t state, ws_message_type_t msg_type, const uint8_t *data, size_t len)
{
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to HowdyTTS");
            howdytts_connected = true;
            ui_manager_set_state(UI_STATE_IDLE);
            ui_manager_update_status("Connected - Tap to speak");
            
            // Initialize audio system now that we're connected
            if (!audio_initialized) {
                init_audio_system();
            }
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected from HowdyTTS");
            howdytts_connected = false;
            ui_manager_set_state(UI_STATE_ERROR);
            ui_manager_update_status("Disconnected from server");
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            howdytts_connected = false;
            ui_manager_set_state(UI_STATE_ERROR);
            ui_manager_update_status("Connection error");
            break;
            
        case WS_CLIENT_STATE_CONNECTING:
            ESP_LOGI(TAG, "Connecting to HowdyTTS...");
            ui_manager_update_status("Connecting to server...");
            break;
    }
    
    // Handle incoming messages
    if (state == WS_CLIENT_STATE_CONNECTED && data && len > 0) {
        if (msg_type == WS_MSG_TYPE_STATUS) {
            ESP_LOGI(TAG, "Received status from HowdyTTS: %.*s", (int)len, data);
            // Update UI based on server status
            if (strstr((char*)data, "listening")) {
                ui_manager_set_state(UI_STATE_LISTENING);
            } else if (strstr((char*)data, "processing")) {
                ui_manager_set_state(UI_STATE_PROCESSING);
                // Stop audio capture when server is processing
                if (voice_active) {
                    stop_voice_capture();
                }
            } else if (strstr((char*)data, "speaking")) {
                ui_manager_set_state(UI_STATE_SPEAKING);
            } else if (strstr((char*)data, "ready") || strstr((char*)data, "idle")) {
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_update_status("Connected - Tap to speak");
            }
        } else if (msg_type == WS_MSG_TYPE_TTS_RESPONSE) {
            ESP_LOGI(TAG, "Received TTS audio response (%d bytes)", len);
            // Play back TTS audio (if audio playback is implemented)
            if (audio_initialized) {
                audio_processor_write_data(data, len);
            }
        }
    }
}

static void connect_to_howdytts(void)
{
    if (discovered_server.port == 0) {
        ESP_LOGW(TAG, "No HowdyTTS server discovered yet");
        return;
    }
    
    ESP_LOGI(TAG, "Connecting to HowdyTTS at ws://%s:%d/howdytts", 
             discovered_server.ip_addr, discovered_server.port);
    
    // Configure WebSocket client
    ws_client_config_t ws_config = {
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 120,
        .keepalive_interval_sec = 30,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = 4096
    };
    
    // Build WebSocket URI - HowdyTTS test server uses /howdytts endpoint
    snprintf(ws_config.server_uri, sizeof(ws_config.server_uri), 
             "ws://%s:%d/howdytts", discovered_server.ip_addr, discovered_server.port);
    
    // Initialize WebSocket client
    esp_err_t ret = ws_client_init(&ws_config, websocket_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s", esp_err_to_name(ret));
        return;
    }
    
    // Start WebSocket connection
    ret = ws_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
    }
}

static void network_init(void)
{
    ESP_LOGI(TAG, "Initializing network components...");
    
    // Initialize WiFi manager
    esp_err_t ret = wifi_manager_init(wifi_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi manager: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize service discovery
    ret = service_discovery_init(service_discovered_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize service discovery: %s", esp_err_to_name(ret));
        return;
    }
    
    // Connect to WiFi
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
    ui_manager_update_status("Connecting to WiFi...");
    
    ret = wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        ui_manager_set_state(UI_STATE_ERROR);
        ui_manager_update_status("WiFi connection failed");
    }
}

static void lvgl_tick_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LVGL tick task started");
    
    while (1) {
        lv_tick_inc(5);  // 5ms tick
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen ESP32-P4 Display Test ===");
    
    // Print system information
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Hardware: ESP32-P4 with %d cores, rev v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Memory: %lu bytes free heap", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Board: ESP32-P4-WIFI6-Touch-LCD-3.4C (800x800 round display)");
    ESP_LOGI(TAG, "Target: Display initialization test");
    
    // Initialize core system
    system_init();
    
    if (!system_ready) {
        ESP_LOGE(TAG, "System initialization failed!");
        return;
    }
    
    // Create LVGL tick task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        lvgl_tick_task,
        "lvgl_tick",
        4096,
        NULL,
        10,  // High priority for UI responsiveness
        NULL,
        1  // Pin to core 1
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL tick task");
        return;
    }
    
    ESP_LOGI(TAG, "🚀 UI Manager initialized - starting voice assistant demo!");
    
    // Initialize network components
    network_init();
    
    // Wait for network initialization
    vTaskDelay(pdMS_TO_TICKS(3000));  // Give WiFi time to connect
    
    // If not connected to WiFi, run in demo mode
    if (!wifi_connected) {
        ESP_LOGW(TAG, "No network connection, running in demo mode");
        ui_manager_set_state(UI_STATE_IDLE);
        ui_manager_update_status("Demo mode - Cycling states");
    }
    
    // Main loop
    int demo_cycle = 0;
    bool demo_mode = true;
    
    while (1) {
        ESP_LOGI(TAG, "Voice assistant running... Free heap: %lu bytes", esp_get_free_heap_size());
        
        // Update WiFi signal strength periodically
        if (wifi_connected) {
            int signal_strength = wifi_manager_get_signal_strength();
            ui_manager_set_wifi_strength(signal_strength);
        }
        
        // If connected to HowdyTTS, stop demo mode and handle voice interaction
        if (howdytts_connected) {
            demo_mode = false;
            
            // Check for touch/button press to start voice capture
            // For now, we'll use a simple timer-based approach
            // In a real implementation, this would be triggered by touch or button
            static int voice_timer = 0;
            voice_timer++;
            
            // Simulate voice activation every 30 seconds
            if (voice_timer >= 30 && !voice_active) {
                ESP_LOGI(TAG, "Simulating voice activation...");
                start_voice_capture();
                voice_timer = 0;
                
                // Stop capture after 5 seconds (simulate speaking)
                vTaskDelay(pdMS_TO_TICKS(5000));
                stop_voice_capture();
            }
            
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // Demo mode: Cycle through different voice assistant states
        if (demo_mode) {
            switch (demo_cycle % 5) {
            case 0:
                ESP_LOGI(TAG, "Demo: IDLE state - Howdy greeting pose");
                ui_manager_set_state(UI_STATE_IDLE);
                ui_manager_update_status("Ready to speak - Tap Howdy to test!");
                ui_manager_update_audio_level(0);
                break;
                
            case 1:
                ESP_LOGI(TAG, "Demo: LISTENING state - Howdy listening pose");
                ui_manager_set_state(UI_STATE_LISTENING);
                ui_manager_update_status("Listening...");
                // Simulate audio levels
                for (int i = 0; i < 5; i++) {
                    ui_manager_update_audio_level(20 + (i * 15));
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
                break;
                
            case 2:
                ESP_LOGI(TAG, "Demo: PROCESSING state - Howdy thinking pose");
                ui_manager_set_state(UI_STATE_PROCESSING);
                ui_manager_update_status("Processing your request...");
                ui_manager_update_audio_level(0);
                break;
                
            case 3:
                ESP_LOGI(TAG, "Demo: SPEAKING state - Howdy response pose");
                ui_manager_set_state(UI_STATE_SPEAKING);
                ui_manager_update_status("Speaking response...");
                // Simulate speaking audio levels
                for (int i = 0; i < 5; i++) {
                    ui_manager_update_audio_level(30 + (i * 10));
                    vTaskDelay(pdMS_TO_TICKS(400));
                }
                break;
                
            case 4:
                ESP_LOGI(TAG, "Demo: ERROR state - System error");
                ui_manager_set_state(UI_STATE_ERROR);
                ui_manager_update_status("Connection error - retrying...");
                ui_manager_update_audio_level(0);
                break;
        }
        
            demo_cycle++;
            vTaskDelay(pdMS_TO_TICKS(8000));  // Hold each state for 8 seconds
        }
    }
}