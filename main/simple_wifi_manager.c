#include "simple_wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi_remote.h"  // Use WiFi remote instead of esp_wifi.h
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "simple_wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_wifi_initialized = false;
static wifi_connection_callback_t s_connection_callback = NULL;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi station started, connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/%d)", s_retry_num, WIFI_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi after %d attempts - STOPPING RETRIES", WIFI_MAXIMUM_RETRY);
            if (s_connection_callback) {
                s_connection_callback(false, "Max retries reached");
            }
            return; // Stop retrying to prevent infinite loop
        }
        if (s_connection_callback) {
            s_connection_callback(false, "Disconnected");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        if (s_connection_callback) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            s_connection_callback(true, ip_str);
        }
    }
}

esp_err_t simple_wifi_init(void)
{
    if (s_wifi_initialized) {
        ESP_LOGI(TAG, "WiFi already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing simple WiFi manager for ESP32-P4");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize network interface (event loop should already be created)
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();
    
    // Initialize ESP32-C6 WiFi remote via SDIO
    ESP_LOGI(TAG, "Initializing ESP32-C6 WiFi remote via ESP-HOSTED/SDIO interface");
    ESP_LOGI(TAG, "ESP32-P4 SDIO pins: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RST=%d",
             CONFIG_HOWDY_SDIO_CLK_GPIO, CONFIG_HOWDY_SDIO_CMD_GPIO,
             CONFIG_HOWDY_SDIO_D0_GPIO, CONFIG_HOWDY_SDIO_D1_GPIO,
             CONFIG_HOWDY_SDIO_D2_GPIO, CONFIG_HOWDY_SDIO_D3_GPIO,
             CONFIG_HOWDY_SLAVE_RESET_GPIO);
    
    // Initialize WiFi remote (ESP-HOSTED handles the SDIO communication automatically)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_remote_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP32-C6 WiFi remote: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ESP32-C6 WiFi remote initialized successfully via ESP-HOSTED");
    
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
    
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Simple WiFi manager initialized successfully");
    return ESP_OK;
}

esp_err_t simple_wifi_connect(const char *ssid, const char *password, wifi_connection_callback_t callback)
{
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid SSID or password");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    ESP_LOGI(TAG, "Password length: %d characters", strlen(password));
    
    s_connection_callback = callback;
    s_retry_num = 0;
    
    // Clear event bits before starting
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;  // Support both WPA and WPA2
    wifi_config.sta.pmf_cfg.capable = false;  // Disable PMF for compatibility
    wifi_config.sta.pmf_cfg.required = false;
    
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_LOGI(TAG, "Setting WiFi mode to STA");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    ESP_LOGI(TAG, "Setting WiFi configuration");
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    ESP_LOGI(TAG, "Starting WiFi");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi connection initiated - waiting for ESP32-C6 to connect to network");
    return ESP_OK;
}

esp_err_t simple_wifi_wait_connected(uint32_t timeout_ms)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connection successful");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi connection failed");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
}

bool simple_wifi_is_connected(void)
{
    if (!s_wifi_initialized || !s_wifi_event_group) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t simple_wifi_disconnect(void)
{
    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Disconnecting WiFi");
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_stop());
    
    return ESP_OK;
}

esp_err_t simple_wifi_get_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (!s_wifi_initialized || !ip_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!simple_wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return ESP_FAIL;
    }
    
    return esp_netif_get_ip_info(netif, ip_info);
}

int simple_wifi_get_rssi(void)
{
    if (!s_wifi_initialized || !simple_wifi_is_connected()) {
        return -100;
    }
    
    // Note: RSSI retrieval via esp_wifi_sta_get_ap_info() requires full ESP-HOSTED integration
    // For now, return a default value indicating good signal strength
    // TODO: Implement proper RSSI reading when ESP-HOSTED SDIO is fully configured
    ESP_LOGW(TAG, "RSSI reading not available - need ESP-HOSTED SDIO configuration");
    return -50; // Simulate moderate signal strength
}