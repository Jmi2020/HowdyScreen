/**
 * @file howdy_phase4_audio_interface.c
 * @brief ESP32-P4 HowdyScreen Phase 4: Audio Interface Implementation
 * 
 * Architecture: Smart Microphone + Speaker + Display for HowdyTTS
 * 
 * The ESP32-P4 HowdyScreen acts as a "smart microphone and speaker with a screen
 * for showing states of the program" - NO local AI processing.
 * 
 * Audio Flow:
 * 1. Microphone → ESP32-P4 → WebSocket → Mac Server (STT Processing)
 * 2. Mac Server (TTS Processing) → WebSocket → ESP32-P4 → Speaker
 * 3. Display shows visual states: IDLE, LISTENING, PROCESSING, SPEAKING
 * 
 * Components Integration:
 * - Audio Interface Coordinator: Manages microphone capture and speaker playback
 * - WebSocket Client: Handles bidirectional audio streaming to/from HowdyTTS server
 * - UI Manager: Provides visual feedback for audio states
 * - Service Discovery: Automatically finds HowdyTTS servers on network
 */

#include "audio_interface_coordinator.h"
#include "websocket_client.h"
#include "ui_manager.h"
#include "service_discovery.h"
#include "howdytts_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "HowdyPhase4";

// Event bits for coordination
#define AUDIO_INTERFACE_READY_BIT    BIT0
#define WEBSOCKET_CONNECTED_BIT      BIT1
#define SERVER_DISCOVERED_BIT        BIT2
#define SYSTEM_READY_BIT            BIT3

static EventGroupHandle_t s_system_events;
static char s_server_ws_uri[256] = {0};

// Forward declarations
static void audio_interface_event_handler(audio_interface_event_t event,
                                        const uint8_t *audio_data,
                                        size_t data_len,
                                        const audio_interface_status_t *status,
                                        void *user_data);

static void websocket_event_handler(ws_client_state_t state,
                                   ws_message_type_t msg_type,
                                   const uint8_t *data,
                                   size_t len);

static esp_err_t websocket_audio_callback(const uint8_t *tts_audio,
                                         size_t audio_len,
                                         void *user_data);

static void server_discovery_callback(const service_discovery_result_t *result, void *user_data);

/**
 * @brief Initialize ESP32-P4 HowdyScreen Audio Interface System
 */
esp_err_t howdy_phase4_init(void)
{
    ESP_LOGI(TAG, "🎤🔊📺 ESP32-P4 HowdyScreen Phase 4: Audio Interface Initialization");
    ESP_LOGI(TAG, "Architecture: Smart microphone + speaker + display for HowdyTTS");
    ESP_LOGI(TAG, "Processing: All STT/TTS done on Mac server, ESP32-P4 is audio passthrough");
    
    // Create system event group for coordination
    s_system_events = xEventGroupCreate();
    if (!s_system_events) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return ESP_ERR_NO_MEM;
    }
    
    // 1. Initialize Audio Interface Coordinator (Smart Microphone + Speaker)
    ESP_LOGI(TAG, "Step 1: Initializing audio interface coordinator...");
    
    audio_interface_config_t audio_config = AUDIO_INTERFACE_DEFAULT_CONFIG();
    audio_config.auto_start_listening = false;  // Manual control via UI
    audio_config.silence_timeout_ms = 5000;     // 5 second silence timeout
    audio_config.visual_feedback = true;        // Enable display state updates
    
    esp_err_t ret = audio_interface_init(&audio_config, audio_interface_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio interface: %s", esp_err_to_name(ret));
        return ret;
    }
    
    xEventGroupSetBits(s_system_events, AUDIO_INTERFACE_READY_BIT);
    ESP_LOGI(TAG, "✅ Audio interface coordinator initialized (microphone + speaker ready)");
    
    // 2. Initialize WebSocket Client for HowdyTTS Communication
    ESP_LOGI(TAG, "Step 2: Initializing WebSocket client for bidirectional audio streaming...");
    
    ws_client_config_t ws_config = {
        .server_uri = "",  // Will be set after service discovery
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 30,
        .keepalive_interval_sec = 5,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = 8192  // Large buffer for audio streaming
    };
    
    ret = ws_client_init(&ws_config, websocket_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set up bidirectional audio streaming callback
    ret = ws_client_set_audio_callback(websocket_audio_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WebSocket audio callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ WebSocket client initialized with bidirectional audio streaming");
    
    // 3. Start Service Discovery to Find HowdyTTS Servers
    ESP_LOGI(TAG, "Step 3: Starting service discovery for HowdyTTS servers...");
    
    ret = service_discovery_start("_howdytts._tcp.local", server_discovery_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start service discovery: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ Service discovery started, scanning for HowdyTTS servers...");
    
    // 4. Initialize UI Manager for Visual State Feedback
    ESP_LOGI(TAG, "Step 4: Initializing UI manager for voice assistant display...");
    
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Show initial "Searching for Server" state
    ui_manager_show_voice_assistant_state("SEARCHING", "Looking for HowdyTTS server...", 0.0f);
    
    ESP_LOGI(TAG, "✅ UI manager initialized with voice assistant interface");
    
    ESP_LOGI(TAG, "🎉 Phase 4 initialization complete - waiting for server discovery...");
    return ESP_OK;
}

/**
 * @brief Start HowdyScreen Audio Interface Operation
 * Called after server discovery completes
 */
static esp_err_t start_audio_interface_operation(void)
{
    ESP_LOGI(TAG, "🚀 Starting HowdyScreen audio interface operation");
    
    // Connect to WebSocket server
    esp_err_t ret = ws_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for WebSocket connection
    EventBits_t bits = xEventGroupWaitBits(s_system_events,
                                          WEBSOCKET_CONNECTED_BIT,
                                          pdFALSE,
                                          pdTRUE,
                                          pdMS_TO_TICKS(10000));  // 10 second timeout
    
    if (!(bits & WEBSOCKET_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WebSocket connection timeout");
        ui_manager_show_voice_assistant_state("ERROR", "Connection failed", 0.0f);
        return ESP_ERR_TIMEOUT;
    }
    
    // Set system ready
    xEventGroupSetBits(s_system_events, SYSTEM_READY_BIT);
    
    // Update UI to show ready state
    ui_manager_show_voice_assistant_state("READY", "Tap to start listening", 0.0f);
    
    ESP_LOGI(TAG, "🎉 HowdyScreen audio interface fully operational!");
    ESP_LOGI(TAG, "Ready to capture voice → stream to server → play TTS response");
    
    return ESP_OK;
}

/**
 * @brief Trigger voice listening (can be called by touch handler or button press)
 */
esp_err_t howdy_phase4_start_listening(void)
{
    // Check if system is ready
    EventBits_t bits = xEventGroupGetBits(s_system_events);
    if (!(bits & SYSTEM_READY_BIT)) {
        ESP_LOGW(TAG, "System not ready for listening");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "🎤 Starting voice listening - will stream audio to HowdyTTS server");
    
    // Start audio capture
    esp_err_t ret = audio_interface_start_listening();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start listening: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief Stop voice listening
 */
esp_err_t howdy_phase4_stop_listening(void)
{
    ESP_LOGI(TAG, "🛑 Stopping voice listening");
    
    esp_err_t ret = audio_interface_stop_listening();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop listening: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief Audio Interface Event Handler
 * Handles events from the audio coordinator and integrates with WebSocket
 */
static void audio_interface_event_handler(audio_interface_event_t event,
                                        const uint8_t *audio_data,
                                        size_t data_len,
                                        const audio_interface_status_t *status,
                                        void *user_data)
{
    switch (event) {
        case AUDIO_INTERFACE_EVENT_MICROPHONE_READY:
            ESP_LOGI(TAG, "🎤 Microphone ready for voice capture");
            break;
            
        case AUDIO_INTERFACE_EVENT_SPEAKER_READY:
            ESP_LOGI(TAG, "🔊 Speaker ready for TTS playback");
            break;
            
        case AUDIO_INTERFACE_EVENT_STATE_CHANGED:
            {
                const char* state_name = "";
                switch (status->current_state) {
                    case AUDIO_INTERFACE_STATE_IDLE:
                        state_name = "IDLE";
                        ui_manager_show_voice_assistant_state("READY", "Tap to start listening", 0.0f);
                        break;
                    case AUDIO_INTERFACE_STATE_LISTENING:
                        state_name = "LISTENING";
                        ui_manager_show_voice_assistant_state("LISTENING", "Speak now...", status->current_audio_level);
                        break;
                    case AUDIO_INTERFACE_STATE_PROCESSING:
                        state_name = "PROCESSING";
                        ui_manager_show_voice_assistant_state("PROCESSING", "Processing speech...", 0.0f);
                        break;
                    case AUDIO_INTERFACE_STATE_SPEAKING:
                        state_name = "SPEAKING";
                        ui_manager_show_voice_assistant_state("SPEAKING", "Playing response...", 0.0f);
                        break;
                    case AUDIO_INTERFACE_STATE_ERROR:
                        state_name = "ERROR";
                        ui_manager_show_voice_assistant_state("ERROR", "Audio error occurred", 0.0f);
                        break;
                }
                ESP_LOGI(TAG, "🔄 Audio interface state: %s", state_name);
            }
            break;
            
        case AUDIO_INTERFACE_EVENT_AUDIO_CAPTURED:
            // Stream captured audio to HowdyTTS server via WebSocket
            if (audio_data && data_len > 0) {
                esp_err_t ret = ws_client_stream_captured_audio(audio_data, data_len);
                if (ret == ESP_OK) {
                    ESP_LOGD(TAG, "📤 Streamed %zu bytes to HowdyTTS server", data_len);
                }
            }
            break;
            
        case AUDIO_INTERFACE_EVENT_VOICE_DETECTED:
            ESP_LOGI(TAG, "🗣️ Voice activity detected - streaming to server");
            audio_interface_set_state(AUDIO_INTERFACE_STATE_PROCESSING);
            break;
            
        case AUDIO_INTERFACE_EVENT_SILENCE_DETECTED:
            ESP_LOGI(TAG, "🤫 Silence detected - waiting for TTS response");
            break;
            
        case AUDIO_INTERFACE_EVENT_AUDIO_RECEIVED:
            ESP_LOGD(TAG, "📥 TTS audio received: %zu bytes", data_len);
            break;
            
        case AUDIO_INTERFACE_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ Audio interface error occurred");
            ui_manager_show_voice_assistant_state("ERROR", "Audio system error", 0.0f);
            break;
    }
}

/**
 * @brief WebSocket Event Handler
 */
static void websocket_event_handler(ws_client_state_t state,
                                   ws_message_type_t msg_type,
                                   const uint8_t *data,
                                   size_t len)
{
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "🔗 WebSocket connected to HowdyTTS server");
            xEventGroupSetBits(s_system_events, WEBSOCKET_CONNECTED_BIT);
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "🔌 WebSocket disconnected from HowdyTTS server");
            xEventGroupClearBits(s_system_events, WEBSOCKET_CONNECTED_BIT | SYSTEM_READY_BIT);
            ui_manager_show_voice_assistant_state("DISCONNECTED", "Reconnecting...", 0.0f);
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "❌ WebSocket error occurred");
            ui_manager_show_voice_assistant_state("ERROR", "Connection error", 0.0f);
            break;
            
        default:
            break;
    }
    
    if (msg_type == WS_MSG_TYPE_TTS_RESPONSE) {
        ESP_LOGI(TAG, "📥 Received TTS response: %zu bytes", len);
    }
}

/**
 * @brief WebSocket Audio Callback - Handles TTS audio from server
 */
static esp_err_t websocket_audio_callback(const uint8_t *tts_audio,
                                         size_t audio_len,
                                         void *user_data)
{
    if (!tts_audio || audio_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "🔊 Received TTS audio from server: %zu bytes", audio_len);
    
    // Send TTS audio to audio interface coordinator for playback
    esp_err_t ret = audio_interface_play_tts_audio(tts_audio, audio_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play TTS audio: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "✅ TTS audio queued for playback");
    return ESP_OK;
}

/**
 * @brief Service Discovery Callback - Handles HowdyTTS server discovery
 */
static void server_discovery_callback(const service_discovery_result_t *result, void *user_data)
{
    if (!result) {
        return;
    }
    
    ESP_LOGI(TAG, "🔍 Discovered HowdyTTS server: %s:%d", result->ip_addr, result->port);
    
    // Build WebSocket URI
    snprintf(s_server_ws_uri, sizeof(s_server_ws_uri), 
             "ws://%s:%d/audio", result->ip_addr, result->port);
    
    ESP_LOGI(TAG, "🔗 WebSocket URI: %s", s_server_ws_uri);
    
    // Update WebSocket client configuration
    ws_client_config_t ws_config = {
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 30,
        .keepalive_interval_sec = 5,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = 8192
    };
    strncpy(ws_config.server_uri, s_server_ws_uri, sizeof(ws_config.server_uri) - 1);
    
    // Re-initialize WebSocket client with server URI
    ws_client_stop();
    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for cleanup
    
    esp_err_t ret = ws_client_init(&ws_config, websocket_event_handler);
    if (ret == ESP_OK) {
        ret = ws_client_set_audio_callback(websocket_audio_callback, NULL);
        if (ret == ESP_OK) {
            xEventGroupSetBits(s_system_events, SERVER_DISCOVERED_BIT);
            
            // Start audio interface operation
            start_audio_interface_operation();
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure WebSocket for discovered server");
        ui_manager_show_voice_assistant_state("ERROR", "Server configuration failed", 0.0f);
    }
}

/**
 * @brief Get system status for debugging
 */
void howdy_phase4_print_status(void)
{
    ESP_LOGI(TAG, "=== HowdyScreen Audio Interface Status ===");
    
    // Audio interface status
    audio_interface_status_t audio_status;
    if (audio_interface_get_status(&audio_status) == ESP_OK) {
        ESP_LOGI(TAG, "Audio State: %d, Mic: %s, Speaker: %s, Voice: %s", 
                audio_status.current_state,
                audio_status.microphone_active ? "ON" : "OFF",
                audio_status.speaker_active ? "ON" : "OFF", 
                audio_status.voice_detected ? "YES" : "NO");
        ESP_LOGI(TAG, "Audio Level: %.3f, Chunks Sent: %lu, TTS Received: %lu",
                audio_status.current_audio_level,
                audio_status.audio_chunks_sent,
                audio_status.tts_chunks_received);
    }
    
    // WebSocket status
    ws_client_state_t ws_state = ws_client_get_state();
    const char* ws_state_str = "";
    switch (ws_state) {
        case WS_CLIENT_STATE_DISCONNECTED: ws_state_str = "DISCONNECTED"; break;
        case WS_CLIENT_STATE_CONNECTING: ws_state_str = "CONNECTING"; break;
        case WS_CLIENT_STATE_CONNECTED: ws_state_str = "CONNECTED"; break;
        case WS_CLIENT_STATE_ERROR: ws_state_str = "ERROR"; break;
    }
    ESP_LOGI(TAG, "WebSocket: %s, URI: %s", ws_state_str, s_server_ws_uri);
    
    // System events
    EventBits_t bits = xEventGroupGetBits(s_system_events);
    ESP_LOGI(TAG, "System Ready: %s, WS Connected: %s, Server Found: %s",
            (bits & SYSTEM_READY_BIT) ? "YES" : "NO",
            (bits & WEBSOCKET_CONNECTED_BIT) ? "YES" : "NO",
            (bits & SERVER_DISCOVERED_BIT) ? "YES" : "NO");
    
    ESP_LOGI(TAG, "=========================================");
}