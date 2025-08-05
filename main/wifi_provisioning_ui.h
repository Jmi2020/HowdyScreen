#pragma once

#include "lvgl.h"
#include "wifi_provisioning.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi provisioning UI states
 */
typedef enum {
    WIFI_UI_STATE_INIT,              // Initial state
    WIFI_UI_STATE_SCANNING,          // Scanning for networks
    WIFI_UI_STATE_NETWORK_LIST,      // Showing available networks
    WIFI_UI_STATE_MANUAL_ENTRY,      // Manual SSID/password entry
    WIFI_UI_STATE_CONNECTING,        // Attempting connection
    WIFI_UI_STATE_CONNECTED,         // Successfully connected
    WIFI_UI_STATE_AP_MODE,           // Running in AP mode
    WIFI_UI_STATE_ERROR              // Error state
} wifi_ui_state_t;

/**
 * @brief WiFi provisioning UI configuration
 */
typedef struct {
    lv_obj_t *parent;               // Parent container for UI
    bool show_signal_strength;      // Show signal strength indicators
    bool show_security_icons;       // Show security type icons
    uint16_t list_item_height;      // Height of network list items
    uint16_t max_networks_shown;    // Maximum networks to display
} wifi_ui_config_t;

/**
 * @brief WiFi provisioning UI callback events
 */
typedef enum {
    WIFI_UI_EVENT_NETWORK_SELECTED, // Network selected from list
    WIFI_UI_EVENT_CREDENTIALS_ENTERED, // Manual credentials entered
    WIFI_UI_EVENT_SCAN_REQUESTED,   // User requested network scan
    WIFI_UI_EVENT_AP_MODE_REQUESTED, // User requested AP mode
    WIFI_UI_EVENT_BACK_PRESSED,     // Back button pressed
    WIFI_UI_EVENT_CONNECT_PRESSED   // Connect button pressed
} wifi_ui_event_t;

/**
 * @brief WiFi UI event data structure
 */
typedef struct {
    wifi_ui_event_t event;
    union {
        struct {
            char ssid[32];
            int8_t rssi;
            wifi_auth_mode_t auth_mode;
        } network_selected;
        struct {
            char ssid[32];
            char password[64];
        } credentials_entered;
    } data;
} wifi_ui_event_data_t;

/**
 * @brief WiFi provisioning UI event callback
 */
typedef void (*wifi_ui_event_cb_t)(wifi_ui_event_data_t *event_data, void *user_data);

/**
 * @brief Initialize WiFi provisioning UI
 * 
 * @param config UI configuration
 * @param event_cb Event callback function
 * @param user_data User data passed to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_init(const wifi_ui_config_t *config, wifi_ui_event_cb_t event_cb, void *user_data);

/**
 * @brief Set WiFi UI state
 * 
 * @param state New UI state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_set_state(wifi_ui_state_t state);

/**
 * @brief Get current WiFi UI state
 * 
 * @return wifi_ui_state_t Current state
 */
wifi_ui_state_t wifi_ui_get_state(void);

/**
 * @brief Update network list display
 * 
 * @param ap_records Array of access point records
 * @param count Number of networks
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_update_network_list(wifi_ap_record_t *ap_records, uint16_t count);

/**
 * @brief Show connection progress
 * 
 * @param ssid SSID being connected to
 * @param progress_percent Progress percentage (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_connection_progress(const char *ssid, uint8_t progress_percent);

/**
 * @brief Show connection success
 * 
 * @param connection_info Connection information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_connection_success(const wifi_connection_info_t *connection_info);

/**
 * @brief Show connection error
 * 
 * @param error_message Error message to display
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_connection_error(const char *error_message);

/**
 * @brief Show AP mode information
 * 
 * @param ap_ssid AP mode SSID
 * @param ap_password AP mode password
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_ap_mode_info(const char *ap_ssid, const char *ap_password);

/**
 * @brief Show manual entry screen
 * 
 * @param prefill_ssid Optional SSID to prefill
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_manual_entry(const char *prefill_ssid);

/**
 * @brief Generate and show QR code for WiFi credentials
 * 
 * @param ssid WiFi SSID  
 * @param password WiFi password
 * @param auth_type Security type
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_show_qr_code(const char *ssid, const char *password, const char *auth_type);

/**
 * @brief Update signal strength indicator
 * 
 * @param rssi Signal strength in dBm
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_update_signal_strength(int8_t rssi);

/**
 * @brief Get default WiFi UI configuration
 * 
 * @param config Output configuration structure
 */
void wifi_ui_get_default_config(wifi_ui_config_t *config);

/**
 * @brief Cleanup WiFi provisioning UI
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ui_deinit(void);

#ifdef __cplusplus
}
#endif