#ifndef DISPLAY_INIT_H
#define DISPLAY_INIT_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Display initialization states
 */
typedef enum {
    DISPLAY_STATE_INIT,
    DISPLAY_STATE_HARDWARE_READY,
    DISPLAY_STATE_LVGL_READY,
    DISPLAY_STATE_READY,
    DISPLAY_STATE_ERROR
} display_state_t;

/**
 * @brief Initialize display hardware (BSP, LCD, Touch)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_init_hardware(void);

/**
 * @brief Initialize LVGL port and configuration
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_init_lvgl(void);

/**
 * @brief Create the initial "Ready to Connect" screen
 */
void display_create_ready_screen(void);

/**
 * @brief Update the status text and color
 * 
 * @param status_text Text to display
 * @param color Color in hex format (0xRRGGBB)
 */
void display_update_status(const char *status_text, uint32_t color);

/**
 * @brief Update the connection status text and color
 * 
 * @param connection_text Text to display
 * @param color Color in hex format (0xRRGGBB)
 */
void display_update_connection_status(const char *connection_text, uint32_t color);

/**
 * @brief Show WiFi connecting state
 */
void display_show_wifi_connecting(void);

/**
 * @brief Show WiFi connected state
 */
void display_show_wifi_connected(void);

/**
 * @brief Show error state with message
 * 
 * @param error_text Error message to display
 */
void display_show_error(const char *error_text);

/**
 * @brief Get current display state
 * 
 * @return display_state_t Current state
 */
display_state_t display_get_state(void);

/**
 * @brief Complete display initialization (hardware + LVGL + UI)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_init_complete(void);

#endif // DISPLAY_INIT_H