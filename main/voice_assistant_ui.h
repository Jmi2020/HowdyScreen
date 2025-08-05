#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Voice Assistant UI States
typedef enum {
    VA_UI_STATE_IDLE = 0,
    VA_UI_STATE_LISTENING,
    VA_UI_STATE_PROCESSING,
    VA_UI_STATE_SPEAKING,
    VA_UI_STATE_ERROR,
    VA_UI_STATE_CONNECTING    // New state for WiFi connection
} va_ui_state_t;

// Touch gestures for voice assistant
typedef enum {
    VA_GESTURE_NONE = 0,
    VA_GESTURE_TAP,
    VA_GESTURE_LONG_PRESS,
    VA_GESTURE_SWIPE_UP,
    VA_GESTURE_SWIPE_DOWN
} va_gesture_t;

// Voice assistant UI configuration
typedef struct {
    uint16_t display_width;
    uint16_t display_height;
    uint16_t circular_container_size;
    bool enable_animations;
    bool enable_audio_visualization;
} va_ui_config_t;

// Audio visualization data
typedef struct {
    float voice_level;          // 0.0 to 1.0
    bool voice_detected;
    float frequency_bands[8];   // 8-band frequency analysis
} va_audio_data_t;

/**
 * @brief Initialize the Voice Assistant UI system
 * 
 * @param config UI configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_init(const va_ui_config_t *config);

/**
 * @brief Set the current voice assistant state
 * 
 * @param state New UI state
 * @param animate Enable smooth transition animation
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_set_state(va_ui_state_t state, bool animate);

/**
 * @brief Update real-time audio visualization
 * 
 * @param audio_data Current audio analysis data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_update_audio_visualization(const va_audio_data_t *audio_data);

/**
 * @brief Set WiFi connection status
 * 
 * @param connected WiFi connection state
 * @param signal_strength Signal strength (0-100)
 * @param ssid WiFi network name (optional)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_set_wifi_status(bool connected, uint8_t signal_strength, const char *ssid);

/**
 * @brief Display status message
 * 
 * @param message Status message text
 * @param duration_ms Display duration in milliseconds (0 = permanent)
 * @param color Message color (RGB888)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_show_message(const char *message, uint32_t duration_ms, uint32_t color);

/**
 * @brief Get the last detected touch gesture
 * 
 * @return va_gesture_t Detected gesture
 */
va_gesture_t va_ui_get_last_gesture(void);

/**
 * @brief Enable/disable power saving mode for UI
 * 
 * @param enable Enable power saving
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_set_power_saving(bool enable);

/**
 * @brief Get current UI state
 * 
 * @return va_ui_state_t Current state
 */
va_ui_state_t va_ui_get_state(void);

/**
 * @brief Show boot/welcome screen with Howdy character
 * 
 * @param message Welcome message to display
 * @param timeout_ms How long to show boot screen (0 = indefinite)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t va_ui_show_boot_screen(const char *message, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif