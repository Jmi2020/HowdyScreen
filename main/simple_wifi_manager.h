#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi connection callback function
 * 
 * @param connected true if connected, false if disconnected
 * @param info Connection info (IP address if connected, error message if disconnected)
 */
typedef void (*wifi_connection_callback_t)(bool connected, const char *info);

/**
 * @brief Initialize simple WiFi manager
 * 
 * This initializes the WiFi system for ESP32-P4 with ESP32-C6 co-processor support.
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t simple_wifi_init(void);

/**
 * @brief Connect to WiFi network
 * 
 * @param ssid WiFi network name
 * @param password WiFi password
 * @param callback Optional callback for connection status updates
 * @return esp_err_t ESP_OK on success
 */
esp_err_t simple_wifi_connect(const char *ssid, const char *password, wifi_connection_callback_t callback);

/**
 * @brief Wait for WiFi connection to complete
 * 
 * @param timeout_ms Timeout in milliseconds
 * @return esp_err_t ESP_OK if connected, ESP_FAIL if failed, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t simple_wifi_wait_connected(uint32_t timeout_ms);

/**
 * @brief Check if WiFi is connected
 * 
 * @return bool true if connected
 */
bool simple_wifi_is_connected(void);

/**
 * @brief Disconnect from WiFi
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t simple_wifi_disconnect(void);

/**
 * @brief Get IP information
 * 
 * @param ip_info Output IP information structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t simple_wifi_get_ip_info(esp_netif_ip_info_t *ip_info);

/**
 * @brief Get WiFi signal strength
 * 
 * @return int RSSI value in dBm (-100 if not connected)
 */
int simple_wifi_get_rssi(void);

#ifdef __cplusplus
}
#endif