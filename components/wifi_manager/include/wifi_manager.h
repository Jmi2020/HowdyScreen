#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi states
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR
} wifi_state_t;

// WiFi event IDs
typedef enum {
    WIFI_EVENT_CONNECTED_ID = 0,
    WIFI_EVENT_DISCONNECTED_ID,
    WIFI_EVENT_SCAN_DONE_ID,
    WIFI_EVENT_GOT_IP_ID,
} wifi_event_id_t;

// WiFi callback function
typedef void (*wifi_event_callback_t)(wifi_event_id_t event_id, void *event_data);

/**
 * @brief Initialize WiFi manager
 * 
 * @param event_callback Optional callback for WiFi events
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_init(wifi_event_callback_t event_callback);

/**
 * @brief Connect to WiFi access point
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from WiFi
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Get current WiFi state
 * 
 * @return wifi_state_t Current state
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Get WiFi signal strength (RSSI)
 * 
 * @param rssi Output RSSI value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_get_rssi(int8_t *rssi);

/**
 * @brief Get WiFi signal strength as percentage
 * 
 * @return int Signal strength (0-100%)
 */
int wifi_manager_get_signal_strength(void);

/**
 * @brief Get IP address as string
 * 
 * @param ip_str Buffer to store IP string (min 16 bytes)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_get_ip_str(char *ip_str);

/**
 * @brief Check if WiFi is connected
 * 
 * @return bool true if connected
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Start WiFi scan
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_start_scan(void);

/**
 * @brief Auto-connect using stored credentials
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_manager_auto_connect(void);

#ifdef __cplusplus
}
#endif