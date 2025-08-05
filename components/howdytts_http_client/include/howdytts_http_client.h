#pragma once

#include "esp_err.h"
#include "service_discovery.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HowdyTTS server health status
typedef struct {
    bool online;                    // Server is responding
    uint32_t response_time_ms;      // HTTP response time
    float cpu_usage;                // Server CPU usage (0.0-1.0)
    float memory_usage;             // Server memory usage (0.0-1.0)
    uint32_t active_sessions;       // Number of active voice sessions
    char version[16];               // Server version string
    char status[32];                // Server status message
    uint32_t last_check;            // Last health check timestamp (ms)
} howdytts_server_health_t;

// HowdyTTS client configuration
typedef struct {
    char device_id[32];             // Unique device identifier
    char device_name[64];           // Human-readable device name
    char capabilities[128];         // Device capabilities string
    uint32_t health_check_interval; // Health check interval (ms)
    uint32_t request_timeout;       // HTTP request timeout (ms)
    bool auto_reconnect;            // Auto-reconnect on failure
} howdytts_client_config_t;

// Server response callback function
typedef void (*howdytts_response_callback_t)(const char *response, size_t length, void *user_data);

// Health status callback function  
typedef void (*howdytts_health_callback_t)(const howdytts_server_info_t *server, 
                                          const howdytts_server_health_t *health);

/**
 * @brief Initialize HowdyTTS HTTP client
 * 
 * @param config Client configuration
 * @param health_callback Health status callback (optional)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_init(const howdytts_client_config_t *config,
                              howdytts_health_callback_t health_callback);

/**
 * @brief Perform health check on a HowdyTTS server
 * 
 * @param server_info Server to check
 * @param health Output health information
 * @return esp_err_t ESP_OK if server is healthy
 */
esp_err_t howdytts_client_health_check(const howdytts_server_info_t *server_info,
                                      howdytts_server_health_t *health);

/**
 * @brief Get server configuration and capabilities
 * 
 * @param server_info Server to query
 * @param callback Response callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_get_config(const howdytts_server_info_t *server_info,
                                    howdytts_response_callback_t callback,
                                    void *user_data);

/**
 * @brief Register this device with a HowdyTTS server
 * 
 * @param server_info Server to register with
 * @param callback Response callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_register_device(const howdytts_server_info_t *server_info,
                                         howdytts_response_callback_t callback,
                                         void *user_data);

/**
 * @brief Unregister this device from a HowdyTTS server
 * 
 * @param server_info Server to unregister from
 * @param callback Response callback function  
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_unregister_device(const howdytts_server_info_t *server_info,
                                           howdytts_response_callback_t callback,
                                           void *user_data);

/**
 * @brief Start a voice session with HowdyTTS server
 * 
 * @param server_info Server to connect to
 * @param session_config Session configuration JSON
 * @param callback Response callback function
 * @param user_data User data for callback  
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_start_session(const howdytts_server_info_t *server_info,
                                       const char *session_config,
                                       howdytts_response_callback_t callback,
                                       void *user_data);

/**
 * @brief End a voice session with HowdyTTS server
 * 
 * @param server_info Server to disconnect from
 * @param session_id Session ID to end
 * @param callback Response callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success  
 */
esp_err_t howdytts_client_end_session(const howdytts_server_info_t *server_info,
                                     const char *session_id,
                                     howdytts_response_callback_t callback,
                                     void *user_data);

/**
 * @brief Send text message to HowdyTTS server for processing
 * 
 * @param server_info Server to send to
 * @param session_id Active session ID
 * @param text Text to process
 * @param callback Response callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_send_text(const howdytts_server_info_t *server_info,
                                   const char *session_id,
                                   const char *text,
                                   howdytts_response_callback_t callback,
                                   void *user_data);

/**
 * @brief Get server statistics and performance metrics
 * 
 * @param server_info Server to query
 * @param callback Response callback function
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_get_stats(const howdytts_server_info_t *server_info,
                                   howdytts_response_callback_t callback,
                                   void *user_data);

/**
 * @brief Start periodic health monitoring of discovered servers
 * 
 * @param interval_ms Health check interval in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_start_health_monitor(uint32_t interval_ms);

/**
 * @brief Stop periodic health monitoring
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_stop_health_monitor(void);

/**
 * @brief Get current client statistics
 * 
 * @param requests_sent Total HTTP requests sent
 * @param requests_failed Total HTTP requests failed
 * @param avg_response_time Average response time in ms
 * @param servers_monitored Number of servers being monitored
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_get_stats_local(uint32_t *requests_sent,
                                         uint32_t *requests_failed,
                                         uint32_t *avg_response_time,
                                         uint32_t *servers_monitored);

/**
 * @brief Test connectivity to all discovered HowdyTTS servers
 * 
 * @param callback Called for each server test result
 * @param user_data User data for callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_test_all_servers(howdytts_response_callback_t callback,
                                          void *user_data);

#ifdef __cplusplus
}
#endif