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
#include "websocket_client.h"
#include "dual_i2s_manager.h"

static const char *TAG = "HowdyPhase6";

// Forward declarations
esp_err_t init_vad_feedback_client(const char *server_ip);
extern esp_err_t run_audio_stream_test(void);
static void update_conversation_context(vad_conversation_context_t new_context);

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
    float wake_word_confidence;          // Last wake word confidence
    
    // VAD feedback client state (includes TTS audio playback)
    vad_feedback_handle_t vad_feedback_handle;
    bool vad_feedback_connected;
} app_state_t;

static app_state_t s_app_state = {0};

// Performance monitoring task
static void performance_monitoring_task(void *pvParameters)
{
    ESP_LOGI(TAG, "🎯 Performance monitoring task started");
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(30000); // Report every 30 seconds
    
    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        
        // Get dual I2S performance metrics
        dual_i2s_performance_metrics_t i2s_metrics;
        esp_err_t ret = dual_i2s_get_performance_metrics(&i2s_metrics);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "📊 === PERFORMANCE REPORT ===");
            ESP_LOGI(TAG, "🎵 Audio Latency: %lu ms (Target: <30ms)", i2s_metrics.estimated_audio_latency_ms);
            ESP_LOGI(TAG, "⚡ I2S Processing: avg=%.1fμs, max=%luμs", 
                    i2s_metrics.average_processing_time_us, i2s_metrics.max_processing_time_us);
            ESP_LOGI(TAG, "📈 Operations: %lu total, %lu underruns, %lu mode switches",
                    i2s_metrics.total_operations, i2s_metrics.buffer_underruns, i2s_metrics.mode_switches);
            ESP_LOGI(TAG, "💾 Memory Usage: %zu bytes I2S buffers", i2s_metrics.memory_usage_bytes);
            
            // System memory info
            multi_heap_info_t heap_info;
            heap_caps_get_info(&heap_info, MALLOC_CAP_DEFAULT);
            ESP_LOGI(TAG, "🧠 System Memory: %lu KB free, %lu KB largest", 
                    heap_info.total_free_bytes / 1024, heap_info.largest_free_block / 1024);
            
            // Audio stream statistics
            ESP_LOGI(TAG, "🎤 Audio Stats: %lu packets sent, level=%.1f", 
                    s_app_state.audio_packets_sent, s_app_state.current_audio_level);
            ESP_LOGI(TAG, "🎯 Wake Words: %lu detections", s_app_state.wake_word_detections);
            
            // Connection status
            ESP_LOGI(TAG, "🌐 Connections: WiFi=%s, HowdyTTS=%s, VAD Feedback=%s",
                    s_app_state.wifi_connected ? "✅" : "❌",
                    s_app_state.howdytts_connected ? "✅" : "❌", 
                    s_app_state.vad_feedback_connected ? "✅" : "❌");
            ESP_LOGI(TAG, "📊 === END PERFORMANCE REPORT ===");
            
            // Performance warning system
            if (i2s_metrics.estimated_audio_latency_ms > 50) {
                ESP_LOGW(TAG, "⚠️ Audio latency above 50ms target!");
            }
            if (i2s_metrics.buffer_underruns > 10) {
                ESP_LOGW(TAG, "⚠️ High buffer underrun count detected!");
            }
            if (heap_info.total_free_bytes < 100000) { // Less than 100KB
                ESP_LOGW(TAG, "⚠️ Low memory warning: %lu bytes free", heap_info.total_free_bytes);
            }
        }
    }
}

// Wake word detection callback
static void wake_word_detection_callback(const esp32_p4_wake_word_result_t *result, void *user_data)
{
    if (!result) return;
    
    s_app_state.wake_word_detections++;
    s_app_state.wake_word_confidence = result->confidence_score;
    
    ESP_LOGI(TAG, "🎯 Wake word detected! Confidence: %.2f%%, Pattern: %d, Syllables: %d", 
            result->confidence_score * 100, 
            result->pattern_match_score,
            result->syllable_count);
    
    // Enhanced wake word detection UI with complete state management
    char wake_word_msg[128];
    snprintf(wake_word_msg, sizeof(wake_word_msg), 
             "'Hey Howdy' detected (%.0f%% confidence)", result->confidence_score * 100);
             
    char detail_msg[128];
    snprintf(detail_msg, sizeof(detail_msg), 
             "Pattern: %d, Syllables: %d", result->pattern_match_score, result->syllable_count);
    
    // Show wake word detection with animation
    ui_manager_show_wake_word_detection(result->confidence_score, "Hey Howdy");
    
    // Update conversation state to wake word detected
    ui_manager_update_conversation_state(UI_STATE_WAKE_WORD_DETECTED,
                                        "Wake word detected!",
                                        detail_msg,
                                        0, // No mic level yet
                                        0, // No TTS
                                        0.0f, // No VAD yet
                                        result->confidence_score);
    
    // Send wake word detection to VAD feedback server for validation
    if (s_app_state.vad_feedback_connected) {
        vad_feedback_send_wake_word_detection(s_app_state.vad_feedback_handle,
                                             result->detection_timestamp_ms, // Use timestamp as ID
                                             result,
                                             NULL); // No VAD result in this callback
    }
    
    // Audio is already streaming continuously - wake word is sent via enhanced UDP
    // The server receives wake word detection through the enhanced UDP protocol
    ESP_LOGI(TAG, "🎤 Wake word detected - server notified via enhanced UDP protocol");
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
            
            // Enhanced UI feedback based on server validation
            if (validation->validated) {
                // Server confirmed wake word - transition to listening
                ui_manager_update_conversation_state(UI_STATE_LISTENING,
                                                    "Wake word confirmed - listening",
                                                    "Server validation successful",
                                                    0, 0, 0.0f, validation->server_confidence);
                                                    
                ESP_LOGI(TAG, "✅ Server confirmed wake word - continuing conversation");
            } else {
                // Server rejected wake word - return to idle
                ui_manager_update_conversation_state(UI_STATE_IDLE,
                                                    "False wake word - back to listening",
                                                    "Server rejected detection",
                                                    0, 0, 0.0f, 0.0f);
                                                    
                // Stop audio streaming if it was started
                howdytts_stop_audio_streaming();
                ESP_LOGI(TAG, "❌ Server rejected wake word - returning to idle");
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
            
            // Show threshold update in detail status
            char threshold_msg[128];
            snprintf(threshold_msg, sizeof(threshold_msg), 
                    "Adaptive learning: E=%d C=%.2f", 
                    update->new_energy_threshold,
                    update->new_confidence_threshold);
                    
            // Update conversation state to show adaptation
            ui_state_t current_state = ui_manager_get_state();
            ui_manager_update_conversation_state(current_state, // Keep current state
                                                NULL, // Don't change main status
                                                threshold_msg, // Show in detail
                                                0, 0, 0.0f, -1.0f);
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
        
        // Update audio visualization with enhanced feedback
        ui_manager_update_mic_level((int)(s_app_state.current_audio_level * 100), 
                                  s_app_state.vad_initialized ? vad_result.confidence : 0.0f);
        
        // Enhanced UI feedback with complete conversation state management
        if (s_app_state.vad_initialized && vad_result.voice_detected) {
            if (vad_result.speech_started) {
                ESP_LOGI(TAG, "🗣️ Speech detected! Confidence: %.2f", vad_result.confidence);
                
                // Use enhanced conversation state update
                char speech_status[128];
                snprintf(speech_status, sizeof(speech_status), 
                        "Speech detected - confidence %.0f%%", vad_result.confidence * 100);
                        
                ui_manager_update_conversation_state(UI_STATE_SPEECH_DETECTED,
                                                    "Voice input detected",
                                                    speech_status,
                                                    (int)(s_app_state.current_audio_level * 100),
                                                    0, // No TTS during listening
                                                    vad_result.confidence,
                                                    s_app_state.wake_word_confidence);
            }
            
            // Continuous VAD confidence updates during speech
            ui_manager_update_mic_level((int)(s_app_state.current_audio_level * 100), 
                                      vad_result.confidence);
                                      
        } else if (s_app_state.vad_initialized && vad_result.speech_ended) {
            ESP_LOGI(TAG, "🤫 Speech ended - transitioning to processing");
            
            ui_manager_update_conversation_state(UI_STATE_PROCESSING,
                                                "Processing your request...",
                                                "Speech analysis complete",
                                                0, // No more mic input
                                                0, // No TTS yet
                                                0.0f, // No VAD during processing
                                                -1.0f); // Wake word not applicable
        }
        
        // Update device status with VAD metrics
        int signal_strength = wifi_manager_get_signal_strength();
        float noise_floor_db = vad_result.snr_db > 0 ? vad_result.snr_db : -1;
        howdytts_update_device_status(s_app_state.current_audio_level, noise_floor_db, signal_strength);
    }
    
    return ret;
}

// TTS Audio event callback with conversation context and echo cancellation
static void tts_audio_event_callback(tts_audio_event_t event, const void *data, size_t data_len, void *user_data)
{
    switch (event) {
        case TTS_AUDIO_EVENT_STARTED:
            ESP_LOGI(TAG, "🔊 TTS playback started - activating simultaneous I2S mode");
            
            // Enhanced TTS start with conversation state management
            ui_manager_update_conversation_state(UI_STATE_SPEAKING,
                                                "Howdy is responding...",
                                                "TTS audio playback started",
                                                0, // No mic input during TTS
                                                50, // Initial TTS level
                                                0.0f, // No VAD during TTS
                                                -1.0f); // Wake word not applicable
            
            // Switch to simultaneous I2S mode for full-duplex operation
            dual_i2s_set_mode(DUAL_I2S_MODE_SIMULTANEOUS);
            dual_i2s_start();
            
            // Notify VAD and wake word systems of TTS start for echo cancellation
            if (s_app_state.vad_initialized && s_app_state.vad_handle) {
                enhanced_vad_set_tts_audio_level(s_app_state.vad_handle, 0.8f, NULL);
            }
            if (s_app_state.wake_word_initialized && s_app_state.wake_word_handle) {
                esp32_p4_wake_word_set_tts_level(s_app_state.wake_word_handle, 0.8f);
            }
            break;
            
        case TTS_AUDIO_EVENT_FINISHED:
            ESP_LOGI(TAG, "✅ TTS playback finished");
            
            // Enhanced TTS completion with conversation state
            ui_manager_update_conversation_state(UI_STATE_CONVERSATION_ACTIVE,
                                                "Ready for your response",
                                                "TTS playback complete",
                                                0, // Reset mic level
                                                0, // Reset TTS level
                                                0.0f, // Ready for new VAD
                                                -1.0f); // Ready for wake word
            
            // Notify VAD and wake word systems of TTS end
            if (s_app_state.vad_initialized && s_app_state.vad_handle) {
                enhanced_vad_set_tts_audio_level(s_app_state.vad_handle, 0.0f, NULL);
            }
            if (s_app_state.wake_word_initialized && s_app_state.wake_word_handle) {
                esp32_p4_wake_word_set_tts_level(s_app_state.wake_word_handle, 0.0f);
            }
            
            // Return to listening context after TTS
            update_conversation_context(VAD_CONVERSATION_LISTENING);
            break;
            
        case TTS_AUDIO_EVENT_CHUNK_PLAYED:
            ESP_LOGV(TAG, "TTS chunk played (%zu bytes)", data_len);
            
            // Update TTS level based on chunk size for dynamic visualization
            int tts_level = (int)((float)data_len / 1024.0f * 100); // Rough level based on chunk size
            if (tts_level > 100) tts_level = 100;
            
            ui_manager_update_tts_level(tts_level, 0.0f); // No progress info available
            break;
            
        case TTS_AUDIO_EVENT_BUFFER_EMPTY:
            ESP_LOGV(TAG, "TTS buffer empty - ready for more data");
            break;
            
        case TTS_AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ TTS playback error");
            
            // Show TTS error with recovery information
            ui_manager_show_error_with_recovery("TTS Audio",
                                              "TTS playback failed - audio system error",
                                              5); // 5 second recovery estimate
            
            // Reset TTS level on error
            if (s_app_state.vad_initialized && s_app_state.vad_handle) {
                enhanced_vad_set_tts_audio_level(s_app_state.vad_handle, 0.0f, NULL);
            }
            if (s_app_state.wake_word_initialized && s_app_state.wake_word_handle) {
                esp32_p4_wake_word_set_tts_level(s_app_state.wake_word_handle, 0.0f);
            }
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
            
            ui_manager_update_conversation_state(UI_STATE_DISCOVERING,
                                                "Discovering HowdyTTS servers...",
                                                "Scanning network for voice servers",
                                                0, 0, 0.0f, -1.0f);
            break;
            
        case HOWDYTTS_EVENT_SERVER_DISCOVERED:
            ESP_LOGI(TAG, "🎯 Discovered HowdyTTS server: %s (%s)", 
                    event->data.server_info.hostname, 
                    event->data.server_info.ip_address);
            
            // Auto-select first discovered server
            if (!s_app_state.howdytts_connected) {
                ESP_LOGI(TAG, "🔗 Auto-selecting first discovered server for connection");
                s_app_state.selected_server = event->data.server_info;
                
                char status_msg[128];
                snprintf(status_msg, sizeof(status_msg), "Found %s - connecting...", 
                        event->data.server_info.hostname);
                        
                char detail_msg[128];
                snprintf(detail_msg, sizeof(detail_msg), "Server: %s", 
                        event->data.server_info.ip_address);
                
                ui_manager_update_conversation_state(UI_STATE_CONNECTING,
                                                    status_msg,
                                                    detail_msg,
                                                    0, 0, 0.0f, -1.0f);
                
                // Connect to server
                ESP_LOGI(TAG, "🚀 Calling howdytts_connect_to_server()");
                esp_err_t ret = howdytts_connect_to_server(&s_app_state.selected_server);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "❌ Connection failed: %s", esp_err_to_name(ret));
                    ui_manager_show_error_with_recovery("Network", "HowdyTTS connection failed", 10);
                }
            } else {
                ESP_LOGI(TAG, "⚠️  Already connected - ignoring discovered server");
            }
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_ESTABLISHED:
            ESP_LOGI(TAG, "✅ CONNECTION_ESTABLISHED event received - server connection successful");
            s_app_state.howdytts_connected = true;
            
            // Show successful connection with server details
            char connection_msg[128];
            snprintf(connection_msg, sizeof(connection_msg), "Connected to %s", 
                    s_app_state.selected_server.hostname);
                    
            ui_manager_update_conversation_state(UI_STATE_REGISTERED,
                                                connection_msg,
                                                "Voice assistant ready",
                                                0, 0, 0.0f, -1.0f);
                                                
            // After 2 seconds, transition to idle listening state
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            ui_manager_update_conversation_state(UI_STATE_IDLE,
                                                "Say 'Hey Howdy' to start",
                                                "Listening for wake word",
                                                0, 0, 0.0f, -1.0f);
            
            // Initialize VAD feedback client now that we have server connection
            if (!s_app_state.vad_feedback_connected && s_app_state.selected_server.ip_address[0] != '\0') {
                ESP_LOGI(TAG, "🔗 Initializing VAD feedback client for %s", s_app_state.selected_server.ip_address);
                init_vad_feedback_client(s_app_state.selected_server.ip_address);
            }
            
            // TTS audio playback is handled by the VAD feedback client WebSocket connection
            ESP_LOGI(TAG, "🔊 TTS audio playback ready via VAD feedback WebSocket connection");
            
            // Start continuous audio streaming for wake word detection
            ESP_LOGI(TAG, "🎤 Starting continuous audio streaming for wake word detection");
            esp_err_t stream_ret = howdytts_start_audio_streaming();
            if (stream_ret != ESP_OK) {
                ESP_LOGE(TAG, "❌ Failed to start audio streaming: %s", esp_err_to_name(stream_ret));
                ui_manager_update_status("Audio streaming failed");
            } else {
                ESP_LOGI(TAG, "✅ Audio streaming started successfully");
            }
            break;
            
        case HOWDYTTS_EVENT_CONNECTION_LOST:
            ESP_LOGW(TAG, "❌ Lost connection to HowdyTTS server");
            s_app_state.howdytts_connected = false;
            
            ui_manager_show_error_with_recovery("Network",
                                              "HowdyTTS connection lost - reconnecting",
                                              15); // 15 second reconnection estimate
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STARTED:
            ESP_LOGI(TAG, "🎵 Audio streaming started");
            
            ui_manager_update_conversation_state(UI_STATE_IDLE, // Stay in idle until wake word
                                                "Audio streaming active",
                                                "Microphone ready for 'Hey Howdy'",
                                                0, 0, 0.0f, -1.0f);
            break;
            
        case HOWDYTTS_EVENT_AUDIO_STREAMING_STOPPED:
            ESP_LOGI(TAG, "🔇 Audio streaming stopped");
            
            ui_manager_update_conversation_state(UI_STATE_IDLE,
                                                "Audio streaming paused",
                                                "Microphone temporarily disabled",
                                                0, 0, 0.0f, -1.0f);
            break;
            
        case HOWDYTTS_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ HowdyTTS error: %s", event->message);
            
            ui_manager_show_error_with_recovery("HowdyTTS",
                                              strlen(event->message) > 0 ? event->message : "Unknown HowdyTTS error",
                                              10); // 10 second recovery estimate
            break;
            
        default:
            ESP_LOGD(TAG, "HowdyTTS event: %s", event->message);
            break;
    }
}

// Helper function to update conversation context for VAD and wake word detection
static void update_conversation_context(vad_conversation_context_t new_context)
{
    // Update VAD conversation context
    if (s_app_state.vad_initialized && s_app_state.vad_handle) {
        enhanced_vad_set_conversation_context(s_app_state.vad_handle, new_context);
    }
    
    // Update wake word conversation context  
    if (s_app_state.wake_word_initialized && s_app_state.wake_word_handle) {
        esp32_p4_wake_word_set_conversation_context(s_app_state.wake_word_handle, new_context);
    }
    
    ESP_LOGI(TAG, "🎯 Conversation context updated: %s",
            new_context == VAD_CONVERSATION_IDLE ? "idle" :
            new_context == VAD_CONVERSATION_LISTENING ? "listening" :
            new_context == VAD_CONVERSATION_SPEAKING ? "speaking" : "processing");
}

static void howdytts_va_state_callback(howdytts_va_state_t va_state, const char *state_text, void *user_data)
{
    ESP_LOGI(TAG, "🗣️ Voice assistant state changed: %s", 
            va_state == HOWDYTTS_VA_STATE_WAITING ? "waiting" :
            va_state == HOWDYTTS_VA_STATE_LISTENING ? "listening" :
            va_state == HOWDYTTS_VA_STATE_THINKING ? "thinking" :
            va_state == HOWDYTTS_VA_STATE_SPEAKING ? "speaking" : "ending");
    
    // Enhanced voice assistant state mapping with complete conversation management
    switch (va_state) {
        case HOWDYTTS_VA_STATE_WAITING:
            ui_manager_update_conversation_state(UI_STATE_IDLE,
                                                "Say 'Hey Howdy' to start",
                                                "Voice assistant ready",
                                                0, 0, 0.0f, -1.0f);
            update_conversation_context(VAD_CONVERSATION_IDLE);
            break;
            
        case HOWDYTTS_VA_STATE_LISTENING:
            ui_manager_update_conversation_state(UI_STATE_LISTENING,
                                                "Listening for your voice...",
                                                "Speak your request",
                                                20, // Some mic activity expected
                                                0, // No TTS
                                                0.5f, // Moderate VAD confidence
                                                -1.0f); // Wake word already detected
            update_conversation_context(VAD_CONVERSATION_LISTENING);
            break;
            
        case HOWDYTTS_VA_STATE_THINKING:
            ui_manager_update_conversation_state(UI_STATE_THINKING,
                                                "Processing your request...",
                                                "AI is thinking about your question",
                                                0, // No mic input during processing
                                                0, // No TTS yet
                                                0.0f, // No VAD during processing
                                                -1.0f); // Wake word not applicable
            update_conversation_context(VAD_CONVERSATION_PROCESSING);
            break;
            
        case HOWDYTTS_VA_STATE_SPEAKING:
            // Format speaking status with response text preview
            char speaking_status[128];
            char speaking_detail[128];
            
            if (state_text && strlen(state_text) > 0) {
                snprintf(speaking_status, sizeof(speaking_status), "Howdy is responding...");
                snprintf(speaking_detail, sizeof(speaking_detail), "%.60s%s", 
                        state_text, strlen(state_text) > 60 ? "..." : "");
            } else {
                snprintf(speaking_status, sizeof(speaking_status), "Howdy is speaking...");
                snprintf(speaking_detail, sizeof(speaking_detail), "Generating voice response");
            }
            
            ui_manager_update_conversation_state(UI_STATE_RESPONDING,
                                                speaking_status,
                                                speaking_detail,
                                                0, // No mic input during TTS
                                                70, // TTS audio level
                                                0.0f, // No VAD during TTS
                                                -1.0f); // Wake word not applicable
            update_conversation_context(VAD_CONVERSATION_SPEAKING);
            break;
            
        case HOWDYTTS_VA_STATE_ENDING:
            ui_manager_update_conversation_state(UI_STATE_SESSION_ENDING,
                                                "Conversation ending...",
                                                "Session complete - goodbye!",
                                                0, 0, 0.0f, -1.0f);
            update_conversation_context(VAD_CONVERSATION_IDLE);
            
            // After 3 seconds, return to idle listening
            vTaskDelay(pdMS_TO_TICKS(3000));
            ui_manager_update_conversation_state(UI_STATE_IDLE,
                                                "Say 'Hey Howdy' to start again",
                                                "Ready for new conversation",
                                                0, 0, 0.0f, -1.0f);
            break;
    }
}

// Voice activation callback from UI - now used to end conversation
static void voice_activation_callback(bool start_voice)
{
    // Touch button now ends conversation instead of starting it
    if (start_voice) {
        ESP_LOGI(TAG, "🛑 Touch detected - ending conversation");
        
        if (s_app_state.howdytts_connected) {
            // Signal conversation end but keep streaming for wake word detection
            ui_manager_update_status("Conversation ended - Listening for 'Hey Howdy'");
            ui_manager_set_state(UI_STATE_IDLE);
            
            // Note: Audio streaming continues for wake word detection
            // TODO: Send special packet to notify server conversation ended
            ESP_LOGI(TAG, "User ended conversation - continuing wake word detection");
        } else {
            ESP_LOGW(TAG, "Not connected to HowdyTTS server");
            ui_manager_update_status("Not connected to server");
        }
    }
    // Button release no longer does anything
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
                    ESP_LOGI(TAG, "🧪 Running audio stream test first to verify UDP connection...");
                    vTaskDelay(pdMS_TO_TICKS(2000)); // Wait for WiFi to stabilize
                    run_audio_stream_test();
                    
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
    
    // Initialize Enhanced VAD with conversation-aware configuration
    enhanced_vad_config_t vad_config;
    enhanced_vad_get_conversation_config(16000, &vad_config);
    
    // Fine-tune for ESP32-P4 HowdyScreen environment and <50ms target latency
    vad_config.amplitude_threshold = 2300;          // Slightly more sensitive for faster response
    vad_config.silence_threshold_ms = 1000;         // 1.0s silence for faster conversation flow
    vad_config.min_voice_duration_ms = 250;         // 250ms minimum for balance of speed/accuracy
    vad_config.snr_threshold_db = 7.5f;            // Slightly lower SNR for faster response
    vad_config.consistency_frames = 4;              // Reduce consistency frames for speed
    vad_config.confidence_threshold = 0.65f;        // Balanced confidence for conversation flow
    vad_config.processing_mode = 1;                 // Optimized mode for performance
    
    s_app_state.vad_handle = enhanced_vad_init(&vad_config);
    if (s_app_state.vad_handle) {
        s_app_state.vad_initialized = true;
        ESP_LOGI(TAG, "✅ Enhanced VAD initialized successfully");
    } else {
        ESP_LOGW(TAG, "⚠️ Enhanced VAD initialization failed - continuing with basic audio");
        s_app_state.vad_initialized = false;
    }
    
    // Initialize Wake Word Detection Engine with conversation-aware configuration
    esp32_p4_wake_word_config_t wake_word_config;
    esp32_p4_wake_word_get_conversation_config(&wake_word_config);
    
    // Optimize for ESP32-P4 HowdyScreen and <50ms target latency
    wake_word_config.sample_rate = 16000;            // 16kHz audio
    wake_word_config.frame_size = 320;               // 20ms frames
    wake_word_config.energy_threshold = 2900;        // Slightly more sensitive for idle wake word detection
    wake_word_config.confidence_threshold = 0.62f;   // Lower confidence for faster response in conversation
    wake_word_config.silence_timeout_ms = 1600;      // Reduced timeout for conversation flow
    wake_word_config.enable_adaptation = true;       // Enable adaptive learning
    wake_word_config.adaptation_rate = 0.06f;        // Slightly faster adaptation
    wake_word_config.max_detections_per_min = 15;    // Allow more frequent detections in conversation
    wake_word_config.pattern_frames = 18;            // Reduce pattern frames for speed
    wake_word_config.consistency_frames = 3;         // Reduce consistency for speed
    
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
            .server_ip = "192.168.86.39",   // Updated to match discovered server
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
        .device_name = CONFIG_HOWDY_DEVICE_NAME,
        .room = CONFIG_HOWDY_DEVICE_ROOM,
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
    ESP_LOGI(TAG, "🎯 VAD Mode: %s", s_app_state.vad_initialized ? "Enhanced Conversation-Aware VAD (<50ms target)" : "Basic Audio");
    ESP_LOGI(TAG, "🎤 Wake Word: %s", s_app_state.wake_word_initialized ? "Hey Howdy Detection with Echo Cancellation" : "Disabled");
    ESP_LOGI(TAG, "⚡ Performance: Optimized for <50ms end-to-end conversation latency");
    ESP_LOGI(TAG, "🔊 Echo Suppression: Hardware (ES7210) + Software (Conversation-Aware)");
    
    // Initialize Dual I2S Manager for full-duplex audio
    ESP_LOGI(TAG, "🎵 Initializing Dual I2S Manager for full-duplex operation");
    dual_i2s_config_t dual_i2s_config;
    
    // Configure microphone I2S (ES7210 + echo cancellation)
    dual_i2s_config.mic_config.sample_rate = 16000;
    dual_i2s_config.mic_config.bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT;
    dual_i2s_config.mic_config.channel_format = I2S_SLOT_MODE_MONO;
    dual_i2s_config.mic_config.bck_pin = 12;    // BSP_I2S_SCLK
    dual_i2s_config.mic_config.ws_pin = 10;     // BSP_I2S_LCLK
    dual_i2s_config.mic_config.data_in_pin = 11; // BSP_I2S_DSIN
    
    // Configure speaker I2S (ES8311)
    dual_i2s_config.speaker_config.sample_rate = 16000;
    dual_i2s_config.speaker_config.bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT;
    dual_i2s_config.speaker_config.channel_format = I2S_SLOT_MODE_MONO;
    dual_i2s_config.speaker_config.bck_pin = 12;    // BSP_I2S_SCLK (shared)
    dual_i2s_config.speaker_config.ws_pin = 10;     // BSP_I2S_LCLK (shared)
    dual_i2s_config.speaker_config.data_out_pin = 9; // BSP_I2S_DOUT
    
    // Performance optimized DMA configuration for minimal latency (<30ms target)
    dual_i2s_config.dma_buf_count = 4;        // Reduced buffer count for lower latency
    dual_i2s_config.dma_buf_len = 160;        // 10ms buffers (160 samples @ 16kHz) for <30ms total latency
    
    esp_err_t dual_i2s_ret = dual_i2s_init(&dual_i2s_config);
    if (dual_i2s_ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ Dual I2S Manager initialized");
        ESP_LOGI(TAG, "🎤 Microphone: ES7210 with echo cancellation");
        ESP_LOGI(TAG, "🔊 Speaker: ES8311 for TTS playback");
        ESP_LOGI(TAG, "⚡ Performance Optimized: 16kHz, 16-bit, mono, 10ms buffers");
        
        // Start in microphone-only mode (will switch to simultaneous during TTS)
        dual_i2s_set_mode(DUAL_I2S_MODE_MIC);
        dual_i2s_start();
        ESP_LOGI(TAG, "🎤 Started in microphone mode - ready for wake word detection");
        
    } else {
        ESP_LOGW(TAG, "⚠️ Dual I2S Manager initialization failed: %s", esp_err_to_name(dual_i2s_ret));
    }
    
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
        
        ESP_LOGI(TAG, "🔊 TTS will use Dual I2S Manager for speaker output");
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
    strncpy(feedback_config.device_name, CONFIG_HOWDY_DEVICE_NAME, sizeof(feedback_config.device_name) - 1);
    strncpy(feedback_config.room, CONFIG_HOWDY_DEVICE_ROOM, sizeof(feedback_config.room) - 1);
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
    
    // Start performance monitoring task for real-time latency tracking
    BaseType_t perf_task_ret = xTaskCreatePinnedToCore(
        performance_monitoring_task,
        "perf_monitor",
        4096,      // Stack size
        NULL,      // Parameters
        2,         // Low priority (background monitoring)
        NULL,      // Task handle
        0          // Core 0
    );
    
    if (perf_task_ret == pdPASS) {
        ESP_LOGI(TAG, "✅ Performance monitoring task started - 30s reporting interval");
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to start performance monitoring task");
    }
    
    ESP_LOGI(TAG, "🎯 Phase 6 initialization complete!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== HowdyTTS Integration Ready ===");
    ESP_LOGI(TAG, "Protocol: Native UDP (PCM streaming)");
    ESP_LOGI(TAG, "Device: %s", "esp32p4-howdyscreen-001");
    ESP_LOGI(TAG, "Audio: 16kHz/16-bit PCM, 10ms frames (optimized for <30ms latency)");
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