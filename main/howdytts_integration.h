#pragma once

#include "esp_err.h"
#include "network_manager.h"
#include "howdytts_http_server.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HowdyTTS integration configuration
 */
typedef struct {
    // Network configuration
    const char *wifi_ssid;
    const char *wifi_password;
    
    // HowdyTTS server configuration
    const char *server_ip;          // Manual server IP (if not using discovery)
    uint16_t server_ws_port;        // WebSocket port (default: 8765)
    uint16_t server_udp_port;       // UDP audio port (default: 8000)
    uint16_t local_http_port;       // Local HTTP server port (default: 80)
    uint16_t local_udp_port;        // Local UDP port (default: 8001)
    
    // Device configuration
    const char *device_id;          // Unique device identifier
    const char *room;               // Room assignment
    
    // Feature flags
    bool enable_mdns_discovery;     // Enable automatic server discovery
    bool enable_udp_streaming;      // Enable UDP audio streaming
    bool enable_websocket;          // Enable WebSocket control channel
    bool enable_http_server;        // Enable HTTP state endpoints
} howdytts_integration_config_t;

/**
 * @brief HowdyTTS connection status
 */
typedef struct {
    bool wifi_connected;
    bool server_discovered;
    bool websocket_connected;
    bool udp_ready;
    bool http_server_running;
    char server_ip[16];
    uint16_t server_ws_port;
    uint16_t server_udp_port;
} howdytts_connection_status_t;

/**
 * @brief Initialize HowdyTTS integration
 * 
 * Sets up all networking components for HowdyTTS communication
 * 
 * @param config Integration configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_integration_init(const howdytts_integration_config_t *config);

/**
 * @brief Start HowdyTTS integration
 * 
 * Connects to WiFi, discovers server, and establishes communication
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_integration_start(void);

/**
 * @brief Stop HowdyTTS integration
 * 
 * Disconnects and stops all services
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_integration_stop(void);

/**
 * @brief Get connection status
 * 
 * @param status Output status structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_integration_get_status(howdytts_connection_status_t *status);

/**
 * @brief Handle state change from HowdyTTS
 * 
 * Called when server sends state update
 * 
 * @param state New state
 * @param text Optional text (for speaking state)
 */
void howdytts_integration_handle_state(howdytts_state_t state, const char *text);

/**
 * @brief Send audio level update
 * 
 * @param level Audio level (0.0-1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_integration_update_audio_level(float level);

/**
 * @brief Check if ready for audio streaming
 * 
 * @return true if at least one audio transport is available
 */
bool howdytts_integration_is_audio_ready(void);

#ifdef __cplusplus
}
#endif