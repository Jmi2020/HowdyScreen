#include "wifi_provisioning.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>

#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
#include "esp_wifi_remote.h"
#endif

static const char *TAG = "wifi_prov";

// NVS storage keys
#define NVS_NAMESPACE "wifi_prov"
#define NVS_SSID_KEY "ssid"
#define NVS_PASSWORD_KEY "password"
#define NVS_VALID_KEY "valid"

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_DISCONNECT_BIT BIT2

// Default configuration
static const wifi_prov_config_t default_config = {
    .ap_ssid = "HowdyScreen-Setup",
    .ap_password = "howdyscreen",
    .ap_channel = 1,
    .ap_max_connections = 4,
    .auto_connect = true,
    .connect_timeout_ms = 30000,
    .retry_attempts = 5,
    .retry_delay_ms = 5000,
};

// WiFi provisioning state
static struct {
    wifi_prov_config_t config;
    wifi_prov_state_t state;
    wifi_prov_event_cb_t event_cb;
    void *user_data;
    
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    EventGroupHandle_t wifi_event_group;
    SemaphoreHandle_t state_mutex;
    
    bool initialized;
    uint32_t retry_count;
    uint32_t start_time;
    wifi_connection_info_t connection_info;
    
} s_wifi_prov = {
    .state = WIFI_PROV_STATE_INIT,
    .initialized = false,
    .retry_count = 0,
    .wifi_event_group = NULL,
    .state_mutex = NULL,
};

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t load_credentials_from_nvs(wifi_credentials_t *credentials);
static esp_err_t save_credentials_to_nvs(const wifi_credentials_t *credentials);
static void notify_event(wifi_prov_event_t event, void *data);
static esp_err_t start_sta_mode(const wifi_credentials_t *credentials);
static esp_err_t configure_ap_mode(void);

void wifi_prov_get_default_config(wifi_prov_config_t *config)
{
    if (!config) return;
    *config = default_config;
    
    // Generate unique AP SSID based on MAC address
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(config->ap_ssid, sizeof(config->ap_ssid), "HowdyScreen-%02X%02X", mac[4], mac[5]);
    }
}

esp_err_t wifi_prov_init(const wifi_prov_config_t *config, wifi_prov_event_cb_t event_cb, void *user_data)
{
    if (s_wifi_prov.initialized) {
        ESP_LOGW(TAG, "WiFi provisioning already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi provisioning system");
    
    // Use provided config or defaults
    if (config) {
        s_wifi_prov.config = *config;
    } else {
        wifi_prov_get_default_config(&s_wifi_prov.config);
    }
    
    s_wifi_prov.event_cb = event_cb;
    s_wifi_prov.user_data = user_data;
    
    // Create synchronization primitives
    s_wifi_prov.wifi_event_group = xEventGroupCreate();
    if (!s_wifi_prov.wifi_event_group) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }
    
    s_wifi_prov.state_mutex = xSemaphoreCreateMutex();
    if (!s_wifi_prov.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        vEventGroupDelete(s_wifi_prov.wifi_event_group);
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces
    s_wifi_prov.sta_netif = esp_netif_create_default_wifi_sta();
    s_wifi_prov.ap_netif = esp_netif_create_default_wifi_ap();
    
    if (!s_wifi_prov.sta_netif || !s_wifi_prov.ap_netif) {
        ESP_LOGE(TAG, "Failed to create network interfaces");
        return ESP_FAIL;
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Set WiFi mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    s_wifi_prov.state = WIFI_PROV_STATE_INIT;
    s_wifi_prov.initialized = true;
    s_wifi_prov.retry_count = 0;
    
    ESP_LOGI(TAG, "WiFi provisioning initialized successfully");
    ESP_LOGI(TAG, "Config: AP SSID='%s', auto_connect=%s, timeout=%lums", 
             s_wifi_prov.config.ap_ssid, 
             s_wifi_prov.config.auto_connect ? "true" : "false",
             s_wifi_prov.config.connect_timeout_ms);
    
    notify_event(WIFI_PROV_EVENT_INIT_DONE, NULL);
    return ESP_OK;
}

esp_err_t wifi_prov_start(void)
{
    if (!s_wifi_prov.initialized) {
        ESP_LOGE(TAG, "WiFi provisioning not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Starting WiFi provisioning");
    s_wifi_prov.state = WIFI_PROV_STATE_STARTING;
    s_wifi_prov.start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    xSemaphoreGive(s_wifi_prov.state_mutex);
    
    // Try to load stored credentials
    wifi_credentials_t credentials;
    esp_err_t ret = load_credentials_from_nvs(&credentials);
    
    if (ret == ESP_OK && credentials.valid && s_wifi_prov.config.auto_connect) {
        ESP_LOGI(TAG, "Found stored credentials for SSID: %s", credentials.ssid);
        return start_sta_mode(&credentials);
    } else {
        ESP_LOGI(TAG, "No valid stored credentials found, starting AP mode");
        return wifi_prov_start_ap_mode();
    }
}

esp_err_t wifi_prov_stop(void)
{
    if (!s_wifi_prov.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi provisioning");
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_prov.state = WIFI_PROV_STATE_INIT;
        xSemaphoreGive(s_wifi_prov.state_mutex);
    }
    
    esp_wifi_stop();
    return ESP_OK;
}

esp_err_t wifi_prov_set_credentials(const char *ssid, const char *password, bool save_to_nvs)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(ssid) >= sizeof(((wifi_credentials_t*)0)->ssid) ||
        strlen(password) >= sizeof(((wifi_credentials_t*)0)->password)) {
        ESP_LOGE(TAG, "Credentials too long");
        return ESP_ERR_INVALID_ARG;
    }
    
    wifi_credentials_t credentials;
    strncpy(credentials.ssid, ssid, sizeof(credentials.ssid) - 1);
    credentials.ssid[sizeof(credentials.ssid) - 1] = '\0';
    strncpy(credentials.password, password, sizeof(credentials.password) - 1);
    credentials.password[sizeof(credentials.password) - 1] = '\0';
    credentials.valid = true;
    
    if (save_to_nvs) {
        esp_err_t ret = save_credentials_to_nvs(&credentials);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save credentials to NVS: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Credentials saved to NVS for SSID: %s", ssid);
    }
    
    notify_event(WIFI_PROV_EVENT_CRED_RECV, &credentials);
    
    // Attempt connection with new credentials
    return start_sta_mode(&credentials);
}

esp_err_t wifi_prov_get_credentials(wifi_credentials_t *credentials)
{
    if (!credentials) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return load_credentials_from_nvs(credentials);
}

esp_err_t wifi_prov_clear_credentials(void)
{
    ESP_LOGI(TAG, "Clearing stored WiFi credentials");
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    nvs_erase_key(nvs_handle, NVS_SSID_KEY);
    nvs_erase_key(nvs_handle, NVS_PASSWORD_KEY);
    nvs_erase_key(nvs_handle, NVS_VALID_KEY);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    return ESP_OK;
}

wifi_prov_state_t wifi_prov_get_state(void)
{
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        wifi_prov_state_t state = s_wifi_prov.state;
        xSemaphoreGive(s_wifi_prov.state_mutex);
        return state;
    }
    return WIFI_PROV_STATE_ERROR;
}

bool wifi_prov_is_connected(void)
{
    return wifi_prov_get_state() == WIFI_PROV_STATE_CONNECTED;
}

esp_err_t wifi_prov_get_connection_info(wifi_connection_info_t *info)
{
    if (!info || !wifi_prov_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    *info = s_wifi_prov.connection_info;
    return ESP_OK;
}

esp_err_t wifi_prov_start_ap_mode(void)
{
    if (!s_wifi_prov.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting AP mode for provisioning");
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_prov.state = WIFI_PROV_STATE_AP_MODE;
        xSemaphoreGive(s_wifi_prov.state_mutex);
    }
    
    esp_err_t ret = configure_ap_mode();
    if (ret == ESP_OK) {
        notify_event(WIFI_PROV_EVENT_AP_MODE_START, NULL);
    }
    
    return ret;
}

esp_err_t wifi_prov_stop_ap_mode(void)
{
    ESP_LOGI(TAG, "Stopping AP mode");
    
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_prov.state = WIFI_PROV_STATE_DISCONNECTED;
        xSemaphoreGive(s_wifi_prov.state_mutex);
    }
    
    notify_event(WIFI_PROV_EVENT_AP_MODE_STOP, NULL);
    return ESP_OK;
}

esp_err_t wifi_prov_scan_networks(wifi_ap_record_t *ap_records, uint16_t max_aps, uint16_t *num_aps)
{
    if (!ap_records || !num_aps || max_aps == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Scanning for available networks");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_wifi_scan_get_ap_records(&max_aps, ap_records);
    *num_aps = max_aps;
    
    ESP_LOGI(TAG, "Found %d networks", *num_aps);
    return ret;
}

esp_err_t wifi_prov_reset(void)
{
    ESP_LOGI(TAG, "Resetting WiFi provisioning system");
    
    wifi_prov_stop();
    wifi_prov_clear_credentials();
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_prov.state = WIFI_PROV_STATE_INIT;
        s_wifi_prov.retry_count = 0;
        memset(&s_wifi_prov.connection_info, 0, sizeof(s_wifi_prov.connection_info));
        xSemaphoreGive(s_wifi_prov.state_mutex);
    }
    
    return ESP_OK;
}

// Internal functions

static esp_err_t load_credentials_from_nvs(wifi_credentials_t *credentials)
{
    if (!credentials) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    size_t required_size = sizeof(credentials->ssid);
    ret = nvs_get_str(nvs_handle, NVS_SSID_KEY, credentials->ssid, &required_size);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    required_size = sizeof(credentials->password);
    ret = nvs_get_str(nvs_handle, NVS_PASSWORD_KEY, credentials->password, &required_size);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    uint8_t valid = 0;
    ret = nvs_get_u8(nvs_handle, NVS_VALID_KEY, &valid);
    credentials->valid = (ret == ESP_OK && valid != 0);
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t save_credentials_to_nvs(const wifi_credentials_t *credentials)
{
    if (!credentials) return ESP_ERR_INVALID_ARG;
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = nvs_set_str(nvs_handle, NVS_SSID_KEY, credentials->ssid);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_set_str(nvs_handle, NVS_PASSWORD_KEY, credentials->password);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_set_u8(nvs_handle, NVS_VALID_KEY, credentials->valid ? 1 : 0);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return ret;
}

static void notify_event(wifi_prov_event_t event, void *data)
{
    if (s_wifi_prov.event_cb) {
        s_wifi_prov.event_cb(event, data, s_wifi_prov.user_data);
    }
}

static esp_err_t start_sta_mode(const wifi_credentials_t *credentials)
{
    if (!credentials || !credentials->valid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", credentials->ssid);
    
    if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_prov.state = WIFI_PROV_STATE_CONNECTING;
        s_wifi_prov.retry_count = 0;
        xSemaphoreGive(s_wifi_prov.state_mutex);
    }
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, credentials->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, credentials->password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    notify_event(WIFI_PROV_EVENT_CONNECTING, (void*)credentials);
    return ESP_OK;
}

static esp_err_t configure_ap_mode(void)
{
    wifi_config_t wifi_config = {
        .ap = {
            .channel = s_wifi_prov.config.ap_channel,
            .max_connection = s_wifi_prov.config.ap_max_connections,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    strncpy((char*)wifi_config.ap.ssid, s_wifi_prov.config.ap_ssid, sizeof(wifi_config.ap.ssid) - 1);
    wifi_config.ap.ssid_len = strlen(s_wifi_prov.config.ap_ssid);
    strncpy((char*)wifi_config.ap.password, s_wifi_prov.config.ap_password, sizeof(wifi_config.ap.password) - 1);
    
    if (strlen(s_wifi_prov.config.ap_password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "AP mode started - SSID: %s, Password: %s", 
             s_wifi_prov.config.ap_ssid, s_wifi_prov.config.ap_password);
    
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_wifi_prov.retry_count < s_wifi_prov.config.retry_attempts) {
                esp_wifi_connect();
                s_wifi_prov.retry_count++;
                ESP_LOGI(TAG, "Retry connecting to WiFi (%lu/%lu)", 
                         s_wifi_prov.retry_count, s_wifi_prov.config.retry_attempts);
            } else {
                ESP_LOGI(TAG, "Failed to connect to WiFi after %lu attempts", s_wifi_prov.retry_count);
                s_wifi_prov.state = WIFI_PROV_STATE_ERROR;
                xEventGroupSetBits(s_wifi_prov.wifi_event_group, WIFI_FAIL_BIT);
                notify_event(WIFI_PROV_EVENT_CRED_FAIL, NULL);
            }
            xSemaphoreGive(s_wifi_prov.state_mutex);
        }
        
        // Clear connection info
        memset(&s_wifi_prov.connection_info, 0, sizeof(s_wifi_prov.connection_info));
        notify_event(WIFI_PROV_EVENT_DISCONNECTED, NULL);
        
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // Update connection info
        snprintf(s_wifi_prov.connection_info.ip_address, sizeof(s_wifi_prov.connection_info.ip_address),
                 IPSTR, IP2STR(&event->ip_info.ip));
        snprintf(s_wifi_prov.connection_info.gateway, sizeof(s_wifi_prov.connection_info.gateway),
                 IPSTR, IP2STR(&event->ip_info.gw));
        snprintf(s_wifi_prov.connection_info.netmask, sizeof(s_wifi_prov.connection_info.netmask),
                 IPSTR, IP2STR(&event->ip_info.netmask));
        
        // Get WiFi info
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_wifi_prov.connection_info.rssi = ap_info.rssi;
            s_wifi_prov.connection_info.channel = ap_info.primary;
            strncpy(s_wifi_prov.connection_info.connected_ssid, (char*)ap_info.ssid, 
                    sizeof(s_wifi_prov.connection_info.connected_ssid) - 1);
        }
        
        s_wifi_prov.connection_info.connection_time = 
            (xTaskGetTickCount() * portTICK_PERIOD_MS - s_wifi_prov.start_time) / 1000;
        
        if (xSemaphoreTake(s_wifi_prov.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_wifi_prov.state = WIFI_PROV_STATE_CONNECTED;
            s_wifi_prov.retry_count = 0;
            xSemaphoreGive(s_wifi_prov.state_mutex);
        }
        
        xEventGroupSetBits(s_wifi_prov.wifi_event_group, WIFI_CONNECTED_BIT);
        
        ESP_LOGI(TAG, "Connected to WiFi successfully!");
        ESP_LOGI(TAG, "IP: %s, Gateway: %s, RSSI: %d dBm", 
                 s_wifi_prov.connection_info.ip_address,
                 s_wifi_prov.connection_info.gateway,
                 s_wifi_prov.connection_info.rssi);
        
        notify_event(WIFI_PROV_EVENT_CONNECTED, &s_wifi_prov.connection_info);
        notify_event(WIFI_PROV_EVENT_CRED_SUCCESS, NULL);
    }
}