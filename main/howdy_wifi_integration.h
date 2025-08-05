#pragma once

#include "esp_err.h"
#include "wifi_provisioning.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi integration states
 */
typedef enum {
    WIFI_INTEGRATION_STATE_INIT,            // Initial state
    WIFI_INTEGRATION_STATE_SETUP_REQUIRED,  // WiFi setup UI should be shown
    WIFI_INTEGRATION_STATE_CONNECTING,      // Attempting to connect
    WIFI_INTEGRATION_STATE_CONNECTED,       // Successfully connected
    WIFI_INTEGRATION_STATE_DISCONNECTED,    // Disconnected from WiFi
    WIFI_INTEGRATION_STATE_AP_MODE,         // Running in AP mode
    WIFI_INTEGRATION_STATE_ERROR            // Error state
} wifi_integration_state_t;

/**
 * @brief WiFi integration event types
 */
typedef enum {
    WIFI_INTEGRATION_EVENT_INIT_DONE,       // Integration initialized
    WIFI_INTEGRATION_EVENT_PROV_INIT,       // Provisioning system initialized
    WIFI_INTEGRATION_EVENT_CONNECTED,       // WiFi connected successfully
    WIFI_INTEGRATION_EVENT_DISCONNECTED,    // WiFi disconnected
    WIFI_INTEGRATION_EVENT_CONNECTION_FAILED, // Connection attempt failed
    WIFI_INTEGRATION_EVENT_AP_MODE_STARTED, // AP mode started
    WIFI_INTEGRATION_EVENT_UI_SHOWN,        // WiFi UI is shown
    WIFI_INTEGRATION_EVENT_UI_HIDDEN,       // WiFi UI is hidden
    WIFI_INTEGRATION_EVENT_RESET,           // Integration reset
    WIFI_INTEGRATION_EVENT_ERROR            // Integration error
} wifi_integration_event_t;

/**
 * @brief WiFi integration event callback
 * 
 * @param event Integration event type
 * @param data Event data (can be NULL)
 * @param user_data User data passed during initialization
 */
typedef void (*wifi_integration_event_cb_t)(wifi_integration_event_t event, void *data, void *user_data);

/**
 * @brief Initialize HowdyScreen WiFi integration
 * 
 * This initializes both the WiFi provisioning system and the UI components.
 * 
 * @param event_cb Event callback function (can be NULL)
 * @param user_data User data passed to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_init(wifi_integration_event_cb_t event_cb, void *user_data);

/**
 * @brief Start WiFi integration
 * 
 * This will either attempt auto-connection with stored credentials
 * or show the WiFi setup UI if no credentials are found.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_start(void);

/**
 * @brief Stop WiFi integration
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_stop(void);

/**
 * @brief Show WiFi configuration UI
 * 
 * This will hide the voice assistant UI and show the WiFi setup interface.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_show_ui(void);

/**
 * @brief Hide WiFi configuration UI
 * 
 * This will hide the WiFi setup interface and show the voice assistant UI.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_hide_ui(void);

/**
 * @brief Get current WiFi integration state
 * 
 * @return wifi_integration_state_t Current state
 */
wifi_integration_state_t howdy_wifi_integration_get_state(void);

/**
 * @brief Check if WiFi is connected
 * 
 * @return bool true if connected to WiFi
 */
bool howdy_wifi_integration_is_connected(void);

/**
 * @brief Reset WiFi integration
 * 
 * This clears all stored credentials and resets to initial state.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_reset(void);

/**
 * @brief Get current WiFi connection information
 * 
 * @param info Output connection info structure
 * @return esp_err_t ESP_OK if connected and info retrieved
 */
esp_err_t howdy_wifi_integration_get_connection_info(wifi_connection_info_t *info);

/**
 * @brief Deinitialize WiFi integration
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdy_wifi_integration_deinit(void);

#ifdef __cplusplus
}
#endif