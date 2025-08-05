#pragma once

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi provisioning states
 */
typedef enum {
    WIFI_PROV_STATE_INIT,           // Initial state, not started
    WIFI_PROV_STATE_STARTING,       // Starting WiFi and checking for stored credentials
    WIFI_PROV_STATE_CONNECTING,     // Attempting to connect with stored/provided credentials
    WIFI_PROV_STATE_CONNECTED,      // Successfully connected to WiFi
    WIFI_PROV_STATE_DISCONNECTED,   // Disconnected from WiFi
    WIFI_PROV_STATE_AP_MODE,        // Running in AP mode for configuration
    WIFI_PROV_STATE_PROVISIONING,   // Actively provisioning via AP mode
    WIFI_PROV_STATE_ERROR           // Error state, provisioning failed
} wifi_prov_state_t;

/**
 * @brief WiFi provisioning event types
 */
typedef enum {
    WIFI_PROV_EVENT_INIT_DONE,      // Provisioning system initialized
    WIFI_PROV_EVENT_CONNECTING,     // Started connecting to WiFi
    WIFI_PROV_EVENT_CONNECTED,      // Successfully connected to WiFi
    WIFI_PROV_EVENT_DISCONNECTED,   // Disconnected from WiFi
    WIFI_PROV_EVENT_AP_MODE_START,  // Started AP mode for provisioning
    WIFI_PROV_EVENT_AP_MODE_STOP,   // Stopped AP mode
    WIFI_PROV_EVENT_CRED_RECV,      // Received new WiFi credentials
    WIFI_PROV_EVENT_CRED_SUCCESS,   // Successfully connected with new credentials
    WIFI_PROV_EVENT_CRED_FAIL,      // Failed to connect with new credentials
    WIFI_PROV_EVENT_ERROR           // Provisioning error occurred
} wifi_prov_event_t;

/**
 * @brief WiFi credentials structure
 */
typedef struct {
    char ssid[32];                  // WiFi SSID
    char password[64];              // WiFi password
    bool valid;                     // Credentials are valid
} wifi_credentials_t;

/**
 * @brief WiFi connection info
 */
typedef struct {
    char ip_address[16];            // Current IP address
    char gateway[16];               // Gateway IP address  
    char netmask[16];               // Network mask
    int rssi;                       // Signal strength in dBm
    uint8_t channel;                // WiFi channel
    char connected_ssid[32];        // Currently connected SSID
    uint32_t connection_time;       // Time connected in seconds
} wifi_connection_info_t;

/**
 * @brief WiFi provisioning configuration
 */
typedef struct {
    char ap_ssid[32];               // AP mode SSID (default: "HowdyScreen-XXXX")
    char ap_password[64];           // AP mode password (default: "howdyscreen")
    uint8_t ap_channel;             // AP mode channel (default: 1)
    uint8_t ap_max_connections;     // Max AP connections (default: 4)
    bool auto_connect;              // Auto-connect on boot (default: true)
    uint32_t connect_timeout_ms;    // Connection timeout (default: 30000)
    uint32_t retry_attempts;        // Max retry attempts (default: 5)
    uint32_t retry_delay_ms;        // Delay between retries (default: 5000)
} wifi_prov_config_t;

/**
 * @brief WiFi provisioning event callback function type
 * 
 * @param event WiFi provisioning event
 * @param data Event data (can be NULL)
 * @param user_data User data passed during registration
 */
typedef void (*wifi_prov_event_cb_t)(wifi_prov_event_t event, void *data, void *user_data);

/**
 * @brief Initialize WiFi provisioning system
 * 
 * @param config Provisioning configuration (NULL for defaults)
 * @param event_cb Event callback function (can be NULL)
 * @param user_data User data passed to callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_init(const wifi_prov_config_t *config, wifi_prov_event_cb_t event_cb, void *user_data);

/**
 * @brief Start WiFi provisioning
 * 
 * This will attempt to connect using stored credentials first.
 * If no credentials are found or connection fails, it will start AP mode.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_start(void);

/**
 * @brief Stop WiFi provisioning
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_stop(void);

/**
 * @brief Set WiFi credentials manually
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @param save_to_nvs Save credentials to NVS for persistence
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_set_credentials(const char *ssid, const char *password, bool save_to_nvs);

/**
 * @brief Get stored WiFi credentials
 * 
 * @param credentials Output credentials structure
 * @return esp_err_t ESP_OK if credentials exist and are valid
 */
esp_err_t wifi_prov_get_credentials(wifi_credentials_t *credentials);

/**
 * @brief Clear stored WiFi credentials
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_clear_credentials(void);

/**
 * @brief Get current WiFi provisioning state
 * 
 * @return wifi_prov_state_t Current state
 */
wifi_prov_state_t wifi_prov_get_state(void);

/**
 * @brief Get WiFi connection information
 * 
 * @param info Output connection info structure
 * @return esp_err_t ESP_OK if connected and info retrieved
 */
esp_err_t wifi_prov_get_connection_info(wifi_connection_info_t *info);

/**
 * @brief Check if WiFi is connected
 * 
 * @return bool true if connected to WiFi
 */
bool wifi_prov_is_connected(void);

/**
 * @brief Force start AP mode for provisioning
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_start_ap_mode(void);

/**
 * @brief Stop AP mode and return to normal WiFi operation
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_stop_ap_mode(void);

/**
 * @brief Scan for available WiFi networks
 * 
 * @param ap_records Output array of AP records
 * @param max_aps Maximum number of APs to scan
 * @param num_aps Output number of APs found
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_scan_networks(wifi_ap_record_t *ap_records, uint16_t max_aps, uint16_t *num_aps);

/**
 * @brief Get default WiFi provisioning configuration
 * 
 * @param config Output configuration structure
 */
void wifi_prov_get_default_config(wifi_prov_config_t *config);

/**
 * @brief Reset WiFi provisioning system
 * 
 * Clears all stored credentials and resets to initial state.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_prov_reset(void);

#ifdef __cplusplus
}
#endif