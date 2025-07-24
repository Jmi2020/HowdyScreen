#ifndef SERVER_DISCOVERY_H
#define SERVER_DISCOVERY_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_SERVER_IPS 5
#define MAX_IP_LENGTH 16
#define MDNS_SERVICE_NAME "_howdytts"
#define MDNS_SERVICE_PROTO "_udp"
#define MDNS_INSTANCE_NAME "HowdyTTS Server"

typedef struct {
    char ips[MAX_SERVER_IPS][MAX_IP_LENGTH];
    int count;
    int current_index;
    bool mdns_enabled;
    char discovered_ip[MAX_IP_LENGTH];
    uint16_t discovered_port;
} server_discovery_t;

/**
 * Initialize server discovery with fallback IPs
 */
esp_err_t server_discovery_init(server_discovery_t *discovery, const char *fallback_ips[], int ip_count);

/**
 * Start mDNS discovery for HowdyTTS server
 */
esp_err_t server_discovery_start_mdns(server_discovery_t *discovery);

/**
 * Get next server to try (rotates through list)
 */
const char* server_discovery_get_next(server_discovery_t *discovery, uint16_t *port);

/**
 * Test connection to a specific server
 */
bool server_discovery_test_connection(const char *ip, uint16_t port);

/**
 * Get current active server
 */
const char* server_discovery_get_current(server_discovery_t *discovery, uint16_t *port);

/**
 * Clean up resources
 */
void server_discovery_deinit(server_discovery_t *discovery);

#endif // SERVER_DISCOVERY_H