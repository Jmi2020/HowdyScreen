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
    UI_STATE_ERROR
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
    ui_state_t current_state;
    bool muted;
    int wifi_signal_strength;
} ui_manager_t;

/**
 * @brief Initialize UI manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_manager_init(void);

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

#ifdef __cplusplus
}
#endif