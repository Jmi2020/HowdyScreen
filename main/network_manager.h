#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

typedef enum {
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_ERROR
} network_state_t;

typedef struct {
    char ssid[32];
    char password[64];
    char server_ip[16];
    uint16_t server_port;
    int udp_socket;
    network_state_t state;
    bool initialized;
} network_manager_t;

typedef struct {
    uint32_t timestamp;
    uint16_t sequence;
    uint16_t data_size;
    uint8_t data[];
} audio_packet_t;

/**
 * Initialize WiFi and network components
 */
esp_err_t network_manager_init(network_manager_t *manager, const char *ssid, const char *password, const char *server_ip, uint16_t port);

/**
 * Start WiFi connection
 */
esp_err_t network_manager_connect(network_manager_t *manager);

/**
 * Disconnect from WiFi
 */
esp_err_t network_manager_disconnect(network_manager_t *manager);

/**
 * Get current network state
 */
network_state_t network_manager_get_state(network_manager_t *manager);

/**
 * Send audio data via UDP
 */
esp_err_t network_send_audio(network_manager_t *manager, const int16_t *audio_data, size_t frames);

/**
 * Receive audio data via UDP (non-blocking)
 */
int network_receive_audio(network_manager_t *manager, int16_t *audio_buffer, size_t max_frames);

/**
 * Send control message via UDP
 */
esp_err_t network_send_control(network_manager_t *manager, const char *message);

/**
 * Get WiFi signal strength
 */
int network_get_rssi(void);

/**
 * Deinitialize network manager
 */
esp_err_t network_manager_deinit(network_manager_t *manager);

#endif // NETWORK_MANAGER_H