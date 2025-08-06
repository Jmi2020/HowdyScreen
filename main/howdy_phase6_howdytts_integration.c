/**
 * @file howdy_phase6_howdytts_integration.c
 * @brief Phase 6A: HowdyTTS Native Integration Application
 * 
 * This application demonstrates the complete HowdyTTS native protocol integration
 * with UDP discovery, PCM audio streaming, and HTTP state management.
 * 
 * Features:
 * - Native HowdyTTS protocol support (UDP discovery, audio streaming, HTTP state)
 * - Raw PCM audio streaming for minimal latency and memory usage
 * - Automatic server discovery and connection
 * - Real-time UI updates based on voice assistant states
 * - Touch-to-talk interface with visual feedback
 * - ESP32-P4 optimized with <10KB audio streaming overhead
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

// BSP includes for display initialization
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "lvgl.h"

// Component includes
#include "howdytts_network_integration.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "audio_processor.h"
#include "enhanced_vad.h"
#include "enhanced_udp_audio.h"
#include "esp32_p4_wake_word.h"
#include "esp32_p4_vad_feedback.h"
#include "tts_audio_handler.h"

static const char *TAG = "HowdyPhase6";

// Forward declarations
esp_err_t init_vad_feedback_client(const char *server_ip);

// Global state
typedef struct {
    bool wifi_connected;
    bool howdytts_connected;
    bool discovery_completed;
    howdytts_server_info_t selected_server;
    uint32_t audio_packets_sent;
    float current_audio_level;
    
    // Enhanced VAD state
    enhanced_vad_handle_t vad_handle;
    bool vad_initialized;
    enhanced_udp_audio_stats_t vad_stats;
    
    // Wake word detection state
    esp32_p4_wake_word_handle_t wake_word_handle;
    bool wake_word_initialized;
    uint32_t wake_word_detections;
    
    // VAD feedback client state
    vad_feedback_handle_t vad_feedback_handle;
    bool vad_feedback_connected;
} app_state_t;

static app_state_t s_app_state = {0};

// Wake word detection callback
static void wake_word_detection_callback(const esp32_p4_wake_word_result_t *result, void *user_data)
{
    if (!result) return;
    
    s_app_state.wake_word_detections++;
    
    ESP_LOGI(TAG, "🎯 Wake word detected! Confidence: %.2f%%, Pattern: %d, Syllables: %d", 
            result->confidence_score * 100, 
            result->pattern_match_score,
            result->syllable_count);
    
    // Update UI immediately for wake word detection
    ui_manager_set_state(UI_STATE_LISTENING);
    char wake_word_msg[128];
    snprintf(wake_word_msg, sizeof(wake_word_msg), 
             "Wake word detected (%.0f%% confidence)", result->confidence_score * 100);
    ui_manager_update_status(wake_word_msg);
    
    // Send wake word detection to VAD feedback server for validation
    if (s_app_state.vad_feedback_connected) {
        vad_feedback_send_wake_word_detection(s_app_state.vad_feedback_handle,
                                             result->detection_timestamp_ms, // Use timestamp as ID
                                             result,
                                             NULL); // No VAD result in this callback
    }
    
    // Start audio streaming to HowdyTTS server if connected
    if (s_app_state.howdytts_connected) {
        ESP_LOGI(TAG, "🎤 Starting audio streaming after wake word detection");
        howdytts_start_audio_streaming();
    }
}

// VAD feedback event callback
static void vad_feedback_event_callback(vad_feedback_message_type_t type, 
                                       const void *data, 
                                       size_t data_len, 
                                       void *user_data)
{
    switch (type) {
        case VAD_FEEDBACK_WAKE_WORD_VALIDATION: {
            const vad_feedback_wake_word_validation_t *validation = 
                (const vad_feedback_wake_word_validation_t*)data;
            
            ESP_LOGI(TAG, "%s Server %s wake word (ID: %lu, confidence: %.3f, time: %lums)",
                    validation->validated ? "✅" : "❌",
                    validation->validated ? "confirmed" : "rejected",
                    validation->detection_id,
                    validation->server_confidence,
                    validation->processing_time_ms);
            
            // Apply server feedback to wake word detector
            if (s_app_state.wake_word_initialized) {
                esp32_p4_wake_word_server_feedback(s_app_state.wake_word_handle,
                                                  validation->detection_id,
                                                  validation->validated,
                                                  validation->processing_time_ms);
            }
            
            // Update UI based on server validation
            if (validation->validated) {
                ui_manager_update_status("Wake word confirmed by server");
                // Continue with voice processing
            } else {
                ui_manager_update_status("False alarm - wake word rejected");
                ui_manager_set_state(UI_STATE_IDLE);
                // Stop audio streaming if it was started
                howdytts_stop_audio_streaming();
            }
            break;
        }
        
        case VAD_FEEDBACK_THRESHOLD_UPDATE: {
            const vad_feedback_threshold_update_t *update = 
                (const vad_feedback_threshold_update_t*)data;
            
            ESP_LOGI(TAG, "🔧 Applying threshold update: energy=%d, confidence=%.3f (%s)",
                    update->new_energy_threshold,
                    update->new_confidence_threshold,
                    update->reason);
            
            // Apply threshold updates to wake word detector
            if (s_app_state.wake_word_initialized) {
                esp32_p4_wake_word_update_thresholds(s_app_state.wake_word_handle,
                                                    update->new_energy_threshold,
                                                    update->new_confidence_threshold);
            }
            
            char threshold_msg[128];
            snprintf(threshold_msg, sizeof(threshold_msg), 
                    "Thresholds updated: E=%d C=%.2f", 
                    update->new_energy_threshold,
                    update->new_confidence_threshold);
            ui_manager_update_status(threshold_msg);
            break;
        }
        
        default:
            ESP_LOGD(TAG, "VAD feedback event type: %d", type);
            break;
    }
}

// HowdyTTS integration callbacks
static esp_err_t howdytts_audio_callback(const int16_t *audio_data, size_t samples, void *user_data)
{
    ESP_LOGD(TAG, "Audio callback: streaming %d samples to HowdyTTS server", (int)samples);
    
    esp_err_t ret = ESP_OK;
    
    // Process audio with enhanced VAD if available
    enhanced_vad_result_t vad_result = {0};
    if (s_app_state.vad_initialized && s_app_state.vad_handle) {
        ret = enhanced_vad_process_audio(s_app_state.vad_handle, audio_data, samples, &vad_result);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "VAD processing failed: %s", esp_err_to_name(ret));
            // Continue without VAD data
            memset(&vad_result, 0, sizeof(enhanced_vad_result_t));
        }
    }
    
    // Process audio with wake word detection if available
    esp32_p4_wake_word_result_t wake_word_result = {0};
    bool has_wake_word = false;
    if (s_app_state.wake_word_initialized && s_app_state.wake_word_handle) {
        ret = esp32_p4_wake_word_process(s_app_state.wake_word_handle, 
                                        audio_data, samples, 
                                        s_app_state.vad_initialized ? &vad_result : NULL,
                                        &wake_word_result);
        if (ret == ESP_OK && wake_word_result.state == WAKE_WORD_STATE_TRIGGERED) {
            has_wake_word = true;
            ESP_LOGI(TAG, "🎯 Wake word 'Hey Howdy' detected in audio callback!");
        }
    }
    
    // Stream audio with VAD and wake word data using enhanced UDP
    if (s_app_state.vad_initialized && has_wake_word) {
        // Use wake word enhanced UDP for wake word events
        ret = enhanced_udp_audio_send_with_wake_word(audio_data, samples, 
                                                    &vad_result, &wake_word_result);
    } else if (s_app_state.vad_initialized) {
        // Use regular enhanced UDP with VAD data
        ret = enhanced_udp_audio_send_with_vad(audio_data, samples, &vad_result);
    } else {
        // Fallback to regular HowdyTTS streaming
        ret = howdytts_stream_audio(audio_data, samples);
    }
    
    if (ret == ESP_OK) {
        s_app_state.audio_packets_sent++;
        
        // Calculate audio level for UI feedback
        s_app_state.current_audio_level = vad_result.max_amplitude > 0 ? 
            (float)vad_result.max_amplitude / 32768.0f : 0.0f;
        
        // Update UI with audio level and VAD state
        ui_manager_update_audio_level((int)(s_app_state.current_audio_level * 100));
        
        // Enhanced UI feedback based on VAD
        if (s_app_state.vad_initialized && vad_result.voice_detected) {
            if (vad_result.speech_started) {
                ESP_LOGI(TAG, "🗣️ Speech detected! Confidence: %.2f", vad_result.confidence);
                ui_manager_set_state(UI_STATE_LISTENING);
            }
            
            // Show VAD confidence in UI
            if (vad_result.high_confidence) {
                char confidence_msg[64];
                snprintf(confidence_msg, sizeof(confidence_msg), 
                        "Voice detected (%.0f%% confidence)", vad_result.confidence * 100);
                ui_manager_update_status(confidence_msg);
            }
        } else if (s_app_state.vad_initialized && vad_result.speech_ended) {
            ESP_LOGI(TAG, "🤫 Speech ended");
            ui_manager_set_state(UI_STATE_PROCESSING);
        }
        
        // Update device status with VAD metrics
        int signal_strength = wifi_manager_get_signal_strength();
        float noise_floor_db = vad_result.snr_db > 0 ? vad_result.snr_db : -1;
        howdytts_update_device_status(s_app_state.current_audio_level, noise_floor_db, signal_strength);
    }
    
    return ret;
}

// TTS Audio event callback
static void tts_audio_event_callback(tts_audio_event_t event, const void *data, size_t data_len, void *user_data)
{
    switch (event) {
        case TTS_AUDIO_EVENT_STARTED:
            ESP_LOGI(TAG, "🔊 TTS playback started");
            ui_manager_set_state(UI_STATE_SPEAKING);
            ui_manager_update_status("Playing TTS response...");
            break;
            
        case TTS_AUDIO_EVENT_FINISHED:
            ESP_LOGI(TAG, "✅ TTS playback finished");
            ui_manager_set_state(UI_STATE_LISTENING);
            ui_manager_update_status("Ready for voice input");
            break;
            
        case TTS_AUDIO_EVENT_CHUNK_PLAYED:
            ESP_LOGV(TAG, "TTS chunk played (%zu bytes)", data_len);
            break;
            
        case TTS_AUDIO_EVENT_BUFFER_EMPTY:
            ESP_LOGV(TAG, "TTS buffer empty - ready for more data");
            break;
            
        case TTS_AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ TTS playback error");
            ui_manager_set_state(UI_STATE_ERROR);
            ui_manager_update_status("TTS playback error");
            break;
            
        default:
            ESP_LOGD(TAG, "Unknown TTS audio event: %d", event);
            break;
    }
}

static esp_err_t howdytts_tts_callback(const int16_t *tts_audio, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "🔊 TTS callback: received %zu samples from HowdyTTS server", samples);
    
    // Convert samples to bytes (16-bit samples = 2 bytes each)
    size_t audio_bytes = samples * sizeof(int16_t);
    
    // Play TTS audio through TTS audio handler
    esp_err_t ret = tts_audio_play_chunk((const uint8_t*)tts_audio, audio_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play TTS audio chunk: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "TTS audio chunk queued successfully (%zu bytes)", audio_bytes);
    return ESP_OK;
}

// WebSocket TTS Audio callback (bridges WebSocket to TTS audio handler)
static void howdytts_tts_audio_callback(const vad_feedback_tts_session_t *session,
                                       const int16_t *audio_data,
                                       size_t sample_count,
                                       void *user_data)
{
    ESP_LOGI(TAG, "🎵 WebSocket TTS audio callback: session=%s, samples=%zu", 
            session->session_id, sample_count);
    
    // Convert samples to bytes (16-bit samples = 2 bytes each)
    size_t audio_bytes = sample_count * sizeof(int16_t);
    
    // Check if TTS is ready for new chunks
    if (!tts_audio_is_playing()) {
        // Start TTS playback session
        esp_err_t start_ret = tts_audio_start_playback();
        if (start_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start TTS playback: %s", esp_err_to_name(start_ret));
            return;
        }
        
        ESP_LOGI(TAG, "🎶 Started TTS playback session: %s", session->session_id);
    }
    
    // Queue TTS audio chunk for playback
    esp_err_t ret = tts_audio_play_chunk((const uint8_t*)audio_data, audio_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue TTS audio chunk: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGD(TAG, "TTS audio chunk from WebSocket queued successfully (%zu bytes)", audio_bytes);
}

static void howdytts_event_callback(const howdytts_event_data_t *event, void *user_data)
{
    switch (event->event_type) {
        case HOWDYTTS_EVENT_DISCOVERY_STARTED:
            ESP_LOGI(TAG, "🔍 HowdyTTS discovery started");
            ui_manager_update_status("Discovering HowdyTTS servers...");
            break;
            
        case HOWDYTTS_EVENT_SERVER_DISCOVERED:
            ESP_LOGI(TAG, "🎯 Discovered HowdyTTS server: %s (%s)", 
                    event->data.server_info.hostname, 
                    event->data.server_info.ip_address);
            
            // Auto-select first discovered server
            if (!s_app_state.howdytts_connected) {
                s_app_state.selected_server = event->data.server_info;
                
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Found %s - connecting...", 
                        event->data.server_info.hostname);
                ui_manager_update_status(status_msg);
                
                // Connect to server
                howdytts_connect_to_server(&s_app_state.selected_server);
            }
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_ESTABLISHED:
            ESP_LOGI(TAG, "✅ Connected to HowdyTTS server");
            s_app_state.howdytts_connected = true;
            ui_manager_update_status("Connected to HowdyTTS");
            ui_manager_set_state(UI_STATE_IDLE);
            
            // Initialize VAD feedback client now that we have server connection
            if (!s_app_state.vad_feedback_connected && s_app_state.selected_server.ip_address[0] != '\0') {
                init_vad_feedback_client(s_app_state.selected_server.ip_address);
            }
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "❌ Lost connection to HowdyTTS server");
            s_app_state.howdytts_connected = false;
            ui_manager_update_status("Connection lost - reconnecting...");
            ui_manager_set_state(UI_STATE_ERROR);
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STARTED:
            ESP_LOGI(TAG, "🎵 Audio streaming started");
            ui_manager_set_state(UI_STATE_LISTENING);
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STOPPED:
            ESP_LOGI(TAG, "🔇 Audio streaming stopped");
            ui_manager_set_state(UI_STATE_IDLE);
            break;
            
        case HOWDYTTS_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ HowdyTTS error: %s", event->message);
            ui_manager_update_status("HowdyTTS Error");
            ui_manager_set_state(UI_STATE_ERROR);
            break;
            
        default:
            ESP_LOGD(TAG, "HowdyTTS event: %s", event->message);
            break;
    }
}

static void howdytts_va_state_callback(howdytts_va_state_t va_state, const char *state_text, void *user_data)
{
    ESP_LOGI(TAG, "🗣️ Voice assistant state changed: %s", 
            va_state == HOWDYTTS_VA_STATE_WAITING ? "waiting" :
            va_state == HOWDYTTS_VA_STATE_LISTENING ? "listening" :
            va_state == HOWDYTTS_VA_STATE_THINKING ? "thinking" :
            va_state == HOWDYTTS_VA_STATE_SPEAKING ? "speaking" : "ending");
    
    // Map HowdyTTS states to UI states
    switch (va_state) {
        case HOWDYTTS_VA_STATE_WAITING:
            ui_manager_set_state(UI_STATE_IDLE);
            ui_manager_update_status("Tap to speak");
            break;
            
        case HOWDYTTS_VA_STATE_LISTENING:
            ui_manager_set_state(UI_STATE_LISTENING);
            ui_manager_update_status("Listening...");
            break;
            
        case HOWDYTTS_VA_STATE_THINKING:
            ui_manager_set_state(UI_STATE_PROCESSING);
            ui_manager_update_status("Processing...");
            break;
            
        case HOWDYTTS_VA_STATE_SPEAKING:
            ui_manager_set_state(UI_STATE_SPEAKING);
            if (state_text) {
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Speaking: %.50s%s", 
                        state_text, strlen(state_text) > 50 ? "..." : "");
                ui_manager_update_status(status_msg);
            } else {
                ui_manager_update_status("Speaking...");
            }
            break;
            
        case HOWDYTTS_VA_STATE_ENDING:
            ui_manager_set_state(UI_STATE_IDLE);
            ui_manager_update_status("Conversation ended");
            break;
    }
}

// Voice activation callback from UI
static void voice_activation_callback(bool start_voice)
{
    if (start_voice) {
        ESP_LOGI(TAG, "🎤 Voice activation triggered by touch");
        
        if (s_app_state.howdytts_connected) {
            // Start audio streaming to HowdyTTS server
            howdytts_start_audio_streaming();
            ui_manager_set_state(UI_STATE_LISTENING);
        } else {
            ESP_LOGW(TAG, "Cannot start voice capture - not connected to HowdyTTS server");
            ui_manager_update_status("Not connected to server");
        }
    } else {
        ESP_LOGI(TAG, "🔇 Voice activation ended");
        howdytts_stop_audio_streaming();
    }
}

// WiFi connection monitoring task
static void wifi_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WiFi monitor task started");
    
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
        
        // Add error handling to prevent crashes
        bool wifi_connected = wifi_manager_is_connected();
        
        if (wifi_connected != s_app_state.wifi_connected) {
            s_app_state.wifi_connected = wifi_connected;
            
            if (wifi_connected) {
                ESP_LOGI(TAG, "WiFi connected");
                
                // Safely update UI if functions are available
                int signal_strength = wifi_manager_get_signal_strength();
                if (signal_strength >= 0) {
                    ui_manager_set_wifi_strength(signal_strength);
                }
                ui_manager_update_status("WiFi connected");
                
                // Start HowdyTTS server discovery
                if (!s_app_state.discovery_completed) {
                    ESP_LOGI(TAG, "Starting HowdyTTS discovery");
                    howdytts_discovery_start(15000);
                    s_app_state.discovery_completed = true;
                }
            } else {
                ESP_LOGW(TAG, "WiFi disconnected");
                s_app_state.howdytts_connected = false;
                ui_manager_set_wifi_strength(0);
                ui_manager_update_status("WiFi disconnected");
                ui_manager_set_state(UI_STATE_ERROR);
            }
        }
        
        // Update signal strength periodically if connected
        if (wifi_connected) {
            int signal_strength = wifi_manager_get_signal_strength();
            if (signal_strength >= 0) {
                ui_manager_set_wifi_strength(signal_strength);
            }
        }
    }
}

// System initialization
static esp_err_t system_init(void)
{
    ESP_LOGI(TAG, "🚀 Initializing HowdyTTS Phase 6 Application");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Note: WiFi events are handled by the WiFi manager component
    
    return ESP_OK;
}

static esp_err_t howdytts_integration_init_app(void)
{
    ESP_LOGI(TAG, "🔧 Initializing HowdyTTS integration with Enhanced VAD and Wake Word Detection");
    
    // Initialize Enhanced VAD first
    enhanced_vad_config_t vad_config;
    enhanced_vad_get_default_config(16000, &vad_config);
    
    // Optimize VAD for ESP32-P4 HowdyScreen environment
    vad_config.amplitude_threshold = 2500;          // Adjust for microphone sensitivity
    vad_config.silence_threshold_ms = 1200;         // 1.2s silence detection
    vad_config.min_voice_duration_ms = 300;         // 300ms minimum voice
    vad_config.snr_threshold_db = 8.0f;            // 8dB SNR requirement
    vad_config.consistency_frames = 5;              // 5-frame consistency
    vad_config.confidence_threshold = 0.7f;         // 70% confidence minimum
    
    s_app_state.vad_handle = enhanced_vad_init(&vad_config);
    if (s_app_state.vad_handle) {
        s_app_state.vad_initialized = true;
        ESP_LOGI(TAG, "✅ Enhanced VAD initialized successfully");
    } else {
        ESP_LOGW(TAG, "⚠️ Enhanced VAD initialization failed - continuing with basic audio");
        s_app_state.vad_initialized = false;
    }
    
    // Initialize Wake Word Detection Engine
    esp32_p4_wake_word_config_t wake_word_config;
    esp32_p4_wake_word_get_default_config(&wake_word_config);
    
    // Optimize wake word detection for "Hey Howdy"
    wake_word_config.sample_rate = 16000;            // 16kHz audio
    wake_word_config.frame_size = 320;               // 20ms frames
    wake_word_config.energy_threshold = 3000;        // Moderate sensitivity
    wake_word_config.confidence_threshold = 0.65f;   // 65% confidence required
    wake_word_config.silence_timeout_ms = 2000;      // 2 second silence timeout
    wake_word_config.enable_adaptation = true;       // Enable adaptive learning
    wake_word_config.adaptation_rate = 0.05f;        // 5% adaptation rate
    wake_word_config.max_detections_per_min = 12;    // Rate limiting
    
    s_app_state.wake_word_handle = esp32_p4_wake_word_init(&wake_word_config);
    if (s_app_state.wake_word_handle) {
        s_app_state.wake_word_initialized = true;
        
        // Set wake word detection callback
        esp32_p4_wake_word_set_callback(s_app_state.wake_word_handle,
                                       wake_word_detection_callback,
                                       NULL);
        
        ESP_LOGI(TAG, "✅ ESP32-P4 Wake Word Detection initialized");
        ESP_LOGI(TAG, "🎯 Target phrase: 'Hey Howdy'");
        ESP_LOGI(TAG, "🔧 Energy threshold: %d, Confidence: %.2f", 
                wake_word_config.energy_threshold, wake_word_config.confidence_threshold);
    } else {
        ESP_LOGW(TAG, "⚠️ Wake word detection initialization failed - continuing without wake word");
        s_app_state.wake_word_initialized = false;
    }
    
    // Initialize Enhanced UDP Audio if VAD is available
    if (s_app_state.vad_initialized) {
        enhanced_udp_audio_config_t udp_config;
        udp_audio_config_t basic_udp_config = {
            .server_ip = "192.168.1.100",  // Will be updated by discovery
            .server_port = 8000,            // HowdyTTS UDP audio port
            .local_port = 0,                // Auto-assign local port
            .buffer_size = 2048,
            .packet_size_ms = 20,           // 20ms packets (320 samples)
            .enable_compression = false     // Raw PCM for minimal latency
        };
        
        enhanced_udp_audio_get_default_config(&basic_udp_config, &udp_config);
        
        // Configure for HowdyTTS integration
        udp_config.enable_vad_transmission = true;
        udp_config.enable_vad_optimization = true;
        udp_config.enable_silence_suppression = true;
        udp_config.silence_packet_interval_ms = 100;    // 100ms during silence
        udp_config.confidence_reporting_threshold = 0;   // Report all confidence levels
        
        esp_err_t udp_ret = enhanced_udp_audio_init(&udp_config);
        if (udp_ret != ESP_OK) {
            ESP_LOGW(TAG, "Enhanced UDP audio init failed: %s", esp_err_to_name(udp_ret));
            s_app_state.vad_initialized = false;  // Fallback to basic mode
        } else {
            ESP_LOGI(TAG, "✅ Enhanced UDP audio streaming initialized");
        }
    }
    
    // Configure HowdyTTS integration
    howdytts_integration_config_t howdytts_config = {
        .device_id = "esp32p4-howdyscreen-001",
        .device_name = "Office HowdyScreen",
        .room = "office",
        .protocol_mode = HOWDYTTS_PROTOCOL_UDP_ONLY, // Start with UDP only
        .audio_format = HOWDYTTS_AUDIO_PCM_16,       // Raw PCM streaming
        .sample_rate = 16000,                        // 16kHz audio
        .frame_size = 320,                          // 20ms frames
        .enable_audio_stats = true,                 // Performance monitoring
        .enable_fallback = false,                   // No WebSocket fallback for now
        .discovery_timeout_ms = 15000,              // 15 second discovery
        .connection_retry_count = 3                 // 3 retry attempts
    };
    
    // Set up callbacks
    howdytts_integration_callbacks_t howdytts_callbacks = {
        .audio_callback = howdytts_audio_callback,
        .tts_callback = howdytts_tts_callback,
        .event_callback = howdytts_event_callback,
        .va_state_callback = howdytts_va_state_callback,
        .user_data = NULL
    };
    
    // Initialize HowdyTTS integration
    esp_err_t ret = howdytts_integration_init(&howdytts_config, &howdytts_callbacks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HowdyTTS integration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ HowdyTTS integration initialized successfully");
    ESP_LOGI(TAG, "🎯 VAD Mode: %s", s_app_state.vad_initialized ? "Enhanced Edge VAD" : "Basic Audio");
    ESP_LOGI(TAG, "🎤 Wake Word: %s", s_app_state.wake_word_initialized ? "Hey Howdy Detection Active" : "Disabled");
    
    // Initialize TTS Audio Handler for speaker output
    ESP_LOGI(TAG, "🔊 Initializing TTS Audio Handler");
    tts_audio_config_t tts_config = TTS_AUDIO_DEFAULT_CONFIG();
    
    // Customize TTS configuration for ESP32-P4 HowdyScreen
    tts_config.sample_rate = 16000;        // Match HowdyTTS output
    tts_config.channels = 1;               // Mono audio
    tts_config.bits_per_sample = 16;       // 16-bit PCM
    tts_config.volume = 0.8f;              // 80% volume
    tts_config.buffer_size = 8192;         // Larger buffer for smooth playback
    tts_config.buffer_timeout_ms = 1000;   // 1 second timeout
    
    esp_err_t tts_ret = tts_audio_init(&tts_config, tts_audio_event_callback, NULL);
    if (tts_ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ TTS Audio Handler initialized");
        ESP_LOGI(TAG, "🔊 Audio Format: %luHz, %dch, %d-bit, %.0f%% volume",
                tts_config.sample_rate, tts_config.channels, 
                tts_config.bits_per_sample, tts_config.volume * 100);
    } else {
        ESP_LOGW(TAG, "⚠️ TTS Audio Handler initialization failed: %s", esp_err_to_name(tts_ret));
    }
    
    // Note: VAD feedback client will be initialized after WiFi connection
    // when we know the HowdyTTS server IP address
    ESP_LOGI(TAG, "📡 VAD feedback client will connect after server discovery");
    
    return ESP_OK;
}

// Initialize VAD feedback client (called after server discovery)
esp_err_t init_vad_feedback_client(const char *server_ip)
{
    if (!s_app_state.wake_word_initialized) {
        ESP_LOGW(TAG, "Skipping VAD feedback - wake word detection not available");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🔧 Initializing VAD feedback client for server: %s", server_ip);
    
    // Get default VAD feedback configuration
    vad_feedback_config_t feedback_config;
    vad_feedback_get_default_config(server_ip, "esp32p4-howdyscreen-001", &feedback_config);
    
    // Customize configuration
    strncpy(feedback_config.device_name, "ESP32-P4 HowdyScreen", sizeof(feedback_config.device_name) - 1);
    strncpy(feedback_config.room, "office", sizeof(feedback_config.room) - 1);
    feedback_config.enable_wake_word_feedback = true;
    feedback_config.enable_threshold_adaptation = true;
    feedback_config.enable_training_mode = false;  // Start in normal mode
    feedback_config.auto_reconnect = true;
    feedback_config.keepalive_interval_ms = 30000;
    
    // Initialize VAD feedback client
    s_app_state.vad_feedback_handle = vad_feedback_init(&feedback_config,
                                                       vad_feedback_event_callback,
                                                       NULL);
    
    if (s_app_state.vad_feedback_handle) {
        ESP_LOGI(TAG, "✅ VAD feedback client initialized");
        
        // Set TTS audio callback for receiving server TTS responses
        esp_err_t tts_cb_ret = vad_feedback_set_tts_audio_callback(s_app_state.vad_feedback_handle,
                                                                   howdytts_tts_audio_callback,
                                                                   NULL);
        if (tts_cb_ret == ESP_OK) {
            ESP_LOGI(TAG, "🔊 TTS audio callback registered for WebSocket streaming");
        } else {
            ESP_LOGW(TAG, "⚠️ Failed to register TTS audio callback: %s", esp_err_to_name(tts_cb_ret));
        }
        
        // Connect to server
        esp_err_t ret = vad_feedback_connect(s_app_state.vad_feedback_handle);
        if (ret == ESP_OK) {
            s_app_state.vad_feedback_connected = true;
            ESP_LOGI(TAG, "✅ VAD feedback client connected to %s:8001", server_ip);
        } else {
            ESP_LOGW(TAG, "⚠️ VAD feedback connection failed: %s", esp_err_to_name(ret));
            s_app_state.vad_feedback_connected = false;
        }
        
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "❌ Failed to initialize VAD feedback client");
        return ESP_FAIL;
    }
}

// Statistics reporting task
static void stats_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (true) {
        // Update every 10 seconds to reduce system load
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10000));
        
        if (s_app_state.howdytts_connected) {
            // Get audio statistics
            howdytts_audio_stats_t stats;
            if (howdytts_get_audio_stats(&stats) == ESP_OK) {
                ESP_LOGI(TAG, "📊 Audio Stats - Packets sent: %d, Loss rate: %.2f%%, Latency: %.1fms",
                        (int)stats.packets_sent, stats.packet_loss_rate * 100, stats.average_latency_ms);
            }
            
            // Enhanced VAD statistics (reduced verbosity to prevent stack issues)
            if (s_app_state.vad_initialized) {
                enhanced_udp_audio_stats_t vad_stats;
                if (enhanced_udp_audio_get_enhanced_stats(&vad_stats) == ESP_OK) {
                    ESP_LOGI(TAG, "🎤 VAD: V:%d S:%d C:%.0f%% Sup:%d NF:%d",
                            (int)vad_stats.voice_packets_sent,
                            (int)vad_stats.silence_packets_sent,
                            vad_stats.average_vad_confidence * 100,
                            (int)vad_stats.packets_suppressed,
                            vad_stats.current_noise_floor);
                }
            }
            
            // Wake word detection statistics
            if (s_app_state.wake_word_initialized) {
                esp32_p4_wake_word_stats_t wake_word_stats;
                if (esp32_p4_wake_word_get_stats(s_app_state.wake_word_handle, &wake_word_stats) == ESP_OK) {
                    ESP_LOGI(TAG, "🎯 WakeWord: Det:%d TP:%d FP:%d Acc:%.0f%% Thr:%d",
                            (int)wake_word_stats.total_detections,
                            (int)wake_word_stats.true_positives,
                            (int)wake_word_stats.false_positives,
                            (wake_word_stats.true_positives + wake_word_stats.false_positives > 0) ?
                                (float)wake_word_stats.true_positives * 100 / 
                                (wake_word_stats.true_positives + wake_word_stats.false_positives) : 0.0f,
                            wake_word_stats.current_energy_threshold);
                    
                    // Send statistics to VAD feedback server periodically
                    if (s_app_state.vad_feedback_connected) {
                        static uint32_t last_stats_sent = 0;
                        uint32_t now = esp_timer_get_time() / 1000;
                        
                        if (now - last_stats_sent > 60000) { // Send every minute
                            enhanced_udp_audio_stats_t enhanced_stats;
                            enhanced_udp_audio_stats_t *udp_stats = NULL;
                            if (enhanced_udp_audio_get_enhanced_stats(&enhanced_stats) == ESP_OK) {
                                udp_stats = &enhanced_stats;
                            }
                            vad_feedback_send_statistics(s_app_state.vad_feedback_handle,
                                                        &wake_word_stats,
                                                        udp_stats);
                            last_stats_sent = now;
                        }
                    }
                }
            }
            
            // VAD feedback client statistics
            if (s_app_state.vad_feedback_connected) {
                vad_feedback_stats_t feedback_stats;
                if (vad_feedback_get_stats(s_app_state.vad_feedback_handle, &feedback_stats) == ESP_OK) {
                    ESP_LOGI(TAG, "📡 Feedback: Sent:%d Recv:%d Val:%d Acc:%.0f%%",
                            (int)feedback_stats.messages_sent,
                            (int)feedback_stats.messages_received,
                            (int)feedback_stats.wake_word_validations,
                            feedback_stats.validation_accuracy * 100);
                }
            }
            
            // System health
            ESP_LOGI(TAG, "💾 System Health - Free heap: %d bytes, Min free: %d bytes",
                    (int)esp_get_free_heap_size(), (int)esp_get_minimum_free_heap_size());
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "🎉 HowdyTTS Phase 6 - Native Protocol Integration");
    ESP_LOGI(TAG, "ESP32-P4 HowdyScreen with PCM Audio Streaming");
    
    // System initialization
    ESP_ERROR_CHECK(system_init());
    
    // Initialize BSP (Board Support Package) and display
    ESP_LOGI(TAG, "🔧 Initializing BSP and display...");
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "❌ BSP display initialization failed");
        return;
    }
    ESP_LOGI(TAG, "✅ BSP display initialized successfully");
    
    // Turn on display backlight
    ESP_LOGI(TAG, "💡 Turning on display backlight...");
    ESP_ERROR_CHECK(bsp_display_backlight_on());
    ESP_LOGI(TAG, "✅ Display backlight enabled");
    
    // Initialize UI Manager
    ESP_LOGI(TAG, "🖥️ Initializing UI Manager");
    ESP_ERROR_CHECK(ui_manager_init());
    ui_manager_set_voice_callback(voice_activation_callback);
    ui_manager_update_status("Initializing HowdyTTS...");
    
    // Initialize HowdyTTS integration
    ESP_ERROR_CHECK(howdytts_integration_init_app());
    
    // Initialize WiFi
    ESP_LOGI(TAG, "📶 Initializing WiFi");
    ESP_ERROR_CHECK(wifi_manager_init(NULL));
    
    ui_manager_update_status("Connecting to WiFi...");
    esp_err_t wifi_result = wifi_manager_auto_connect();
    if (wifi_result != ESP_OK) {
        ESP_LOGW(TAG, "⚠️ WiFi auto-connect failed: %s", esp_err_to_name(wifi_result));
        ui_manager_update_status("WiFi connection failed - will retry");
    }
    
    // Start monitoring tasks
    xTaskCreate(stats_task, "stats_task", 4096, NULL, 2, NULL);
    xTaskCreate(wifi_monitor_task, "wifi_monitor", 4096, NULL, 1, NULL);
    
    ESP_LOGI(TAG, "🎯 Phase 6 initialization complete!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== HowdyTTS Integration Ready ===");
    ESP_LOGI(TAG, "Protocol: Native UDP (PCM streaming)");
    ESP_LOGI(TAG, "Device: %s", "esp32p4-howdyscreen-001");
    ESP_LOGI(TAG, "Audio: 16kHz/16-bit PCM, 20ms frames");
    ESP_LOGI(TAG, "Memory: <10KB audio streaming overhead");
    ESP_LOGI(TAG, "UI: Touch-to-talk with visual feedback");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== Option C: Bidirectional VAD ===");
    ESP_LOGI(TAG, "Wake Word: %s", s_app_state.wake_word_initialized ? "Hey Howdy Detection" : "Disabled");
    ESP_LOGI(TAG, "Enhanced VAD: %s", s_app_state.vad_initialized ? "Edge Processing" : "Basic");
    ESP_LOGI(TAG, "VAD Feedback: WebSocket client (connects after discovery)");
    ESP_LOGI(TAG, "Adaptive Learning: Server-guided threshold adjustment");
    ESP_LOGI(TAG, "=====================================");
    ESP_LOGI(TAG, "");
    
    // Main application loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // Periodic health checks
        if (s_app_state.wifi_connected && !s_app_state.howdytts_connected) {
            // Try to reconnect to HowdyTTS if WiFi is up but HowdyTTS is down
            static uint32_t last_reconnect_attempt = 0;
            uint32_t now = esp_timer_get_time() / 1000;
            
            if (now - last_reconnect_attempt > 30000) { // Try every 30 seconds
                ESP_LOGI(TAG, "🔄 Attempting to reconnect to HowdyTTS servers");
                howdytts_discovery_start(10000);
                last_reconnect_attempt = now;
            }
        }
    }
}