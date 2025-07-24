#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include "esp_err.h"
#include <stdbool.h>

#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_MAC_LEN 18
#define PROVISION_AP_SSID "HowdyScreen-Setup"
#define PROVISION_AP_PASSWORD "configure"

typedef struct {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSWORD_LEN];
    char target_mac[MAX_MAC_LEN];  // MAC address of target device (Mac Studio)
    bool configured;
    bool ap_mode_active;
} wifi_provision_config_t;

typedef enum {
    PROVISION_STATE_NOT_CONFIGURED,
    PROVISION_STATE_AP_MODE,
    PROVISION_STATE_CONNECTING,
    PROVISION_STATE_CONNECTED,
    PROVISION_STATE_FAILED
} provision_state_t;

/**
 * Initialize WiFi provisioning system
 */
esp_err_t wifi_provisioning_init(wifi_provision_config_t *config);

/**
 * Load WiFi configuration from NVS
 */
esp_err_t wifi_provisioning_load_config(wifi_provision_config_t *config);

/**
 * Save WiFi configuration to NVS
 */
esp_err_t wifi_provisioning_save_config(const wifi_provision_config_t *config);

/**
 * Start provisioning AP mode
 */
esp_err_t wifi_provisioning_start_ap(wifi_provision_config_t *config);

/**
 * Stop provisioning AP mode
 */
esp_err_t wifi_provisioning_stop_ap(wifi_provision_config_t *config);

/**
 * Connect to configured WiFi network
 */
esp_err_t wifi_provisioning_connect(wifi_provision_config_t *config);

/**
 * Check if target device (Mac) is on network
 */
bool wifi_provisioning_check_target_device(const char *target_mac);

/**
 * Get current provisioning state
 */
provision_state_t wifi_provisioning_get_state(const wifi_provision_config_t *config);

/**
 * Reset configuration (clear stored WiFi credentials)
 */
esp_err_t wifi_provisioning_reset(wifi_provision_config_t *config);

/**
 * Handle HTTP requests for configuration
 */
void wifi_provisioning_handle_request(const char *uri, const char *body, char *response, size_t max_len);

/**
 * Clean up provisioning resources
 */
esp_err_t wifi_provisioning_deinit(wifi_provision_config_t *config);

#endif // WIFI_PROVISIONING_H