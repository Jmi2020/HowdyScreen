/**
 * @file howdy_phase5_voice_assistant.c
 * @brief ESP32-P4 HowdyScreen Phase 5: Complete Voice Assistant Implementation
 * 
 * ARCHITECTURE: Smart Microphone + Speaker + Visual Display
 * 
 * The ESP32-P4 HowdyScreen functions as an intelligent audio interface:
 * - Microphone: Captures voice audio and streams to HowdyTTS server (Mac) via WebSocket
 * - Speaker: Receives TTS audio from HowdyTTS server via WebSocket and plays through ES8311
 * - Display: Shows rich visual states with animations (waiting, listening, thinking, speaking, ending)
 * 
 * Key Features:
 * 1. mDNS Service Discovery - Automatically finds HowdyTTS servers on network
 * 2. HTTP Health Monitoring - Monitors server connectivity and system status
 * 3. WebSocket Real-time Communication - Bidirectional audio streaming protocol
 * 4. Enhanced Visual Interface - Professional circular UI with state animations
 * 5. Audio Interface Coordination - Smart passthrough for STT/TTS processing
 * 
 * State Synchronization:
 * - HowdyTTS Server States: waiting → listening → thinking → speaking → ending
 * - ESP32-P4 UI States: IDLE → LISTENING → PROCESSING → SPEAKING → IDLE
 * - Visual Feedback: Pulsing animations, color changes, status updates
 * 
 * NO local STT/TTS processing - all AI processing happens on the Mac server.
 * The ESP32-P4 is a smart audio interface with rich visual feedback.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"

// Component includes
#include "service_discovery.h"
#include "howdytts_http_client.h"
#include "websocket_client.h"
#include "audio_interface_coordinator.h"
#include "ui_manager.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

static const char *TAG = "HowdyVoiceAssistant";

// Event group for system coordination
#define SYSTEM_READY_BIT        BIT0
#define SERVER_CONNECTED_BIT    BIT1
#define AUDIO_READY_BIT         BIT2
#define UI_READY_BIT            BIT3

static EventGroupHandle_t s_system_events;

// System state tracking
typedef struct {
    char server_ip[16];
    uint16_t server_port;
    bool server_connected;
    bool audio_active;
    char current_state[32];
    float audio_level;
    uint32_t uptime_seconds;
} voice_assistant_state_t;

static voice_assistant_state_t s_va_state = {0};

/**
 * @brief Server discovery callback - called when HowdyTTS server is found
 */
static void on_server_discovered(const howdytts_server_info_t *server_info)
{
    ESP_LOGI(TAG, "🎯 HowdyTTS server discovered: %s:%d (%s)", server_info->ip_addr, server_info->port, server_info->hostname);
    
    // Update system state
    strncpy(s_va_state.server_ip, server_info->ip_addr, sizeof(s_va_state.server_ip) - 1);
    s_va_state.server_port = server_info->port;
    
    // Update UI with discovery status
    ui_manager_show_voice_assistant_state("SEARCHING", "Server found! Connecting...", 0.0f);
    
    // Initialize HTTP client with discovered server
    esp_err_t ret = howdytts_http_init(server_info->ip_addr, server_info->port);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ HTTP client initialized successfully");
        
        // Initialize WebSocket client
        ret = ws_client_init(server_info->ip_addr, server_info->port, on_websocket_state_change, NULL);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ WebSocket client initialized successfully");
            s_va_state.server_connected = true;
            xEventGroupSetBits(s_system_events, SERVER_CONNECTED_BIT);
            
            // Update UI with connection success
            ui_manager_show_voice_assistant_state("READY", "Connected to HowdyTTS!", 0.0f);
        } else {
            ESP_LOGE(TAG, "❌ WebSocket client initialization failed: %s", esp_err_to_name(ret));
            ui_manager_show_voice_assistant_state("ERROR", "WebSocket connection failed", 0.0f);
        }
    } else {
        ESP_LOGE(TAG, "❌ HTTP client initialization failed: %s", esp_err_to_name(ret));
        ui_manager_show_voice_assistant_state("ERROR", "Server connection failed", 0.0f);
    }
}

/**
 * @brief WebSocket state change callback
 */
static void on_websocket_state_change(ws_client_state_t state, ws_message_type_t msg_type, 
                                     const uint8_t *data, size_t len)
{
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "🚀 WebSocket connected - voice assistant ready!");
            ui_manager_show_voice_assistant_state("READY", "Voice assistant active", 0.0f);
            strncpy(s_va_state.current_state, "waiting", sizeof(s_va_state.current_state) - 1);
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "⚠️  WebSocket disconnected - attempting reconnection");
            ui_manager_show_voice_assistant_state("DISCONNECTED", "Reconnecting...", 0.0f);
            s_va_state.server_connected = false;
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "❌ WebSocket error occurred");
            ui_manager_show_voice_assistant_state("ERROR", "Connection error", 0.0f);
            break;
            
        default:
            break;
    }
    
    // Handle incoming messages
    if (msg_type == WS_MESSAGE_TYPE_TEXT && data && len > 0) {
        // Parse server state messages
        char message[256];
        size_t copy_len = (len < sizeof(message) - 1) ? len : sizeof(message) - 1;
        memcpy(message, data, copy_len);
        message[copy_len] = '\0';
        
        ESP_LOGI(TAG, "📨 Server message: %s", message);
        
        // Update UI based on server state
        if (strstr(message, "waiting")) {
            ui_manager_show_voice_assistant_state("waiting", "Ready to listen", 0.0f);
            strncpy(s_va_state.current_state, "waiting", sizeof(s_va_state.current_state) - 1);
        } else if (strstr(message, "listening")) {
            ui_manager_show_voice_assistant_state("listening", "Listening...", s_va_state.audio_level);
            strncpy(s_va_state.current_state, "listening", sizeof(s_va_state.current_state) - 1);
        } else if (strstr(message, "thinking")) {
            ui_manager_show_voice_assistant_state("thinking", "Processing your request...", 0.0f);
            strncpy(s_va_state.current_state, "thinking", sizeof(s_va_state.current_state) - 1);
        } else if (strstr(message, "speaking")) {
            ui_manager_show_voice_assistant_state("speaking", "Playing response", 0.0f);
            strncpy(s_va_state.current_state, "speaking", sizeof(s_va_state.current_state) - 1);
        } else if (strstr(message, "ending")) {
            ui_manager_show_voice_assistant_state("ending", "Goodbye, partner! Happy trails!", 0.0f);
            strncpy(s_va_state.current_state, "ending", sizeof(s_va_state.current_state) - 1);
        }
    }
}

/**
 * @brief Audio interface event callback - handles captured audio and status updates
 */
static void on_audio_interface_event(audio_interface_event_t event_type,
                                   const uint8_t *data,
                                   size_t data_len,
                                   const audio_interface_status_t *status,
                                   void *user_data)
{
    switch (event_type) {
        case AUDIO_INTERFACE_EVENT_AUDIO_CAPTURED:
            if (s_va_state.server_connected && data && data_len > 0) {
                // Send captured audio to server via WebSocket
                esp_err_t ret = ws_client_stream_captured_audio(data, data_len);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to stream audio: %s", esp_err_to_name(ret));
                }
                
                // Update UI with current audio level during listening state
                if (status && strcmp(s_va_state.current_state, "listening") == 0) {
                    float level = status->current_audio_level;
                    s_va_state.audio_level = level;
                    ui_manager_show_voice_assistant_state("listening", "Listening...", level);
                }
            }
            break;
            
        case AUDIO_INTERFACE_EVENT_STATE_CHANGED:
            if (status) {
                ESP_LOGD(TAG, "Audio interface status - Level: %.2f, Mic Active: %s, Speaker Active: %s", 
                        status->current_audio_level, 
                        status->microphone_active ? "Yes" : "No",
                        status->speaker_active ? "Yes" : "No");
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief Audio playback callback - plays received TTS audio
 */
static int on_audio_received(const uint8_t *audio_data, size_t audio_len, void *user_data)
{
    ESP_LOGD(TAG, "Received %zu bytes of TTS audio", audio_len);
    
    // Play received audio through audio interface
    esp_err_t ret = audio_interface_play_tts_audio(audio_data, audio_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to play TTS audio: %s", esp_err_to_name(ret));
        return -1;
    }
    
    return 0;  // Success
}

/**
 * @brief System monitoring task - tracks uptime, memory, and connection health
 */
static void system_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🔍 System monitor started");
    
    const TickType_t monitor_interval = pdMS_TO_TICKS(5000); // 5 seconds
    
    while (true) {
        s_va_state.uptime_seconds += 5;
        
        // Log system status every 30 seconds
        if (s_va_state.uptime_seconds % 30 == 0) {
            size_t free_heap = esp_get_free_heap_size();
            size_t min_free_heap = esp_get_minimum_free_heap_size();
            
            ESP_LOGI(TAG, "📊 System Status - Uptime: %lu s, Free Heap: %zu KB (min: %zu KB), State: %s, Connected: %s",
                     (unsigned long)s_va_state.uptime_seconds,
                     free_heap / 1024,
                     min_free_heap / 1024,
                     s_va_state.current_state,
                     s_va_state.server_connected ? "Yes" : "No");
        }
        
        // Check server health if connected
        if (s_va_state.server_connected && s_va_state.uptime_seconds % 60 == 0) {
            // Ping server health every minute
            ESP_LOGD(TAG, "🏥 Checking server health...");
            // Health check happens automatically in HTTP client component
        }
        
        vTaskDelay(monitor_interval);
    }
}

/**
 * @brief Initialize all system components
 */
static esp_err_t init_voice_assistant_system(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "🚀 Initializing HowdyScreen Voice Assistant System...");
    
    // Create system event group
    s_system_events = xEventGroupCreate();
    if (!s_system_events) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✅ NVS initialized");
    
    // Initialize BSP (Board Support Package)
    ESP_LOGI(TAG, "🔧 Initializing BSP...");
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "❌ BSP display initialization failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ BSP display initialized successfully");
    
    // Initialize UI Manager
    ESP_LOGI(TAG, "🎨 Initializing UI Manager...");
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ UI Manager initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ UI Manager initialized");
    xEventGroupSetBits(s_system_events, UI_READY_BIT);
    
    // Show initial status
    ui_manager_show_voice_assistant_state("INIT", "Starting voice assistant...", 0.0f);
    
    // Initialize Audio Interface Coordinator
    ESP_LOGI(TAG, "🎤 Initializing Audio Interface...");
    audio_interface_config_t audio_config = {
        .capture_sample_rate = 16000,
        .capture_channels = 1,
        .capture_bits_per_sample = 16,
        .microphone_gain = 1.0f,
        .capture_chunk_size = 1024,
        .playback_sample_rate = 16000,
        .playback_channels = 1,
        .playback_bits_per_sample = 16,
        .speaker_volume = 0.8f,
        .playback_buffer_size = 4096,
        .auto_start_listening = false,
        .silence_timeout_ms = 5000,
        .visual_feedback = true
    };
    
    ret = audio_interface_init(&audio_config, on_audio_interface_event, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Audio Interface initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Audio Interface initialized");
    xEventGroupSetBits(s_system_events, AUDIO_READY_BIT);
    
    // Update UI status
    ui_manager_show_voice_assistant_state("SEARCHING", "Discovering HowdyTTS servers...", 0.0f);
    
    // Initialize Service Discovery
    ESP_LOGI(TAG, "🔍 Initializing Service Discovery...");
    ret = service_discovery_init(on_server_discovered);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Service Discovery initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Service Discovery initialized with callback");
    
    // Start service discovery scanning
    ret = service_discovery_start_scan(0);  // 0 = continuous scanning
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Failed to start service discovery: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✅ Service Discovery started - scanning for _howdytts._tcp services");
    
    xEventGroupSetBits(s_system_events, SYSTEM_READY_BIT);
    ESP_LOGI(TAG, "🎉 Voice Assistant System initialization complete!");
    
    return ESP_OK;
}

/**
 * @brief Main application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "================================================================================");
    ESP_LOGI(TAG, "  🤠 HowdyScreen Phase 5: Complete Voice Assistant Implementation");
    ESP_LOGI(TAG, "================================================================================");
    ESP_LOGI(TAG, "  Architecture: Smart Microphone + Speaker + Visual Display");
    ESP_LOGI(TAG, "  ");
    ESP_LOGI(TAG, "  Audio Processing: Mac Server (HowdyTTS) handles all STT/TTS");
    ESP_LOGI(TAG, "  ESP32-P4 Role: Intelligent audio interface with rich visual feedback");
    ESP_LOGI(TAG, "  ");
    ESP_LOGI(TAG, "  Features:");
    ESP_LOGI(TAG, "    • mDNS Service Discovery");
    ESP_LOGI(TAG, "    • HTTP Health Monitoring");  
    ESP_LOGI(TAG, "    • WebSocket Real-time Communication");
    ESP_LOGI(TAG, "    • Enhanced Visual Interface with Animations");
    ESP_LOGI(TAG, "    • Audio Interface Coordination");
    ESP_LOGI(TAG, "================================================================================");
    ESP_LOGI(TAG, "");
    
    // Initialize the complete voice assistant system
    esp_err_t ret = init_voice_assistant_system();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Voice Assistant System initialization failed: %s", esp_err_to_name(ret));
        ui_manager_show_voice_assistant_state("ERROR", "System initialization failed", 0.0f);
        return;
    }
    
    // Wait for system components to be ready
    ESP_LOGI(TAG, "⏳ Waiting for system components to be ready...");
    EventBits_t bits = xEventGroupWaitBits(
        s_system_events,
        SYSTEM_READY_BIT | UI_READY_BIT | AUDIO_READY_BIT,
        pdFALSE,
        pdTRUE,
        pdMS_TO_TICKS(10000)
    );
    
    if ((bits & (SYSTEM_READY_BIT | UI_READY_BIT | AUDIO_READY_BIT)) == 
        (SYSTEM_READY_BIT | UI_READY_BIT | AUDIO_READY_BIT)) {
        ESP_LOGI(TAG, "✅ All system components ready!");
    } else {
        ESP_LOGW(TAG, "⚠️  Some components not ready, continuing anyway...");
    }
    
    // Start system monitoring task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        system_monitor_task,
        "sys_monitor",
        4096,
        NULL,
        5,
        NULL,
        1  // Pin to core 1
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "❌ Failed to create system monitor task");
    } else {
        ESP_LOGI(TAG, "✅ System monitor task started");
    }
    
    // Wait for server connection
    ESP_LOGI(TAG, "🔍 Searching for HowdyTTS servers on the network...");
    bits = xEventGroupWaitBits(
        s_system_events,
        SERVER_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000)  // 30 second timeout
    );
    
    if (bits & SERVER_CONNECTED_BIT) {
        ESP_LOGI(TAG, "🎉 Successfully connected to HowdyTTS server at %s:%d", 
                 s_va_state.server_ip, s_va_state.server_port);
        
        // Set WebSocket audio callback
        ws_client_set_audio_callback(on_audio_received, NULL);
        ESP_LOGI(TAG, "✅ WebSocket audio callback registered");
        
        // Start audio interface listening mode
        ret = audio_interface_start_listening();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "🎤 Audio interface listening started successfully");
            s_va_state.audio_active = true;
        } else {
            ESP_LOGW(TAG, "⚠️  Audio interface listening start warning: %s", esp_err_to_name(ret));
        }
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "🎉🎤🔊 HOWDYSCREEN VOICE ASSISTANT IS NOW ACTIVE! 🔊🎤🎉");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "Ready to provide intelligent audio interface services:");
        ESP_LOGI(TAG, "  • Visual state feedback with animations");
        ESP_LOGI(TAG, "  • Audio capture and streaming to server"); 
        ESP_LOGI(TAG, "  • TTS audio playback from server");
        ESP_LOGI(TAG, "  • Real-time system health monitoring");
        ESP_LOGI(TAG, "");
        
    } else {
        ESP_LOGW(TAG, "⚠️  No HowdyTTS server found within 30 seconds");
        ESP_LOGI(TAG, "📡 Continuing service discovery in background...");
        ui_manager_show_voice_assistant_state("SEARCHING", "No servers found - still searching...", 0.0f);
    }
    
    // Main application loop - handle system events and state updates
    ESP_LOGI(TAG, "🔄 Entering main application loop...");
    strncpy(s_va_state.current_state, "waiting", sizeof(s_va_state.current_state) - 1);
    
    while (true) {
        // Update WiFi signal strength for UI
        // (This would typically come from the network layer)
        ui_manager_set_wifi_strength(85); // Placeholder - good signal
        
        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}