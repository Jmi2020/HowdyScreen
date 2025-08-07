#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // System states
    UI_STATE_INIT,
    UI_STATE_IDLE,
    UI_STATE_ERROR,
    
    // Conversation states
    UI_STATE_WAKE_WORD_DETECTED,
    UI_STATE_LISTENING,
    UI_STATE_SPEECH_DETECTED,
    UI_STATE_PROCESSING,
    UI_STATE_THINKING,
    UI_STATE_SPEAKING,
    UI_STATE_RESPONDING,
    UI_STATE_CONVERSATION_ACTIVE,
    UI_STATE_SESSION_ENDING,
    
    // Network states
    UI_STATE_CONNECTING,
    UI_STATE_DISCOVERING,
    UI_STATE_REGISTERED,
    UI_STATE_DISCONNECTED
} ui_state_t;

typedef struct {
    // Screen objects
    lv_obj_t *screen;
    lv_obj_t *main_container;
    
    // Audio visualization (circular for round display)
    lv_obj_t *outer_audio_ring;     // Microphone input levels
    lv_obj_t *inner_audio_ring;     // TTS output levels
    lv_obj_t *level_arc;            // Combined audio level meter
    lv_obj_t *wake_word_ring;       // Wake word detection pulse
    
    // Central character and controls
    lv_obj_t *howdy_character;      // Howdy character image for state visualization
    lv_obj_t *center_button;        // Touch control for conversation
    lv_obj_t *character_glow;       // Glow effect around character
    
    // Status displays
    lv_obj_t *status_label;         // Current conversation state
    lv_obj_t *status_detail;        // Detailed status information
    lv_obj_t *mic_icon;             // Microphone status icon
    lv_obj_t *confidence_meter;     // VAD/wake word confidence display
    
    // Network and system info
    lv_obj_t *wifi_label;           // WiFi connection status
    lv_obj_t *server_info;          // HowdyTTS server information
    lv_obj_t *protocol_indicator;   // Current protocol (UDP/WebSocket)
    lv_obj_t *system_label;         // System version/info
    
    // Touch zones for round display
    lv_obj_t *volume_touch_up;      // Upper arc for volume up
    lv_obj_t *volume_touch_down;    // Lower arc for volume down
    lv_obj_t *gesture_zone;         // Full screen gesture detection
    
    // State management
    ui_state_t current_state;
    ui_state_t previous_state;
    uint32_t state_change_time;
    bool in_conversation;
    
    // Audio levels and feedback
    int mic_level;                  // Current microphone level (0-100)
    int tts_level;                  // Current TTS output level (0-100)
    float vad_confidence;           // VAD confidence (0.0-1.0)
    float wake_word_confidence;     // Wake word confidence (0.0-1.0)
    
    // System status
    bool muted;
    int wifi_signal_strength;
    bool howdytts_connected;
    bool dual_protocol_mode;
    bool using_websocket;
    char connected_server[64];
    
    // Animation state
    bool listening_animation_active;
    bool processing_animation_active;
    bool wake_word_animation_active;
    uint16_t animation_step;
} ui_manager_t;

/**
 * @brief Voice activation callback function type
 */
typedef void (*ui_voice_activation_callback_t)(bool start_voice);

/**
 * @brief Initialize UI manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_init(void);

/**
 * @brief Set voice activation callback
 * 
 * @param callback Callback function for voice activation
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_voice_callback(ui_voice_activation_callback_t callback);

/**
 * @brief Set UI state
 * 
 * @param state New UI state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_state(ui_state_t state);

/**
 * @brief Update audio level display
 * 
 * @param level Audio level (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_audio_level(int level);

/**
 * @brief Set WiFi signal strength
 * 
 * @param strength Signal strength (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_wifi_strength(int strength);

/**
 * @brief Set mute state
 * 
 * @param muted True if muted
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_mute(bool muted);

/**
 * @brief Update status text
 * 
 * @param status Status text to display
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_status(const char *status);

/**
 * @brief Get current UI state
 * 
 * @return ui_state_t Current UI state
 */
ui_state_t ui_manager_get_state(void);

/**
 * @brief Get mute state
 * 
 * @return true if muted
 */
bool ui_manager_is_muted(void);

/**
 * @brief Update complete conversation state with audio levels and confidence
 * 
 * @param state New conversation state
 * @param status_text Primary status message
 * @param detail_text Detailed status information (can be NULL)
 * @param mic_level Microphone input level (0-100)
 * @param tts_level TTS output level (0-100)
 * @param vad_confidence VAD confidence score (0.0-1.0)
 * @param wake_word_confidence Wake word confidence (0.0-1.0, -1.0 if not applicable)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_conversation_state(ui_state_t state,
                                              const char *status_text,
                                              const char *detail_text,
                                              int mic_level,
                                              int tts_level,
                                              float vad_confidence,
                                              float wake_word_confidence);

/**
 * @brief Show voice assistant state with enhanced feedback (legacy compatibility)
 * 
 * @param state_name State name (e.g., "LISTENING", "PROCESSING", "SPEAKING")
 * @param status_text Detailed status text
 * @param audio_level Current audio level (0.0 to 1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_show_voice_assistant_state(const char *state_name, 
                                               const char *status_text, 
                                               float audio_level);

/**
 * @brief Start listening animation (pulsing effect)
 */
esp_err_t ui_manager_start_listening_animation(void);

/**
 * @brief Stop listening animation
 */
esp_err_t ui_manager_stop_listening_animation(void);

/**
 * @brief Start processing animation (spinner)
 */
esp_err_t ui_manager_start_processing_animation(void);

/**
 * @brief Stop processing animation
 */
esp_err_t ui_manager_stop_processing_animation(void);

/**
 * @brief Start breathing animation for idle state
 */
esp_err_t ui_manager_start_breathing_animation(void);

/**
 * @brief Stop breathing animation
 */
esp_err_t ui_manager_stop_breathing_animation(void);

/**
 * @brief Set HowdyTTS connection status
 * 
 * @param connected HowdyTTS connection status
 * @param server_name Connected server name
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_howdytts_status(bool connected, const char *server_name);

/**
 * @brief Set dual protocol mode status
 * 
 * @param dual_mode Dual protocol mode enabled
 * @param using_websocket Currently using WebSocket (vs UDP)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_protocol_status(bool dual_mode, bool using_websocket);

/**
 * @brief Update HowdyTTS conversation state with enhanced feedback
 * 
 * @param howdy_state HowdyTTS conversation state
 * @param text Associated text (transcript/response)
 * @param confidence Confidence score (0.0-1.0)
 * @param audio_level Current audio level (0.0-1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_howdytts_state(int howdy_state, const char *text, float confidence, float audio_level);

/**
 * @brief Show network discovery progress
 * 
 * @param discovering Currently discovering servers
 * @param servers_found Number of servers found
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_show_discovery_progress(bool discovering, int servers_found);

/**
 * @brief Show protocol switch animation
 * 
 * @param from_protocol Source protocol ("UDP" or "WebSocket")
 * @param to_protocol Target protocol ("UDP" or "WebSocket")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_show_protocol_switch(const char *from_protocol, const char *to_protocol);

/**
 * @brief Update microphone audio level with real-time display
 * 
 * @param level Microphone level (0-100)
 * @param vad_confidence VAD confidence (0.0-1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_mic_level(int level, float vad_confidence);

/**
 * @brief Update TTS audio level with playback visualization
 * 
 * @param level TTS output level (0-100)
 * @param progress Playback progress (0.0-1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_tts_level(int level, float progress);

/**
 * @brief Show wake word detection with pulsing animation
 * 
 * @param confidence Wake word confidence (0.0-1.0)
 * @param phrase_detected Detected phrase text (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_show_wake_word_detection(float confidence, const char *phrase_detected);

/**
 * @brief Start wake word detection animation (pulsing rings)
 */
esp_err_t ui_manager_start_wake_word_animation(void);

/**
 * @brief Stop wake word detection animation
 */
esp_err_t ui_manager_stop_wake_word_animation(void);

/**
 * @brief Update conversation progress indicator
 * 
 * @param in_conversation True if actively in conversation
 * @param turns_completed Number of conversation turns completed
 * @param estimated_remaining Estimated remaining time in seconds (-1 if unknown)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_update_conversation_progress(bool in_conversation, 
                                                 int turns_completed, 
                                                 int estimated_remaining);

/**
 * @brief Handle touch gesture for conversation control
 * 
 * @param gesture_type Type of gesture (tap, long_press, swipe_up, swipe_down)
 * @param gesture_data Gesture-specific data (position, direction, etc.)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_handle_touch_gesture(const char *gesture_type, void *gesture_data);

/**
 * @brief Set conversation context for UI adaptation
 * 
 * @param context Conversation context (idle, listening, speaking, processing)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_set_conversation_context(int context);

/**
 * @brief Get current conversation state information
 * 
 * @param in_conversation Output: true if in active conversation
 * @param current_confidence Output: current confidence level
 * @param audio_levels Output: current audio levels [mic, tts]
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_get_conversation_info(bool *in_conversation, 
                                          float *current_confidence, 
                                          int audio_levels[2]);

/**
 * @brief Show error state with recovery information
 * 
 * @param error_type Type of error (network, audio, server, etc.)
 * @param error_message Human-readable error message
 * @param recovery_time Estimated recovery time in seconds (-1 if unknown)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_show_error_with_recovery(const char *error_type,
                                             const char *error_message,
                                             int recovery_time);

#ifdef __cplusplus
}
#endif