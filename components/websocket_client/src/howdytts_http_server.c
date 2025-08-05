#include "howdytts_http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "mdns.h"
#include <string.h>

static const char *TAG = "HowdyTTSHTTP";

// HTTP server state
static struct {
    httpd_handle_t server;
    howdytts_http_config_t config;
    howdytts_state_callback_t state_callback;
    howdytts_discovery_callback_t discovery_callback;
    
    // Device status
    char device_status[64];
    float audio_level;
    int battery_level;
    int signal_strength;
    
    // Statistics
    uint32_t state_requests;
    uint32_t speak_requests;
    uint32_t discovery_requests;
    uint32_t status_requests;
    
    bool is_initialized;
    bool is_running;
} s_http_server = {
    .server = NULL,
    .state_callback = NULL,
    .discovery_callback = NULL,
    .audio_level = 0.0f,
    .battery_level = -1,
    .signal_strength = -1,
    .is_initialized = false,
    .is_running = false
};

// Forward declarations
static esp_err_t state_handler(httpd_req_t *req);
static esp_err_t speak_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t discover_handler(httpd_req_t *req);
static esp_err_t send_json_response(httpd_req_t *req, int status_code, const char *message);
static esp_err_t parse_post_data(httpd_req_t *req, char *buffer, size_t buffer_size);

esp_err_t howdytts_http_server_init(const howdytts_http_config_t *config,
                                   howdytts_state_callback_t state_callback,
                                   howdytts_discovery_callback_t discovery_callback)
{
    if (!config || !state_callback) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_http_server.is_initialized) {
        ESP_LOGI(TAG, "HowdyTTS HTTP server already initialized");
        return ESP_OK;
    }
    
    // Copy configuration
    s_http_server.config = *config;
    s_http_server.state_callback = state_callback;
    s_http_server.discovery_callback = discovery_callback;
    
    // Initialize device status
    snprintf(s_http_server.device_status, sizeof(s_http_server.device_status), "ready");
    
    s_http_server.is_initialized = true;
    
    ESP_LOGI(TAG, "HowdyTTS HTTP server initialized on port %d", config->port);
    ESP_LOGI(TAG, "Device ID: %s, Room: %s", config->device_id, config->room);
    
    return ESP_OK;
}

esp_err_t howdytts_http_server_start(void)
{
    if (!s_http_server.is_initialized) {
        ESP_LOGE(TAG, "HTTP server not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_http_server.is_running) {
        ESP_LOGI(TAG, "HTTP server already running");
        return ESP_OK;
    }
    
    // Configure HTTP server
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = s_http_server.config.port;
    server_config.max_open_sockets = s_http_server.config.max_open_sockets;
    server_config.lru_purge_enable = s_http_server.config.lru_purge_enable;
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&s_http_server.server, &server_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register URI handlers for HowdyTTS integration
    
    // POST /state - Receive state changes from HowdyTTS
    httpd_uri_t state_uri = {
        .uri = "/state",
        .method = HTTP_POST,
        .handler = state_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_http_server.server, &state_uri);
    
    // POST /speak - Receive speaking state with text content
    httpd_uri_t speak_uri = {
        .uri = "/speak",
        .method = HTTP_POST,
        .handler = speak_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_http_server.server, &speak_uri);
    
    // GET /status - Device status for discovery
    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_http_server.server, &status_uri);
    
    // POST /discover - Device discovery response
    httpd_uri_t discover_uri = {
        .uri = "/discover",
        .method = HTTP_POST,
        .handler = discover_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(s_http_server.server, &discover_uri);
    
    s_http_server.is_running = true;
    
    ESP_LOGI(TAG, "HowdyTTS HTTP server started successfully");
    ESP_LOGI(TAG, "Endpoints: /state, /speak, /status, /discover");
    
    return ESP_OK;
}

esp_err_t howdytts_http_server_stop(void)
{
    if (!s_http_server.is_running) {
        return ESP_OK;
    }
    
    esp_err_t ret = httpd_stop(s_http_server.server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_http_server.server = NULL;
    s_http_server.is_running = false;
    
    ESP_LOGI(TAG, "HowdyTTS HTTP server stopped");
    return ESP_OK;
}

static esp_err_t state_handler(httpd_req_t *req)
{
    s_http_server.state_requests++;
    
    // Parse POST data
    char buffer[256];
    esp_err_t ret = parse_post_data(req, buffer, sizeof(buffer));
    if (ret != ESP_OK) {
        return send_json_response(req, 400, "Invalid request data");
    }
    
    ESP_LOGI(TAG, "Received state request: %s", buffer);
    
    // Parse JSON data
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        ESP_LOGW(TAG, "Failed to parse JSON state data");
        return send_json_response(req, 400, "Invalid JSON");
    }
    
    cJSON *state_item = cJSON_GetObjectItem(json, "state");
    if (!state_item || !cJSON_IsString(state_item)) {
        cJSON_Delete(json);
        return send_json_response(req, 400, "Missing state field");
    }
    
    const char *state_str = cJSON_GetStringValue(state_item);
    howdytts_state_t state = howdytts_parse_state(state_str);
    
    ESP_LOGI(TAG, "State update: %s (%d)", state_str, state);
    
    // Notify application
    if (s_http_server.state_callback) {
        s_http_server.state_callback(state, NULL);
    }
    
    cJSON_Delete(json);
    return send_json_response(req, 200, "State updated");
}

static esp_err_t speak_handler(httpd_req_t *req)
{
    s_http_server.speak_requests++;
    
    // Parse POST data
    char buffer[1024];  // Larger buffer for text content
    esp_err_t ret = parse_post_data(req, buffer, sizeof(buffer));
    if (ret != ESP_OK) {
        return send_json_response(req, 400, "Invalid request data");
    }
    
    ESP_LOGI(TAG, "Received speak request");
    
    // Parse JSON data
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        ESP_LOGW(TAG, "Failed to parse JSON speak data");
        return send_json_response(req, 400, "Invalid JSON");
    }
    
    cJSON *text_item = cJSON_GetObjectItem(json, "text");
    if (!text_item || !cJSON_IsString(text_item)) {
        cJSON_Delete(json);
        return send_json_response(req, 400, "Missing text field");
    }
    
    const char *text = cJSON_GetStringValue(text_item);
    ESP_LOGI(TAG, "Speak text: %.50s%s", text, strlen(text) > 50 ? "..." : "");
    
    // Notify application with speaking state and text
    if (s_http_server.state_callback) {
        s_http_server.state_callback(HOWDYTTS_STATE_SPEAKING, text);
    }
    
    cJSON_Delete(json);
    return send_json_response(req, 200, "Speaking initiated");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    s_http_server.status_requests++;
    
    // Create status JSON response
    cJSON *status_json = cJSON_CreateObject();
    
    cJSON *device_id = cJSON_CreateString(s_http_server.config.device_id);
    cJSON *device_type = cJSON_CreateString("ESP32P4_HowdyScreen");
    cJSON *room = cJSON_CreateString(s_http_server.config.room);
    cJSON *status = cJSON_CreateString(s_http_server.device_status);
    cJSON *audio_level = cJSON_CreateNumber(s_http_server.audio_level);
    cJSON *battery_level = cJSON_CreateNumber(s_http_server.battery_level);
    cJSON *signal_strength = cJSON_CreateNumber(s_http_server.signal_strength);
    
    cJSON_AddItemToObject(status_json, "device_id", device_id);
    cJSON_AddItemToObject(status_json, "device_type", device_type);
    cJSON_AddItemToObject(status_json, "room", room);
    cJSON_AddItemToObject(status_json, "status", status);
    cJSON_AddItemToObject(status_json, "audio_level", audio_level);
    cJSON_AddItemToObject(status_json, "battery_level", battery_level);
    cJSON_AddItemToObject(status_json, "signal_strength", signal_strength);
    
    char *json_string = cJSON_Print(status_json);
    
    // Send JSON response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    ESP_LOGD(TAG, "Status response sent");
    
    free(json_string);
    cJSON_Delete(status_json);
    
    return ESP_OK;
}

static esp_err_t discover_handler(httpd_req_t *req)
{
    s_http_server.discovery_requests++;
    
    // Parse POST data for discovery information
    char buffer[256];
    esp_err_t ret = parse_post_data(req, buffer, sizeof(buffer));
    if (ret != ESP_OK) {
        return send_json_response(req, 400, "Invalid request data");
    }
    
    ESP_LOGI(TAG, "Received discovery request: %s", buffer);
    
    // Parse JSON data
    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        ESP_LOGW(TAG, "Failed to parse JSON discovery data");
        return send_json_response(req, 400, "Invalid JSON");
    }
    
    cJSON *server_ip_item = cJSON_GetObjectItem(json, "server_ip");
    cJSON *server_port_item = cJSON_GetObjectItem(json, "server_port");
    
    if (server_ip_item && cJSON_IsString(server_ip_item) && 
        server_port_item && cJSON_IsNumber(server_port_item)) {
        
        const char *server_ip = cJSON_GetStringValue(server_ip_item);
        uint16_t server_port = (uint16_t)cJSON_GetNumberValue(server_port_item);
        
        ESP_LOGI(TAG, "HowdyTTS server discovered: %s:%d", server_ip, server_port);
        
        // Notify application
        if (s_http_server.discovery_callback) {
            s_http_server.discovery_callback(server_ip, server_port);
        }
    }
    
    cJSON_Delete(json);
    return send_json_response(req, 200, "Discovery acknowledged");
}

static esp_err_t parse_post_data(httpd_req_t *req, char *buffer, size_t buffer_size)
{
    if (req->content_len >= buffer_size) {
        ESP_LOGW(TAG, "Request too large: %d bytes", req->content_len);
        return ESP_ERR_INVALID_SIZE;
    }
    
    int ret = httpd_req_recv(req, buffer, req->content_len);
    if (ret <= 0) {
        ESP_LOGW(TAG, "Failed to receive request data");
        return ESP_FAIL;
    }
    
    buffer[ret] = '\0';  // Null terminate
    return ESP_OK;
}

static esp_err_t send_json_response(httpd_req_t *req, int status_code, const char *message)
{
    cJSON *response = cJSON_CreateObject();
    cJSON *msg = cJSON_CreateString(message);
    cJSON *code = cJSON_CreateNumber(status_code);
    
    cJSON_AddItemToObject(response, "message", msg);
    cJSON_AddItemToObject(response, "code", code);
    
    char *json_string = cJSON_Print(response);
    
    httpd_resp_set_status(req, status_code == 200 ? "200 OK" : "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    free(json_string);
    cJSON_Delete(response);
    
    return ESP_OK;
}

esp_err_t howdytts_http_update_status(const char *status, 
                                     float audio_level,
                                     int battery_level,
                                     int signal_strength)
{
    if (status) {
        strncpy(s_http_server.device_status, status, sizeof(s_http_server.device_status) - 1);
        s_http_server.device_status[sizeof(s_http_server.device_status) - 1] = '\0';
    }
    
    s_http_server.audio_level = audio_level;
    s_http_server.battery_level = battery_level;
    s_http_server.signal_strength = signal_strength;
    
    ESP_LOGD(TAG, "Status updated: %s, audio: %.2f, battery: %d%%, signal: %ddBm",
             status ? status : "unchanged", audio_level, battery_level, signal_strength);
    
    return ESP_OK;
}

esp_err_t howdytts_http_get_stats(uint32_t *state_requests,
                                 uint32_t *speak_requests,
                                 uint32_t *discovery_requests,
                                 uint32_t *status_requests)
{
    if (state_requests) *state_requests = s_http_server.state_requests;
    if (speak_requests) *speak_requests = s_http_server.speak_requests;
    if (discovery_requests) *discovery_requests = s_http_server.discovery_requests;
    if (status_requests) *status_requests = s_http_server.status_requests;
    
    return ESP_OK;
}

howdytts_state_t howdytts_parse_state(const char *state_str)
{
    if (!state_str) {
        return HOWDYTTS_STATE_WAITING;
    }
    
    if (strcmp(state_str, "waiting") == 0) {
        return HOWDYTTS_STATE_WAITING;
    } else if (strcmp(state_str, "listening") == 0) {
        return HOWDYTTS_STATE_LISTENING;
    } else if (strcmp(state_str, "thinking") == 0) {
        return HOWDYTTS_STATE_THINKING;
    } else if (strcmp(state_str, "speaking") == 0) {
        return HOWDYTTS_STATE_SPEAKING;
    } else if (strcmp(state_str, "ending") == 0) {
        return HOWDYTTS_STATE_ENDING;
    }
    
    ESP_LOGW(TAG, "Unknown state string: %s", state_str);
    return HOWDYTTS_STATE_WAITING;
}

const char* howdytts_state_to_string(howdytts_state_t state)
{
    switch (state) {
        case HOWDYTTS_STATE_WAITING:  return "waiting";
        case HOWDYTTS_STATE_LISTENING: return "listening";
        case HOWDYTTS_STATE_THINKING:  return "thinking";
        case HOWDYTTS_STATE_SPEAKING:  return "speaking";
        case HOWDYTTS_STATE_ENDING:    return "ending";
        default: return "unknown";
    }
}

esp_err_t howdytts_register_device(void)
{
    // Initialize mDNS
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set mDNS hostname
    char hostname[64];
    snprintf(hostname, sizeof(hostname), "howdyscreen-%s", s_http_server.config.device_id);
    
    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Advertise as HowdyTTS client device
    ret = mdns_service_add(NULL, "_howdyclient", "_tcp", s_http_server.config.port, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add mDNS service: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add service TXT records
    mdns_txt_item_t txt_data[4] = {
        {"device_id", s_http_server.config.device_id},
        {"device_type", "ESP32P4_HowdyScreen"},
        {"room", s_http_server.config.room},
        {"version", "1.0"}
    };
    
    ret = mdns_service_txt_set("_howdyclient", "_tcp", txt_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS TXT records: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Device registered via mDNS as %s._howdyclient._tcp.local", hostname);
    return ESP_OK;
}

esp_err_t howdytts_unregister_device(void)
{
    mdns_service_remove("_howdyclient", "_tcp");
    mdns_free();
    
    ESP_LOGI(TAG, "Device unregistered from mDNS");
    return ESP_OK;
}