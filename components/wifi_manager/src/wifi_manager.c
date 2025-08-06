#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "WiFiManager";

// Event group for WiFi connection
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// WiFi manager state
static struct {
    wifi_state_t state;
    wifi_event_callback_t event_callback;
    int retry_count;
    int max_retry;
    bool initialized;
    esp_netif_t *netif;
} s_wifi_manager = {
    .state = WIFI_STATE_DISCONNECTED,
    .event_callback = NULL,
    .retry_count = 0,
    .max_retry = 5,
    .initialized = false,
    .netif = NULL
};

// Event handler for WiFi and IP events
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi started, connecting...");
                esp_wifi_connect();
                s_wifi_manager.state = WIFI_STATE_CONNECTING;
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected from AP");
                s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
                
                if (s_wifi_manager.retry_count < s_wifi_manager.max_retry) {
                    esp_wifi_connect();
                    s_wifi_manager.retry_count++;
                    ESP_LOGI(TAG, "Retrying connection... (%d/%d)", 
                            s_wifi_manager.retry_count, s_wifi_manager.max_retry);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    s_wifi_manager.state = WIFI_STATE_ERROR;
                    ESP_LOGE(TAG, "Failed to connect after %d attempts", s_wifi_manager.max_retry);
                }
                
                if (s_wifi_manager.event_callback) {
                    s_wifi_manager.event_callback(WIFI_EVENT_DISCONNECTED_ID, NULL);
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                s_wifi_manager.retry_count = 0;
                if (s_wifi_manager.event_callback) {
                    s_wifi_manager.event_callback(WIFI_EVENT_CONNECTED_ID, NULL);
                }
                break;
                
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi scan completed");
                if (s_wifi_manager.event_callback) {
                    s_wifi_manager.event_callback(WIFI_EVENT_SCAN_DONE_ID, event_data);
                }
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_manager.state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        if (s_wifi_manager.event_callback) {
            s_wifi_manager.event_callback(WIFI_EVENT_GOT_IP_ID, &event->ip_info);
        }
    }
}

esp_err_t wifi_manager_init(wifi_event_callback_t event_callback)
{
    if (s_wifi_manager.initialized) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    
    // Initialize NVS (might already be initialized by the system)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // NVS might already be initialized, which is OK
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_INITIALIZED) {
        ESP_LOGW(TAG, "NVS init returned: %s", esp_err_to_name(ret));
    }
    
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Event loop should already be created by system_init in main
    // Just verify it exists, don't create a new one
    
    // Create default WiFi station
    s_wifi_manager.netif = esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Store callback
    s_wifi_manager.event_callback = event_callback;
    s_wifi_manager.initialized = true;
    
    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_wifi_manager.initialized) {
        ESP_LOGE(TAG, "WiFi manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!ssid) {
        ESP_LOGE(TAG, "SSID cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    
    // Set configuration and start WiFi
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected event");
        return ESP_FAIL;
    }
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_wifi_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting from WiFi...");
    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
    }
    return ret;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_wifi_manager.state;
}

esp_err_t wifi_manager_get_rssi(int8_t *rssi)
{
    if (!s_wifi_manager.initialized || !rssi) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_wifi_manager.state != WIFI_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        *rssi = ap_info.rssi;
    }
    return ret;
}

int wifi_manager_get_signal_strength(void)
{
    int8_t rssi;
    if (wifi_manager_get_rssi(&rssi) != ESP_OK) {
        return 0;
    }
    
    // Convert RSSI to percentage (approximate)
    // -30 dBm = 100%, -90 dBm = 0%
    int strength = 0;
    if (rssi >= -30) {
        strength = 100;
    } else if (rssi <= -90) {
        strength = 0;
    } else {
        strength = (int)(100.0 * (90.0 + rssi) / 60.0);
    }
    
    return strength;
}

esp_err_t wifi_manager_get_ip_str(char *ip_str)
{
    if (!s_wifi_manager.initialized || !ip_str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_wifi_manager.state != WIFI_STATE_CONNECTED) {
        strcpy(ip_str, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_wifi_manager.netif, &ip_info);
    if (ret == ESP_OK) {
        sprintf(ip_str, IPSTR, IP2STR(&ip_info.ip));
    }
    return ret;
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_manager.state == WIFI_STATE_CONNECTED;
}

esp_err_t wifi_manager_start_scan(void)
{
    if (!s_wifi_manager.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };
    
    return esp_wifi_scan_start(&scan_config, false);
}

esp_err_t wifi_manager_auto_connect(void)
{
    // Use configured WiFi credentials from Kconfig
    const char *default_ssid = CONFIG_HOWDY_WIFI_SSID;
    const char *default_password = CONFIG_HOWDY_WIFI_PASSWORD;
    
    ESP_LOGI(TAG, "Auto-connecting to saved WiFi...");
    ESP_LOGI(TAG, "Connecting to SSID: %s", default_ssid);
    return wifi_manager_connect(default_ssid, default_password);
}