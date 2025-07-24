#include "server_discovery.h"
#include "esp_log.h"
#include "mdns.h"
#include "lwip/sockets.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static const char *TAG = "server_discovery";

esp_err_t server_discovery_init(server_discovery_t *discovery, const char *fallback_ips[], int ip_count)
{
    if (!discovery) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(discovery, 0, sizeof(server_discovery_t));
    
    // Copy fallback IPs
    discovery->count = (ip_count > MAX_SERVER_IPS) ? MAX_SERVER_IPS : ip_count;
    for (int i = 0; i < discovery->count; i++) {
        strncpy(discovery->ips[i], fallback_ips[i], MAX_IP_LENGTH - 1);
        ESP_LOGI(TAG, "Added fallback server: %s", discovery->ips[i]);
    }
    
    discovery->current_index = 0;
    discovery->mdns_enabled = false;
    
    return ESP_OK;
}

esp_err_t server_discovery_start_mdns(server_discovery_t *discovery)
{
    if (!discovery) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Starting mDNS discovery for HowdyTTS servers");
    
    // Initialize mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set mDNS hostname
    mdns_hostname_set("howdyscreen");
    mdns_instance_name_set("HowdyScreen ESP32P4");
    
    discovery->mdns_enabled = true;
    
    // Query for HowdyTTS services
    mdns_result_t *results = NULL;
    err = mdns_query_ptr(MDNS_SERVICE_NAME, MDNS_SERVICE_PROTO, 3000, 10, &results);
    if (err == ESP_OK && results) {
        mdns_result_t *r = results;
        
        // Use first found service
        if (r->addr) {
            struct in_addr addr;
            addr.s_addr = r->addr->addr.u_addr.ip4.addr;
            strncpy(discovery->discovered_ip, inet_ntoa(addr), MAX_IP_LENGTH - 1);
            discovery->discovered_port = r->port;
            
            ESP_LOGI(TAG, "Found HowdyTTS server via mDNS: %s:%d", 
                    discovery->discovered_ip, discovery->discovered_port);
        }
        
        mdns_query_results_free(results);
    }
    
    return ESP_OK;
}

bool server_discovery_test_connection(const char *ip, uint16_t port)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return false;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    
    // Send a test packet
    const char *test_msg = "PING";
    int sent = sendto(sock, test_msg, strlen(test_msg), 0,
                     (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    bool success = false;
    if (sent > 0) {
        // Wait for any response
        char buffer[64];
        struct sockaddr_in src_addr;
        socklen_t src_len = sizeof(src_addr);
        
        int received = recvfrom(sock, buffer, sizeof(buffer), 0,
                               (struct sockaddr*)&src_addr, &src_len);
        
        if (received > 0) {
            success = true;
            ESP_LOGI(TAG, "Server %s:%d is reachable", ip, port);
        }
    }
    
    close(sock);
    return success;
}

const char* server_discovery_get_next(server_discovery_t *discovery, uint16_t *port)
{
    if (!discovery) {
        return NULL;
    }
    
    // First try mDNS discovered server
    if (discovery->mdns_enabled && strlen(discovery->discovered_ip) > 0) {
        if (server_discovery_test_connection(discovery->discovered_ip, discovery->discovered_port)) {
            *port = discovery->discovered_port;
            return discovery->discovered_ip;
        }
    }
    
    // Fall back to IP list
    if (discovery->count == 0) {
        return NULL;
    }
    
    // Try each IP in round-robin fashion
    int start_index = discovery->current_index;
    do {
        const char *ip = discovery->ips[discovery->current_index];
        discovery->current_index = (discovery->current_index + 1) % discovery->count;
        
        if (server_discovery_test_connection(ip, *port)) {
            return ip;
        }
        
    } while (discovery->current_index != start_index);
    
    // No reachable servers found
    ESP_LOGW(TAG, "No reachable HowdyTTS servers found");
    return NULL;
}

const char* server_discovery_get_current(server_discovery_t *discovery, uint16_t *port)
{
    if (!discovery) {
        return NULL;
    }
    
    // Return mDNS discovered server if available
    if (discovery->mdns_enabled && strlen(discovery->discovered_ip) > 0) {
        *port = discovery->discovered_port;
        return discovery->discovered_ip;
    }
    
    // Return current fallback IP
    if (discovery->count > 0) {
        int index = (discovery->current_index == 0) ? 
                    discovery->count - 1 : discovery->current_index - 1;
        return discovery->ips[index];
    }
    
    return NULL;
}

void server_discovery_deinit(server_discovery_t *discovery)
{
    if (!discovery) {
        return;
    }
    
    if (discovery->mdns_enabled) {
        mdns_free();
        discovery->mdns_enabled = false;
    }
    
    memset(discovery, 0, sizeof(server_discovery_t));
}