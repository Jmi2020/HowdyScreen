#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HowdyTTS HTTP server configuration
 */
typedef struct {
    uint16_t port;              // HTTP server port (default 80)
    uint16_t max_open_sockets;  // Maximum open sockets
    uint32_t lru_purge_enable;  // Enable LRU purge
    char device_id[32];         // Device ID for registration
    char room[32];              // Room assignment
} howdytts_http_config_t;

/**
 * @brief HowdyTTS state received from server
 */
typedef enum {
    HOWDYTTS_STATE_WAITING = 0,   // Waiting for wake word
    HOWDYTTS_STATE_LISTENING,     // Listening for speech
    HOWDYTTS_STATE_THINKING,      // Processing on server
    HOWDYTTS_STATE_SPEAKING,      // Playing TTS response
    HOWDYTTS_STATE_ENDING         // Conversation ending
} howdytts_state_t;

/**
 * @brief State change callback function
 * 
 * Called when HowdyTTS server sends state update
 */
typedef void (*howdytts_state_callback_t)(howdytts_state_t state, const char *text);

/**
 * @brief Device discovery callback function
 * 
 * Called when HowdyTTS server discovers our device
 */
typedef void (*howdytts_discovery_callback_t)(const char *server_ip, uint16_t server_port);

/**
 * @brief Initialize HowdyTTS HTTP server
 * 
 * Creates HTTP endpoints for HowdyTTS state synchronization:
 * - POST /state - Receive state changes from HowdyTTS
 * - POST /speak - Receive speaking state with text content
 * - GET /status - Device status for discovery
 * - POST /discover - Device discovery response
 * 
 * @param config HTTP server configuration
 * @param state_callback State change callback
 * @param discovery_callback Discovery callback
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_http_server_init(const howdytts_http_config_t *config,
                                   howdytts_state_callback_t state_callback,
                                   howdytts_discovery_callback_t discovery_callback);

/**
 * @brief Start HowdyTTS HTTP server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_http_server_start(void);

/**
 * @brief Stop HowdyTTS HTTP server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_http_server_stop(void);

/**
 * @brief Update device status information
 * 
 * @param status Device status string
 * @param audio_level Current audio level (0.0-1.0)
 * @param battery_level Battery percentage (-1 for unknown)
 * @param signal_strength WiFi signal strength in dBm
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_http_update_status(const char *status, 
                                     float audio_level,
                                     int battery_level,
                                     int signal_strength);

/**
 * @brief Get current HTTP server statistics
 * 
 * @param state_requests Number of state update requests received
 * @param speak_requests Number of speak requests received
 * @param discovery_requests Number of discovery requests received
 * @param status_requests Number of status requests received
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_http_get_stats(uint32_t *state_requests,
                                 uint32_t *speak_requests,
                                 uint32_t *discovery_requests,
                                 uint32_t *status_requests);

/**
 * @brief Convert HowdyTTS state string to enum
 * 
 * @param state_str State string from HowdyTTS
 * @return howdytts_state_t Corresponding state enum
 */
howdytts_state_t howdytts_parse_state(const char *state_str);

/**
 * @brief Convert HowdyTTS state enum to string
 * 
 * @param state State enum
 * @return const char* State string
 */
const char* howdytts_state_to_string(howdytts_state_t state);

/**
 * @brief Register device with HowdyTTS server (via mDNS)
 * 
 * Advertises device as _howdyclient._tcp.local service
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_register_device(void);

/**
 * @brief Unregister device from HowdyTTS server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_unregister_device(void);

#ifdef __cplusplus
}
#endif