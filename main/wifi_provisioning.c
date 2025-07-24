#include "wifi_provisioning.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>
#include <cJSON.h>

#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
#include "esp_wifi_remote.h"
#endif

static const char *TAG = "wifi_provisioning";
static httpd_handle_t provisioning_server = NULL;
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    wifi_provision_config_t *config = (wifi_provision_config_t*)arg;
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi connection failed");
        xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_provisioning_init(wifi_provision_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(wifi_provision_config_t));
    
    wifi_event_group = xEventGroupCreate();
    
    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
    // Initialize WiFi remote for ESP32-C6 co-processor
    wifi_remote_config_t remote_config = WIFI_REMOTE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_wifi_remote_init(&remote_config));
    ESP_LOGI(TAG, "ESP WiFi Remote initialized for ESP32-C6 co-processor");
#endif
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        config,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        config,
                                                        NULL));
    
    // Try to load existing configuration
    wifi_provisioning_load_config(config);
    
    ESP_LOGI(TAG, "WiFi provisioning initialized");
    return ESP_OK;
}

esp_err_t wifi_provisioning_load_config(wifi_provision_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved WiFi configuration found");
        return err;
    }
    
    size_t required_size;
    
    // Load SSID
    required_size = MAX_SSID_LEN;
    err = nvs_get_str(nvs_handle, "ssid", config->ssid, &required_size);
    if (err != ESP_OK) goto cleanup;
    
    // Load password
    required_size = MAX_PASSWORD_LEN;
    err = nvs_get_str(nvs_handle, "password", config->password, &required_size);
    if (err != ESP_OK) goto cleanup;
    
    // Load target MAC (optional)
    required_size = MAX_MAC_LEN;
    nvs_get_str(nvs_handle, "target_mac", config->target_mac, &required_size);
    
    config->configured = true;
    ESP_LOGI(TAG, "Loaded WiFi config: SSID=%s", config->ssid);
    
cleanup:
    nvs_close(nvs_handle);
    return err;
}

esp_err_t wifi_provisioning_save_config(const wifi_provision_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_set_str(nvs_handle, "ssid", config->ssid);
    if (err != ESP_OK) goto cleanup;
    
    err = nvs_set_str(nvs_handle, "password", config->password);
    if (err != ESP_OK) goto cleanup;
    
    if (strlen(config->target_mac) > 0) {
        err = nvs_set_str(nvs_handle, "target_mac", config->target_mac);
    }
    
    err = nvs_commit(nvs_handle);
    ESP_LOGI(TAG, "WiFi configuration saved");
    
cleanup:
    nvs_close(nvs_handle);
    return err;
}

// HTTP handlers for provisioning web interface
static esp_err_t root_handler(httpd_req_t *req)
{
    const char* html_page = 
        "<!DOCTYPE html>"
        "<html><head>"
        "<title>HowdyScreen Setup</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;margin:40px;background:#f0f0f0}"
        ".container{max-width:400px;margin:auto;background:white;padding:30px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.1)}"
        "h1{color:#333;text-align:center;margin-bottom:30px}"
        "input,select{width:100%;padding:12px;margin:10px 0;border:1px solid #ddd;border-radius:5px;box-sizing:border-box}"
        "button{width:100%;background:#4CAF50;color:white;padding:14px;margin:20px 0;border:none;border-radius:5px;cursor:pointer;font-size:16px}"
        "button:hover{background:#45a049}"
        ".status{padding:10px;margin:10px 0;border-radius:5px;text-align:center}"
        ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}"
        ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>🤠 HowdyScreen Setup</h1>"
        "<form id='wifiForm'>"
        "<label>WiFi Network:</label>"
        "<select id='ssid' name='ssid' required>"
        "<option value=''>Scanning networks...</option>"
        "</select>"
        "<label>Password:</label>"
        "<input type='password' id='password' name='password' placeholder='WiFi Password'>"
        "<label>Mac Studio MAC Address (Optional):</label>"
        "<input type='text' id='target_mac' name='target_mac' placeholder='aa:bb:cc:dd:ee:ff'>"
        "<button type='submit'>Connect</button>"
        "</form>"
        "<div id='status'></div>"
        "</div>"
        "<script>"
        "document.getElementById('wifiForm').addEventListener('submit', function(e) {"
        "  e.preventDefault();"
        "  var formData = new FormData(e.target);"
        "  var data = Object.fromEntries(formData);"
        "  fetch('/configure', {"
        "    method: 'POST',"
        "    headers: {'Content-Type': 'application/json'},"
        "    body: JSON.stringify(data)"
        "  })"
        "  .then(response => response.json())"
        "  .then(data => {"
        "    var status = document.getElementById('status');"
        "    if(data.success) {"
        "      status.innerHTML = '<div class=\"status success\">Configuration saved! Connecting to WiFi...</div>';"
        "      setTimeout(() => { status.innerHTML += '<div class=\"status success\">Setup complete! You can close this page.</div>'; }, 3000);"
        "    } else {"
        "      status.innerHTML = '<div class=\"status error\">Error: ' + data.message + '</div>';"
        "    }"
        "  });"
        "});"
        "fetch('/networks').then(r=>r.json()).then(networks=>{"
        "  var select = document.getElementById('ssid');"
        "  select.innerHTML = '';"
        "  networks.forEach(n => {"
        "    var option = document.createElement('option');"
        "    option.value = n.ssid;"
        "    option.textContent = n.ssid + ' (' + n.rssi + ' dBm)';"
        "    select.appendChild(option);"
        "  });"
        "});"
        "</script></body></html>";
    
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t configure_handler(httpd_req_t *req)
{
    char content[512];
    size_t recv_size = MIN(req->content_len, sizeof(content) - 1);
    
    if (httpd_req_recv(req, content, recv_size) <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    content[recv_size] = '\0';
    
    // Parse JSON
    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }
    
    cJSON *ssid_json = cJSON_GetObjectItem(json, "ssid");
    cJSON *password_json = cJSON_GetObjectItem(json, "password");
    cJSON *mac_json = cJSON_GetObjectItem(json, "target_mac");
    
    if (!ssid_json || !cJSON_IsString(ssid_json)) {
        cJSON_Delete(json);
        httpd_resp_send_400(req);
        return ESP_FAIL;
    }
    
    // Get config from request user context (set during server start)
    wifi_provision_config_t *config = (wifi_provision_config_t*)req->user_ctx;
    
    strncpy(config->ssid, cJSON_GetStringValue(ssid_json), MAX_SSID_LEN - 1);
    if (password_json && cJSON_IsString(password_json)) {
        strncpy(config->password, cJSON_GetStringValue(password_json), MAX_PASSWORD_LEN - 1);
    }
    if (mac_json && cJSON_IsString(mac_json)) {
        strncpy(config->target_mac, cJSON_GetStringValue(mac_json), MAX_MAC_LEN - 1);
    }
    
    config->configured = true;
    
    // Save configuration
    esp_err_t err = wifi_provisioning_save_config(config);
    
    const char* response = (err == ESP_OK) ? 
        "{\"success\":true,\"message\":\"Configuration saved\"}" :
        "{\"success\":false,\"message\":\"Failed to save configuration\"}";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t networks_handler(httpd_req_t *req)
{
    // Scan for WiFi networks
    wifi_scan_config_t scan_config = {0};
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));
    
    // Build JSON response
    cJSON *networks = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char*)ap_list[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_list[i].rssi);
        cJSON_AddItemToArray(networks, network);
    }
    
    char *json_string = cJSON_Print(networks);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);
    
    free(json_string);
    cJSON_Delete(networks);
    free(ap_list);
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_start_ap(wifi_provision_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Create AP netif
    esp_netif_create_default_wifi_ap();
    
    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = PROVISION_AP_SSID,
            .ssid_len = strlen(PROVISION_AP_SSID),
            .channel = 1,
            .password = PROVISION_AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    config->ap_mode_active = true;
    
    // Start HTTP server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = 80;
    
    if (httpd_start(&provisioning_server, &server_config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = config
        };
        httpd_register_uri_handler(provisioning_server, &root_uri);
        
        httpd_uri_t configure_uri = {
            .uri = "/configure",
            .method = HTTP_POST,
            .handler = configure_handler,
            .user_ctx = config
        };
        httpd_register_uri_handler(provisioning_server, &configure_uri);
        
        httpd_uri_t networks_uri = {
            .uri = "/networks",
            .method = HTTP_GET,
            .handler = networks_handler,
            .user_ctx = config
        };
        httpd_register_uri_handler(provisioning_server, &networks_uri);
    }
    
    ESP_LOGI(TAG, "AP mode started: SSID=%s, Password=%s", PROVISION_AP_SSID, PROVISION_AP_PASSWORD);
    ESP_LOGI(TAG, "Connect to WiFi and go to http://192.168.4.1 to configure");
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_stop_ap(wifi_provision_config_t *config)
{
    if (provisioning_server) {
        httpd_stop(provisioning_server);
        provisioning_server = NULL;
    }
    
    if (config) {
        config->ap_mode_active = false;
    }
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_connect(wifi_provision_config_t *config)
{
    if (!config || !config->configured) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop AP mode
    wifi_provisioning_stop_ap(config);
    
    // Create STA netif
    esp_netif_create_default_wifi_sta();
    
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", config->ssid);
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          pdMS_TO_TICKS(15000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
        return ESP_FAIL;
    }
}

bool wifi_provisioning_check_target_device(const char *target_mac)
{
    // This is a simplified check - in practice you might want to
    // ping the device or check ARP table
    if (!target_mac || strlen(target_mac) == 0) {
        return true;  // No specific target device required
    }
    
    // TODO: Implement MAC address verification
    // For now, just return true if we have any WiFi connection
    wifi_ap_record_t ap_info;
    return (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
}

provision_state_t wifi_provisioning_get_state(const wifi_provision_config_t *config)
{
    if (!config) {
        return PROVISION_STATE_NOT_CONFIGURED;
    }
    
    if (!config->configured) {
        return config->ap_mode_active ? PROVISION_STATE_AP_MODE : PROVISION_STATE_NOT_CONFIGURED;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return PROVISION_STATE_CONNECTED;
    }
    
    return PROVISION_STATE_CONNECTING;
}

esp_err_t wifi_provisioning_reset(wifi_provision_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Clear NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    // Clear config
    memset(config, 0, sizeof(wifi_provision_config_t));
    
    ESP_LOGI(TAG, "WiFi configuration reset");
    return ESP_OK;
}

esp_err_t wifi_provisioning_deinit(wifi_provision_config_t *config)
{
    wifi_provisioning_stop_ap(config);
    
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
    }
    
    return ESP_OK;
}