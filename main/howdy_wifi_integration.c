#include "howdy_wifi_integration.h"
#include "wifi_provisioning.h"
#include "wifi_provisioning_ui.h"
#include "voice_assistant_ui.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "howdy_wifi";

// WiFi integration state
static struct {
    bool initialized;
    bool ui_active;
    wifi_integration_state_t state;
    wifi_integration_event_cb_t event_cb;
    void *user_data;
    
    // Synchronization
    SemaphoreHandle_t state_mutex;
    TaskHandle_t scan_task_handle;
    
    // Connection tracking
    char current_ssid[32];
    uint32_t connection_start_time;
    uint8_t connection_progress;
    
} s_wifi_integration = {
    .initialized = false,
    .ui_active = false,
    .state = WIFI_INTEGRATION_STATE_INIT,
    .connection_progress = 0,
};

// Forward declarations
static void wifi_prov_event_handler(wifi_prov_event_t event, void *data, void *user_data);
static void wifi_ui_event_handler(wifi_ui_event_data_t *event_data, void *user_data);
static void wifi_scan_task(void *pvParameters);
static void update_connection_progress(void);
static void notify_integration_event(wifi_integration_event_t event, void *data);
static esp_err_t transition_to_state(wifi_integration_state_t new_state);

esp_err_t howdy_wifi_integration_init(wifi_integration_event_cb_t event_cb, void *user_data)
{
    if (s_wifi_integration.initialized) {
        ESP_LOGW(TAG, "WiFi integration already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing HowdyScreen WiFi integration");
    
    s_wifi_integration.event_cb = event_cb;
    s_wifi_integration.user_data = user_data;
    
    // Create synchronization primitives
    s_wifi_integration.state_mutex = xSemaphoreCreateMutex();
    if (!s_wifi_integration.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize WiFi provisioning system
    wifi_prov_config_t prov_config;
    wifi_prov_get_default_config(&prov_config);
    
    esp_err_t ret = wifi_prov_init(&prov_config, wifi_prov_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi provisioning: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_wifi_integration.state_mutex);
        return ret;
    }
    
    // Initialize WiFi UI
    wifi_ui_config_t ui_config;
    wifi_ui_get_default_config(&ui_config);
    
    ret = wifi_ui_init(&ui_config, wifi_ui_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi UI: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_wifi_integration.state_mutex);
        return ret;
    }
    
    s_wifi_integration.state = WIFI_INTEGRATION_STATE_INIT;
    s_wifi_integration.initialized = true;
    
    ESP_LOGI(TAG, "WiFi integration initialized successfully");
    notify_integration_event(WIFI_INTEGRATION_EVENT_INIT_DONE, NULL);
    
    return ESP_OK;
}

esp_err_t howdy_wifi_integration_start(void)
{
    if (!s_wifi_integration.initialized) {
        ESP_LOGE(TAG, "WiFi integration not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting WiFi integration");
    
    // Start WiFi provisioning
    esp_err_t ret = wifi_prov_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi provisioning: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Check if we have stored credentials and try auto-connect
    wifi_credentials_t credentials;
    ret = wifi_prov_get_credentials(&credentials);
    
    if (ret == ESP_OK && credentials.valid) {
        ESP_LOGI(TAG, "Found stored credentials, attempting connection");
        transition_to_state(WIFI_INTEGRATION_STATE_CONNECTING);
    } else {
        ESP_LOGI(TAG, "No stored credentials, showing WiFi setup UI");
        transition_to_state(WIFI_INTEGRATION_STATE_SETUP_REQUIRED);
    }
    
    return ESP_OK;
}

esp_err_t howdy_wifi_integration_stop(void)
{
    if (!s_wifi_integration.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Stopping WiFi integration");
    
    // Stop scan task if running
    if (s_wifi_integration.scan_task_handle) {
        vTaskDelete(s_wifi_integration.scan_task_handle);
        s_wifi_integration.scan_task_handle = NULL;
    }
    
    // Stop WiFi provisioning
    wifi_prov_stop();
    
    // Hide UI if active
    if (s_wifi_integration.ui_active) {
        howdy_wifi_integration_hide_ui();
    }
    
    transition_to_state(WIFI_INTEGRATION_STATE_INIT);
    
    return ESP_OK;
}

esp_err_t howdy_wifi_integration_show_ui(void)
{
    if (!s_wifi_integration.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Showing WiFi configuration UI");
    
    // Hide voice assistant UI
    voice_assistant_ui_set_visibility(false);
    
    // Show WiFi UI
    wifi_ui_set_state(WIFI_UI_STATE_INIT);
    s_wifi_integration.ui_active = true;
    
    notify_integration_event(WIFI_INTEGRATION_EVENT_UI_SHOWN, NULL);
    
    return ESP_OK;
}

esp_err_t howdy_wifi_integration_hide_ui(void)
{
    if (!s_wifi_integration.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Hiding WiFi configuration UI");
    
    s_wifi_integration.ui_active = false;
    
    // Show voice assistant UI
    voice_assistant_ui_set_visibility(true);
    
    notify_integration_event(WIFI_INTEGRATION_EVENT_UI_HIDDEN, NULL);
    
    return ESP_OK;
}

wifi_integration_state_t howdy_wifi_integration_get_state(void)
{
    if (xSemaphoreTake(s_wifi_integration.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        wifi_integration_state_t state = s_wifi_integration.state;
        xSemaphoreGive(s_wifi_integration.state_mutex);
        return state;
    }
    return WIFI_INTEGRATION_STATE_ERROR;
}

bool howdy_wifi_integration_is_connected(void)
{
    return (howdy_wifi_integration_get_state() == WIFI_INTEGRATION_STATE_CONNECTED);
}

esp_err_t howdy_wifi_integration_reset(void)
{
    if (!s_wifi_integration.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Resetting WiFi integration");
    
    // Reset WiFi provisioning
    wifi_prov_reset();
    
    // Reset UI state
    wifi_ui_set_state(WIFI_UI_STATE_INIT);
    
    // Reset internal state
    if (xSemaphoreTake(s_wifi_integration.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_wifi_integration.state = WIFI_INTEGRATION_STATE_INIT;
        s_wifi_integration.connection_progress = 0;
        memset(s_wifi_integration.current_ssid, 0, sizeof(s_wifi_integration.current_ssid));
        xSemaphoreGive(s_wifi_integration.state_mutex);
    }
    
    notify_integration_event(WIFI_INTEGRATION_EVENT_RESET, NULL);
    
    return ESP_OK;
}

esp_err_t howdy_wifi_integration_get_connection_info(wifi_connection_info_t *info)
{
    if (!s_wifi_integration.initialized || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return wifi_prov_get_connection_info(info);
}

esp_err_t howdy_wifi_integration_deinit(void)
{
    if (!s_wifi_integration.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing WiFi integration");
    
    howdy_wifi_integration_stop();
    
    // Cleanup UI
    wifi_ui_deinit();
    
    // Cleanup synchronization
    if (s_wifi_integration.state_mutex) {
        vSemaphoreDelete(s_wifi_integration.state_mutex);
        s_wifi_integration.state_mutex = NULL;
    }
    
    memset(&s_wifi_integration, 0, sizeof(s_wifi_integration));
    s_wifi_integration.state = WIFI_INTEGRATION_STATE_INIT;
    
    ESP_LOGI(TAG, "WiFi integration deinitialized");
    
    return ESP_OK;
}

// Internal functions

static void wifi_prov_event_handler(wifi_prov_event_t event, void *data, void *user_data)
{
    ESP_LOGI(TAG, "WiFi provisioning event: %d", event);
    
    switch (event) {
        case WIFI_PROV_EVENT_INIT_DONE:
            notify_integration_event(WIFI_INTEGRATION_EVENT_PROV_INIT, NULL);
            break;
            
        case WIFI_PROV_EVENT_CONNECTING:
            {
                wifi_credentials_t *cred = (wifi_credentials_t*)data;
                if (cred) {
                    strncpy(s_wifi_integration.current_ssid, cred->ssid, 
                            sizeof(s_wifi_integration.current_ssid) - 1);
                    s_wifi_integration.connection_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                    s_wifi_integration.connection_progress = 10;
                    
                    wifi_ui_show_connection_progress(cred->ssid, 10);
                    transition_to_state(WIFI_INTEGRATION_STATE_CONNECTING);
                }
            }
            break;
            
        case WIFI_PROV_EVENT_CONNECTED:
            {
                wifi_connection_info_t *conn_info = (wifi_connection_info_t*)data;
                if (conn_info) {
                    s_wifi_integration.connection_progress = 100;
                    wifi_ui_show_connection_success(conn_info);
                    transition_to_state(WIFI_INTEGRATION_STATE_CONNECTED);
                    
                    // Auto-hide UI after successful connection
                    vTaskDelay(pdMS_TO_TICKS(3000)); // Show success for 3 seconds
                    if (s_wifi_integration.ui_active) {
                        howdy_wifi_integration_hide_ui();
                    }
                }
                notify_integration_event(WIFI_INTEGRATION_EVENT_CONNECTED, data);
            }
            break;
            
        case WIFI_PROV_EVENT_DISCONNECTED:
            transition_to_state(WIFI_INTEGRATION_STATE_DISCONNECTED);
            notify_integration_event(WIFI_INTEGRATION_EVENT_DISCONNECTED, NULL);
            break;
            
        case WIFI_PROV_EVENT_CRED_FAIL:
            wifi_ui_show_connection_error("Invalid credentials or connection failed");
            transition_to_state(WIFI_INTEGRATION_STATE_ERROR);
            notify_integration_event(WIFI_INTEGRATION_EVENT_CONNECTION_FAILED, NULL);
            break;
            
        case WIFI_PROV_EVENT_AP_MODE_START:
            {
                wifi_prov_config_t config;
                wifi_prov_get_default_config(&config);
                wifi_ui_show_ap_mode_info(config.ap_ssid, config.ap_password);
                transition_to_state(WIFI_INTEGRATION_STATE_AP_MODE);
                notify_integration_event(WIFI_INTEGRATION_EVENT_AP_MODE_STARTED, NULL);
            }
            break;
            
        case WIFI_PROV_EVENT_ERROR:
            transition_to_state(WIFI_INTEGRATION_STATE_ERROR);
            notify_integration_event(WIFI_INTEGRATION_EVENT_ERROR, NULL);
            break;
            
        default:
            break;
    }
}

static void wifi_ui_event_handler(wifi_ui_event_data_t *event_data, void *user_data)
{
    if (!event_data) return;
    
    ESP_LOGI(TAG, "WiFi UI event: %d", event_data->event);
    
    switch (event_data->event) {
        case WIFI_UI_EVENT_SCAN_REQUESTED:
            // Start network scan
            if (s_wifi_integration.scan_task_handle == NULL) {
                xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 5, &s_wifi_integration.scan_task_handle);
            }
            wifi_ui_set_state(WIFI_UI_STATE_SCANNING);
            break;
            
        case WIFI_UI_EVENT_NETWORK_SELECTED:
            {
                // Show manual entry with pre-filled SSID
                wifi_ui_show_manual_entry(event_data->data.network_selected.ssid);
                wifi_ui_set_state(WIFI_UI_STATE_MANUAL_ENTRY);
            }
            break;
            
        case WIFI_UI_EVENT_CREDENTIALS_ENTERED:
            {
                // Attempt connection with entered credentials
                const char *ssid = event_data->data.credentials_entered.ssid;
                const char *password = event_data->data.credentials_entered.password;
                
                esp_err_t ret = wifi_prov_set_credentials(ssid, password, true);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Connecting to network: %s", ssid);
                } else {
                    ESP_LOGE(TAG, "Failed to set credentials: %s", esp_err_to_name(ret));
                    wifi_ui_show_connection_error("Failed to set credentials");
                }
            }
            break;
            
        case WIFI_UI_EVENT_AP_MODE_REQUESTED:
            wifi_prov_start_ap_mode();
            break;
            
        case WIFI_UI_EVENT_BACK_PRESSED:
            wifi_ui_set_state(WIFI_UI_STATE_INIT);
            break;
            
        default:
            break;
    }
}

static void wifi_scan_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting WiFi network scan");
    
    wifi_ap_record_t ap_records[10];
    uint16_t num_aps = 0;
    
    esp_err_t ret = wifi_prov_scan_networks(ap_records, 10, &num_aps);
    if (ret == ESP_OK && num_aps > 0) {
        ESP_LOGI(TAG, "Found %d networks", num_aps);
        wifi_ui_update_network_list(ap_records, num_aps);
        wifi_ui_set_state(WIFI_UI_STATE_NETWORK_LIST);
    } else {
        ESP_LOGW(TAG, "No networks found or scan failed");
        wifi_ui_show_connection_error("No networks found");
        wifi_ui_set_state(WIFI_UI_STATE_ERROR);
    }
    
    s_wifi_integration.scan_task_handle = NULL;
    vTaskDelete(NULL);
}

static void update_connection_progress(void)
{
    if (s_wifi_integration.state == WIFI_INTEGRATION_STATE_CONNECTING) {
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) - s_wifi_integration.connection_start_time;
        
        // Simulate progress based on elapsed time (max 30 seconds)
        s_wifi_integration.connection_progress = (elapsed * 90) / 30000 + 10;
        if (s_wifi_integration.connection_progress > 90) {
            s_wifi_integration.connection_progress = 90;
        }
        
        wifi_ui_show_connection_progress(s_wifi_integration.current_ssid, 
                                       s_wifi_integration.connection_progress);
    }
}

static void notify_integration_event(wifi_integration_event_t event, void *data)
{
    if (s_wifi_integration.event_cb) {
        s_wifi_integration.event_cb(event, data, s_wifi_integration.user_data);
    }
}

static esp_err_t transition_to_state(wifi_integration_state_t new_state)
{
    if (xSemaphoreTake(s_wifi_integration.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ESP_LOGI(TAG, "State transition: %d -> %d", s_wifi_integration.state, new_state);
        s_wifi_integration.state = new_state;
        xSemaphoreGive(s_wifi_integration.state_mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}