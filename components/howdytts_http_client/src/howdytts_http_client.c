#include "howdytts_http_client.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "HowdyTTSClient";

#define MAX_HTTP_RESPONSE_SIZE 4096
#define MAX_MONITORED_SERVERS 10
#define DEFAULT_HEALTH_CHECK_INTERVAL 30000  // 30 seconds
#define DEFAULT_REQUEST_TIMEOUT 5000         // 5 seconds

// HTTP response context
typedef struct {
    char *buffer;
    size_t size;
    size_t offset;
    howdytts_response_callback_t callback;
    void *user_data;
} http_response_ctx_t;

// Monitored server entry
typedef struct {
    howdytts_server_info_t server;
    howdytts_server_health_t health;
    uint32_t last_check;
    bool active;
} monitored_server_t;

// Client state
static struct {
    bool initialized;
    howdytts_client_config_t config;
    howdytts_health_callback_t health_callback;
    
    // Health monitoring
    TaskHandle_t health_task_handle;
    bool health_monitoring;
    uint32_t health_interval;
    monitored_server_t servers[MAX_MONITORED_SERVERS];
    size_t server_count;
    SemaphoreHandle_t servers_mutex;
    
    // Statistics
    uint32_t requests_sent;
    uint32_t requests_failed;
    uint32_t total_response_time;
    
} s_client = {0};

// Forward declarations
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static void health_monitor_task(void *pvParameters);
static esp_err_t perform_http_request(const char *url, const char *method, 
                                     const char *post_data, http_response_ctx_t *ctx);
static esp_err_t add_monitored_server(const howdytts_server_info_t *server);
static esp_err_t update_server_health(const howdytts_server_info_t *server, 
                                     const howdytts_server_health_t *health);

esp_err_t howdytts_client_init(const howdytts_client_config_t *config,
                              howdytts_health_callback_t health_callback)
{
    if (s_client.initialized) {
        ESP_LOGI(TAG, "HowdyTTS client already initialized");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Initializing HowdyTTS HTTP client");
    ESP_LOGI(TAG, "Device: %s (%s)", config->device_name, config->device_id);
    ESP_LOGI(TAG, "Capabilities: %s", config->capabilities);
    
    // Copy configuration
    s_client.config = *config;
    s_client.health_callback = health_callback;
    
    // Set defaults
    if (s_client.config.health_check_interval == 0) {
        s_client.config.health_check_interval = DEFAULT_HEALTH_CHECK_INTERVAL;
    }
    if (s_client.config.request_timeout == 0) {
        s_client.config.request_timeout = DEFAULT_REQUEST_TIMEOUT;
    }
    
    // Create mutex for server list
    s_client.servers_mutex = xSemaphoreCreateMutex();
    if (!s_client.servers_mutex) {
        ESP_LOGE(TAG, "Failed to create servers mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize statistics
    s_client.requests_sent = 0;
    s_client.requests_failed = 0;
    s_client.total_response_time = 0;
    s_client.server_count = 0;
    s_client.health_monitoring = false;
    
    s_client.initialized = true;
    ESP_LOGI(TAG, "HowdyTTS client initialized successfully");
    
    return ESP_OK;
}

esp_err_t howdytts_client_health_check(const howdytts_server_info_t *server_info,
                                      howdytts_server_health_t *health)
{
    if (!s_client.initialized || !server_info || !health) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Health check: %s:%d", server_info->ip_addr, server_info->port);
    
    // Build health check URL
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/health", 
             server_info->ip_addr, server_info->port);
    
    // Prepare response context
    char response_buffer[1024];
    http_response_ctx_t ctx = {
        .buffer = response_buffer,
        .size = sizeof(response_buffer),
        .offset = 0,
        .callback = NULL,
        .user_data = NULL
    };
    
    uint32_t start_time = esp_timer_get_time() / 1000;  // Convert to ms
    esp_err_t ret = perform_http_request(url, "GET", NULL, &ctx);
    uint32_t end_time = esp_timer_get_time() / 1000;
    
    // Initialize health structure
    memset(health, 0, sizeof(howdytts_server_health_t));
    health->response_time_ms = end_time - start_time;
    health->last_check = end_time;
    
    if (ret == ESP_OK) {
        health->online = true;
        
        // Parse JSON response
        cJSON *json = cJSON_Parse(response_buffer);
        if (json) {
            cJSON *status = cJSON_GetObjectItem(json, "status");
            if (status && cJSON_IsString(status)) {
                strncpy(health->status, status->valuestring, sizeof(health->status) - 1);
            }
            
            cJSON *version = cJSON_GetObjectItem(json, "version");
            if (version && cJSON_IsString(version)) {
                strncpy(health->version, version->valuestring, sizeof(health->version) - 1);
            }
            
            cJSON *cpu = cJSON_GetObjectItem(json, "cpu_usage");
            if (cpu && cJSON_IsNumber(cpu)) {
                health->cpu_usage = (float)cpu->valuedouble;
            }
            
            cJSON *memory = cJSON_GetObjectItem(json, "memory_usage");
            if (memory && cJSON_IsNumber(memory)) {
                health->memory_usage = (float)memory->valuedouble;
            }
            
            cJSON *sessions = cJSON_GetObjectItem(json, "active_sessions");
            if (sessions && cJSON_IsNumber(sessions)) {
                health->active_sessions = sessions->valueint;
            }
            
            cJSON_Delete(json);
        }
        
        ESP_LOGI(TAG, "Health check passed: %s (response: %lums)", 
                 server_info->hostname, health->response_time_ms);
        
        // Add to monitoring if not already present
        add_monitored_server(server_info);
        update_server_health(server_info, health);
        
    } else {
        health->online = false;
        strcpy(health->status, "unreachable");
        ESP_LOGW(TAG, "Health check failed: %s:%d", server_info->ip_addr, server_info->port);
    }
    
    return ret;
}

esp_err_t howdytts_client_get_config(const howdytts_server_info_t *server_info,
                                    howdytts_response_callback_t callback,
                                    void *user_data)
{
    if (!s_client.initialized || !server_info || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Getting config from: %s:%d", server_info->ip_addr, server_info->port);
    
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/config", 
             server_info->ip_addr, server_info->port);
    
    char response_buffer[MAX_HTTP_RESPONSE_SIZE];
    http_response_ctx_t ctx = {
        .buffer = response_buffer,
        .size = sizeof(response_buffer),
        .offset = 0,
        .callback = callback,
        .user_data = user_data
    };
    
    return perform_http_request(url, "GET", NULL, &ctx);
}

esp_err_t howdytts_client_register_device(const howdytts_server_info_t *server_info,
                                         howdytts_response_callback_t callback,
                                         void *user_data)
{
    if (!s_client.initialized || !server_info || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Registering device with: %s:%d", server_info->ip_addr, server_info->port);
    
    // Build registration JSON
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_id", s_client.config.device_id);
    cJSON_AddStringToObject(json, "device_name", s_client.config.device_name);
    cJSON_AddStringToObject(json, "capabilities", s_client.config.capabilities);
    cJSON_AddStringToObject(json, "device_type", "ESP32-P4-HowdyScreen");
    
    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to create registration JSON");
        return ESP_FAIL;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/devices/register", 
             server_info->ip_addr, server_info->port);
    
    char response_buffer[MAX_HTTP_RESPONSE_SIZE];
    http_response_ctx_t ctx = {
        .buffer = response_buffer,
        .size = sizeof(response_buffer),
        .offset = 0,
        .callback = callback,
        .user_data = user_data
    };
    
    esp_err_t ret = perform_http_request(url, "POST", json_string, &ctx);
    free(json_string);
    
    return ret;
}

esp_err_t howdytts_client_start_health_monitor(uint32_t interval_ms)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_client.health_monitoring) {
        ESP_LOGI(TAG, "Health monitoring already active");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting health monitoring (interval: %lums)", interval_ms);
    
    s_client.health_interval = interval_ms;
    s_client.health_monitoring = true;
    
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        health_monitor_task,
        "howdy_health",
        4096,
        NULL,
        5,
        &s_client.health_task_handle,
        0  // Core 0
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create health monitor task");
        s_client.health_monitoring = false;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t howdytts_client_stop_health_monitor(void)
{
    if (!s_client.health_monitoring) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping health monitoring");
    
    s_client.health_monitoring = false;
    
    if (s_client.health_task_handle) {
        vTaskDelete(s_client.health_task_handle);
        s_client.health_task_handle = NULL;
    }
    
    return ESP_OK;
}

esp_err_t howdytts_client_get_stats_local(uint32_t *requests_sent,
                                         uint32_t *requests_failed,
                                         uint32_t *avg_response_time,
                                         uint32_t *servers_monitored)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (requests_sent) *requests_sent = s_client.requests_sent;
    if (requests_failed) *requests_failed = s_client.requests_failed;
    if (servers_monitored) *servers_monitored = s_client.server_count;
    
    if (avg_response_time) {
        if (s_client.requests_sent > 0) {
            *avg_response_time = s_client.total_response_time / s_client.requests_sent;
        } else {
            *avg_response_time = 0;
        }
    }
    
    return ESP_OK;
}

static esp_err_t perform_http_request(const char *url, const char *method, 
                                     const char *post_data, http_response_ctx_t *ctx)
{
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = ctx,
        .timeout_ms = s_client.config.request_timeout,
        .buffer_size = MAX_HTTP_RESPONSE_SIZE,
        .buffer_size_tx = 1024
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        s_client.requests_failed++;
        return ESP_FAIL;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Set method
    if (strcmp(method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        if (post_data) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, post_data, strlen(post_data));
        }
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }
    
    // Set user agent
    esp_http_client_set_header(client, "User-Agent", "ESP32-P4-HowdyScreen/1.0");
    
    uint32_t start_time = esp_timer_get_time() / 1000;
    ret = esp_http_client_perform(client);
    uint32_t end_time = esp_timer_get_time() / 1000;
    
    if (ret == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        
        if (status_code >= 200 && status_code < 300) {
            s_client.requests_sent++;
            s_client.total_response_time += (end_time - start_time);
            
            // Call response callback if provided
            if (ctx->callback) {
                ctx->callback(ctx->buffer, ctx->offset, ctx->user_data);
            }
            
        } else {
            ESP_LOGW(TAG, "HTTP request failed with status: %d", status_code);
            s_client.requests_failed++;
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
        s_client.requests_failed++;
    }
    
    esp_http_client_cleanup(client);
    return ret;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (ctx && ctx->buffer && evt->data_len > 0) {
                size_t copy_len = evt->data_len;
                if (ctx->offset + copy_len >= ctx->size) {
                    copy_len = ctx->size - ctx->offset - 1;  // Leave space for null terminator
                }
                
                if (copy_len > 0) {
                    memcpy(ctx->buffer + ctx->offset, evt->data, copy_len);
                    ctx->offset += copy_len;
                    ctx->buffer[ctx->offset] = '\0';  // Null terminate
                }
            }
            break;
            
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
            
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
            
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
            
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER: %.*s", evt->data_len, (char*)evt->data);
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

static void health_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Health monitor task started");
    
    while (s_client.health_monitoring) {
        // Get all discovered servers from service discovery
        howdytts_server_info_t servers[10];
        size_t num_servers = 0;
        
        if (service_discovery_get_servers(servers, 10, &num_servers) == ESP_OK) {
            for (size_t i = 0; i < num_servers; i++) {
                howdytts_server_health_t health;
                esp_err_t ret = howdytts_client_health_check(&servers[i], &health);
                
                if (ret == ESP_OK && s_client.health_callback) {
                    s_client.health_callback(&servers[i], &health);
                }
            }
        }
        
        // Wait for next check
        vTaskDelay(pdMS_TO_TICKS(s_client.health_interval));
    }
    
    ESP_LOGI(TAG, "Health monitor task ended");
    s_client.health_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t add_monitored_server(const howdytts_server_info_t *server)
{
    if (!server || xSemaphoreTake(s_client.servers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    // Check if server already exists
    for (size_t i = 0; i < s_client.server_count; i++) {
        if (strcmp(s_client.servers[i].server.hostname, server->hostname) == 0) {
            s_client.servers[i].server = *server;  // Update server info
            s_client.servers[i].active = true;
            xSemaphoreGive(s_client.servers_mutex);
            return ESP_OK;
        }
    }
    
    // Add new server if space available
    if (s_client.server_count < MAX_MONITORED_SERVERS) {
        s_client.servers[s_client.server_count].server = *server;
        s_client.servers[s_client.server_count].active = true;
        s_client.servers[s_client.server_count].last_check = 0;
        s_client.server_count++;
    }
    
    xSemaphoreGive(s_client.servers_mutex);
    return ESP_OK;
}

static esp_err_t update_server_health(const howdytts_server_info_t *server, 
                                     const howdytts_server_health_t *health)
{
    if (!server || !health || xSemaphoreTake(s_client.servers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_FAIL;
    }
    
    for (size_t i = 0; i < s_client.server_count; i++) {
        if (strcmp(s_client.servers[i].server.hostname, server->hostname) == 0) {
            s_client.servers[i].health = *health;
            s_client.servers[i].last_check = esp_timer_get_time() / 1000;
            break;
        }
    }
    
    xSemaphoreGive(s_client.servers_mutex);
    return ESP_OK;
}