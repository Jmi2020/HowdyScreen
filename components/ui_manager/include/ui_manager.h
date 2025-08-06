#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_STATE_INIT,
    UI_STATE_IDLE,
    UI_STATE_LISTENING,
    UI_STATE_PROCESSING,
    UI_STATE_SPEAKING,
    UI_STATE_ERROR,
    // HowdyTTS specific states
    UI_STATE_WAKE_WORD_DETECTED,
    UI_STATE_SPEECH_DETECTED,
    UI_STATE_THINKING,
    UI_STATE_RESPONDING,
    UI_STATE_SESSION_ENDING,
    // Network states
    UI_STATE_CONNECTING,
    UI_STATE_DISCOVERING,
    UI_STATE_REGISTERED
} ui_state_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *audio_meter;      // Real-time audio level display
    lv_obj_t *status_label;     // Connection/processing status
    lv_obj_t *wifi_label;       // Network status
    lv_obj_t *level_arc;        // Circular audio level meter
    lv_obj_t *center_button;    // Mute/unmute control
    lv_obj_t *howdy_character;  // Howdy character image for state visualization
    lv_obj_t *mic_icon;         // Microphone icon
    lv_obj_t *protocol_indicator;  // Shows UDP/WebSocket protocol
    lv_obj_t *server_info;      // Connected server information
    ui_state_t current_state;
    bool muted;
    int wifi_signal_strength;
    // HowdyTTS Integration
    char connected_server[64];  // Currently connected HowdyTTS server
    bool howdytts_connected;    // HowdyTTS connection status
    bool dual_protocol_mode;    // Dual protocol mode enabled
    bool using_websocket;       // Currently using WebSocket vs UDP
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
 * @brief Show voice assistant state with enhanced feedback
 * This function provides rich visual feedback for the smart audio interface
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

#ifdef __cplusplus
}
#endif