#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HowdyTTS server information
typedef struct {
    char ip_addr[16];        // IPv4 address string
    uint16_t port;           // WebSocket port
    char hostname[64];       // Server hostname
    char version[16];        // Server version
    bool secure;             // Whether to use WSS
    uint32_t last_seen;      // Last seen timestamp (ms)
} howdytts_server_info_t;

// Service discovery callback function
typedef void (*service_discovered_callback_t)(const howdytts_server_info_t *server_info);

/**
 * @brief Initialize mDNS service discovery for HowdyTTS servers
 * 
 * @param callback Callback function called when servers are discovered
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_init(service_discovered_callback_t callback);

/**
 * @brief Start scanning for HowdyTTS servers on the network
 * 
 * @param scan_duration_ms How long to scan (0 = continuous)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_start_scan(uint32_t scan_duration_ms);

/**
 * @brief Stop scanning for HowdyTTS servers
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_stop_scan(void);

/**
 * @brief Get list of discovered HowdyTTS servers
 * 
 * @param servers Output array for server information
 * @param max_servers Maximum number of servers to return
 * @param num_servers Number of servers found
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_get_servers(howdytts_server_info_t *servers, 
                                       size_t max_servers, size_t *num_servers);

/**
 * @brief Get the best HowdyTTS server (lowest latency, most recent)
 * 
 * @param server_info Output server information
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if no servers
 */
esp_err_t service_discovery_get_best_server(howdytts_server_info_t *server_info);

/**
 * @brief Advertise this ESP32 as a HowdyTTS client on the network
 * 
 * @param device_name Name to advertise
 * @param capabilities Client capabilities string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_advertise_client(const char *device_name, const char *capabilities);

/**
 * @brief Stop advertising this client
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t service_discovery_stop_advertising(void);

/**
 * @brief Check connectivity to a specific HowdyTTS server
 * 
 * @param server_info Server to test
 * @param timeout_ms Test timeout in milliseconds
 * @param latency_ms Output measured latency
 * @return esp_err_t ESP_OK if reachable
 */
esp_err_t service_discovery_test_server(const howdytts_server_info_t *server_info, 
                                       uint32_t timeout_ms, uint32_t *latency_ms);

#ifdef __cplusplus
}
#endif