#include "service_discovery.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mdns.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ServiceDiscovery";

#define MAX_DISCOVERED_SERVERS 10
#define MDNS_SERVICE_TYPE "_howdytts"
#define MDNS_PROTOCOL "_tcp"
#define SCAN_TASK_PRIORITY 5
#define SCAN_TASK_STACK_SIZE 4096

// Service discovery state
static struct {
    bool initialized;
    service_discovered_callback_t callback;
    howdytts_server_info_t servers[MAX_DISCOVERED_SERVERS];
    size_t num_servers;
    SemaphoreHandle_t servers_mutex;
    TaskHandle_t scan_task_handle;
    bool scanning;
    uint32_t scan_duration_ms;
} s_discovery = {0};

// Forward declarations
static void scan_task(void *pvParameters);
static esp_err_t parse_mdns_result(mdns_result_t *result, howdytts_server_info_t *server_info);
static esp_err_t add_server(const howdytts_server_info_t *server_info);
static bool is_server_known(const char *hostname);

esp_err_t service_discovery_init(service_discovered_callback_t callback)
{
    if (s_discovery.initialized) {
        ESP_LOGI(TAG, "Service discovery already initialized");
        return ESP_OK;
    }

    if (!callback) {
        ESP_LOGE(TAG, "Invalid callback function");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing mDNS service discovery for HowdyTTS servers");

    // Initialize mDNS
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set mDNS hostname
    ret = mdns_hostname_set("howdy-esp32p4");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }

    // Set default mDNS instance
    ret = mdns_instance_name_set("HowdyTTS ESP32-P4 Client");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }

    // Create mutex for server list protection
    s_discovery.servers_mutex = xSemaphoreCreateMutex();
    if (!s_discovery.servers_mutex) {
        ESP_LOGE(TAG, "Failed to create servers mutex");
        mdns_free();
        return ESP_ERR_NO_MEM;
    }

    s_discovery.callback = callback;
    s_discovery.num_servers = 0;
    s_discovery.scanning = false;
    s_discovery.initialized = true;

    ESP_LOGI(TAG, "Service discovery initialized successfully");
    ESP_LOGI(TAG, "Looking for service: %s.%s", MDNS_SERVICE_TYPE, MDNS_PROTOCOL);
    
    return ESP_OK;
}

esp_err_t service_discovery_start_scan(uint32_t scan_duration_ms)
{
    if (!s_discovery.initialized) {
        ESP_LOGE(TAG, "Service discovery not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_discovery.scanning) {
        ESP_LOGI(TAG, "Scan already in progress");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HowdyTTS server scan (duration: %lu ms)", scan_duration_ms);

    s_discovery.scan_duration_ms = scan_duration_ms;
    s_discovery.scanning = true;

    // Create scan task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        scan_task,
        "service_scan",
        SCAN_TASK_STACK_SIZE,
        NULL,
        SCAN_TASK_PRIORITY,
        &s_discovery.scan_task_handle,
        0  // Core 0 (with network tasks)
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create scan task");
        s_discovery.scanning = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t service_discovery_stop_scan(void)
{
    if (!s_discovery.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_discovery.scanning) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HowdyTTS server scan");
    
    s_discovery.scanning = false;
    
    // Wait for scan task to finish
    if (s_discovery.scan_task_handle) {
        vTaskDelete(s_discovery.scan_task_handle);
        s_discovery.scan_task_handle = NULL;
    }

    return ESP_OK;
}

esp_err_t service_discovery_get_servers(howdytts_server_info_t *servers, 
                                       size_t max_servers, size_t *num_servers)
{
    if (!s_discovery.initialized || !servers || !num_servers) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_discovery.servers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire servers mutex");
        return ESP_ERR_TIMEOUT;
    }

    size_t copy_count = (s_discovery.num_servers < max_servers) ? s_discovery.num_servers : max_servers;
    
    memcpy(servers, s_discovery.servers, copy_count * sizeof(howdytts_server_info_t));
    *num_servers = copy_count;

    xSemaphoreGive(s_discovery.servers_mutex);

    ESP_LOGI(TAG, "Retrieved %zu HowdyTTS servers", *num_servers);
    return ESP_OK;
}

esp_err_t service_discovery_get_best_server(howdytts_server_info_t *server_info)
{
    if (!s_discovery.initialized || !server_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_discovery.servers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire servers mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (s_discovery.num_servers == 0) {
        xSemaphoreGive(s_discovery.servers_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // For now, just return the first server
    // TODO: Implement server scoring based on ping, load, etc.
    *server_info = s_discovery.servers[0];

    xSemaphoreGive(s_discovery.servers_mutex);

    ESP_LOGI(TAG, "Best server: %s:%d (%s)", 
             server_info->ip_addr, server_info->port, server_info->hostname);
    
    return ESP_OK;
}

esp_err_t service_discovery_advertise_client(const char *device_name, const char *capabilities)
{
    if (!s_discovery.initialized || !device_name || !capabilities) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Advertising HowdyTTS client: %s", device_name);

    // Add service
    esp_err_t ret = mdns_service_add(NULL, "_howdyclient", "_tcp", 8080, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add mDNS service: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add service instance
    ret = mdns_service_instance_name_set("_howdyclient", "_tcp", device_name);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set service instance name: %s", esp_err_to_name(ret));
        return ret;
    }

    // Add TXT records
    mdns_txt_item_t txt_data[] = {
        {"capabilities", capabilities},
        {"version", "1.0"},
        {"device", "ESP32-P4"},
        {"display", "800x800"}
    };

    ret = mdns_service_txt_set("_howdyclient", "_tcp", txt_data, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set TXT records: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Client advertisement started successfully");
    return ESP_OK;
}

esp_err_t service_discovery_stop_advertising(void)
{
    if (!s_discovery.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping client advertisement");
    
    esp_err_t ret = mdns_service_remove("_howdyclient", "_tcp");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove mDNS service: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t service_discovery_test_server(const howdytts_server_info_t *server_info, 
                                       uint32_t timeout_ms, uint32_t *latency_ms)
{
    if (!server_info || !latency_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Testing connectivity to %s:%d", server_info->ip_addr, server_info->port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_info->port);
    inet_pton(AF_INET, server_info->ip_addr, &server_addr.sin_addr);

    uint32_t start_time = esp_timer_get_time() / 1000;  // Convert to ms
    int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    uint32_t end_time = esp_timer_get_time() / 1000;

    close(sock);

    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to connect to server: %d", errno);
        return ESP_FAIL;
    }

    *latency_ms = end_time - start_time;
    ESP_LOGI(TAG, "Server connectivity test successful - latency: %lu ms", *latency_ms);
    
    return ESP_OK;
}

static void scan_task(void *pvParameters)
{
    ESP_LOGI(TAG, "mDNS scan task started");

    uint32_t start_time = esp_timer_get_time() / 1000;  // Convert to ms
    
    while (s_discovery.scanning) {
        // Check scan duration
        if (s_discovery.scan_duration_ms > 0) {
            uint32_t elapsed = (esp_timer_get_time() / 1000) - start_time;
            if (elapsed >= s_discovery.scan_duration_ms) {
                ESP_LOGI(TAG, "Scan duration completed");
                break;
            }
        }

        ESP_LOGI(TAG, "Scanning for HowdyTTS servers...");
        
        // Query for HowdyTTS services
        mdns_result_t *results = NULL;
        esp_err_t ret = mdns_query_ptr(MDNS_SERVICE_TYPE, MDNS_PROTOCOL, 3000, 20, &results);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "mDNS query failed: %s", esp_err_to_name(ret));
        } else if (results) {
            mdns_result_t *r = results;
            int found_count = 0;
            
            while (r) {
                howdytts_server_info_t server_info;
                if (parse_mdns_result(r, &server_info) == ESP_OK) {
                    if (!is_server_known(server_info.hostname)) {
                        add_server(&server_info);
                        
                        // Notify callback
                        if (s_discovery.callback) {
                            s_discovery.callback(&server_info);
                        }
                        
                        found_count++;
                        ESP_LOGI(TAG, "Discovered HowdyTTS server: %s (%s:%d)", 
                                 server_info.hostname, server_info.ip_addr, server_info.port);
                    }
                }
                r = r->next;
            }
            
            if (found_count > 0) {
                ESP_LOGI(TAG, "Found %d new HowdyTTS servers", found_count);
            } else {
                ESP_LOGD(TAG, "No new servers found in this scan");
            }
            
            mdns_query_results_free(results);
            
        } else {
            ESP_LOGD(TAG, "No HowdyTTS servers found");
        }

        // Wait before next scan
        vTaskDelay(pdMS_TO_TICKS(5000));  // Scan every 5 seconds
    }

    ESP_LOGI(TAG, "mDNS scan task ended");
    s_discovery.scanning = false;
    s_discovery.scan_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t parse_mdns_result(mdns_result_t *result, howdytts_server_info_t *server_info)
{
    if (!result || !server_info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(server_info, 0, sizeof(howdytts_server_info_t));

    // Get hostname
    if (result->hostname) {
        strncpy(server_info->hostname, result->hostname, sizeof(server_info->hostname) - 1);
    }

    // Get IP address
    if (result->addr) {
        esp_ip4_addr_t *addr4 = &result->addr->addr.u_addr.ip4;
        snprintf(server_info->ip_addr, sizeof(server_info->ip_addr), 
                IPSTR, IP2STR(addr4));
        
        // Check for invalid IP address (0.0.0.0)
        if (strcmp(server_info->ip_addr, "0.0.0.0") == 0) {
            // Try to resolve hostname if IP is invalid
            ESP_LOGW(TAG, "Invalid IP address 0.0.0.0 for %s, attempting hostname resolution", server_info->hostname);
            
            // For local testing, use gateway IP (typically the host computer)
            if (strstr(server_info->hostname, "esp32-test-server") != NULL || 
                strstr(server_info->hostname, "test") != NULL) {
                // Use computer's IP address for local test server
                strncpy(server_info->ip_addr, "192.168.86.39", sizeof(server_info->ip_addr) - 1);
                ESP_LOGI(TAG, "Using computer IP %s for test server", server_info->ip_addr);
            } else {
                // For production, this would need proper hostname resolution
                ESP_LOGW(TAG, "Cannot resolve hostname %s", server_info->hostname);
                return ESP_FAIL;
            }
        }
    } else {
        ESP_LOGW(TAG, "No IP address found for %s", server_info->hostname);
        return ESP_FAIL;
    }

    // Get port
    server_info->port = result->port;
    if (server_info->port == 0) {
        server_info->port = 8080;  // Default HowdyTTS port
    }

    // Parse TXT records
    if (result->txt_count > 0) {
        for (size_t i = 0; i < result->txt_count; i++) {
            mdns_txt_item_t *txt = &result->txt[i];
            if (txt->key && txt->value) {
                if (strcmp(txt->key, "version") == 0) {
                    strncpy(server_info->version, txt->value, sizeof(server_info->version) - 1);
                } else if (strcmp(txt->key, "secure") == 0) {
                    server_info->secure = (strcmp(txt->value, "true") == 0);
                }
            }
        }
    }

    server_info->last_seen = esp_timer_get_time() / 1000;  // Current time in ms

    return ESP_OK;
}

static esp_err_t add_server(const howdytts_server_info_t *server_info)
{
    if (!server_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_discovery.servers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire servers mutex");
        return ESP_ERR_TIMEOUT;
    }

    if (s_discovery.num_servers >= MAX_DISCOVERED_SERVERS) {
        ESP_LOGW(TAG, "Server list full, replacing oldest entry");
        // Replace the first (oldest) entry
        s_discovery.servers[0] = *server_info;
    } else {
        s_discovery.servers[s_discovery.num_servers] = *server_info;
        s_discovery.num_servers++;
    }

    xSemaphoreGive(s_discovery.servers_mutex);
    return ESP_OK;
}

static bool is_server_known(const char *hostname)
{
    if (!hostname) {
        return false;
    }

    if (xSemaphoreTake(s_discovery.servers_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool found = false;
    for (size_t i = 0; i < s_discovery.num_servers; i++) {
        if (strcmp(s_discovery.servers[i].hostname, hostname) == 0) {
            // Update last seen time
            s_discovery.servers[i].last_seen = esp_timer_get_time() / 1000;
            found = true;
            break;
        }
    }

    xSemaphoreGive(s_discovery.servers_mutex);
    return found;
}