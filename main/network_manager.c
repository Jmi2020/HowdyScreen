#include "network_manager.h"
#include "howdy_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
#include "esp_wifi_remote.h"
#endif

static const char *TAG = "network_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static uint16_t s_sequence_num = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    network_manager_t *manager = (network_manager_t*)arg;
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        manager->state = NETWORK_STATE_CONNECTING;
        ESP_LOGI(TAG, "WiFi connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            manager->state = NETWORK_STATE_ERROR;
        }
        ESP_LOGI(TAG, "Connection to WiFi failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        manager->state = NETWORK_STATE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t network_manager_init(network_manager_t *manager, const char *ssid, const char *password, const char *server_ip, uint16_t port)
{
    if (!manager || !ssid || !password || !server_ip) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(manager, 0, sizeof(network_manager_t));
    
    strncpy(manager->ssid, ssid, sizeof(manager->ssid) - 1);
    strncpy(manager->password, password, sizeof(manager->password) - 1);
    strncpy(manager->server_ip, server_ip, sizeof(manager->server_ip) - 1);
    manager->server_port = port;
    manager->udp_socket = -1;
    manager->state = NETWORK_STATE_DISCONNECTED;

    ESP_LOGI(TAG, "Initializing network manager");

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
    
#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
    // Initialize WiFi remote for ESP32-C6 co-processor
    wifi_remote_config_t remote_config = WIFI_REMOTE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_wifi_remote_init(&remote_config));
    ESP_LOGI(TAG, "ESP WiFi Remote initialized for ESP32-C6 co-processor");
#endif
    
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        manager,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        manager,
                                                        NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Disable power saving for low latency
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    manager->initialized = true;
    ESP_LOGI(TAG, "Network manager initialized");
    return ESP_OK;
}

esp_err_t network_manager_connect(network_manager_t *manager)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting WiFi connection to %s", manager->ssid);
    
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", manager->ssid);
        
        // Create UDP socket
        manager->udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (manager->udp_socket < 0) {
            ESP_LOGE(TAG, "Failed to create UDP socket");
            return ESP_FAIL;
        }

        // Set socket to non-blocking for receive
        int flags = fcntl(manager->udp_socket, F_GETFL, 0);
        fcntl(manager->udp_socket, F_SETFL, flags | O_NONBLOCK);

        // Set socket options for low latency
        int flag = 1;
        setsockopt(manager->udp_socket, IPPROTO_IP, IP_TOS, &flag, sizeof(flag));

        ESP_LOGI(TAG, "UDP socket created successfully");
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", manager->ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
        return ESP_FAIL;
    }
}

esp_err_t network_manager_disconnect(network_manager_t *manager)
{
    if (!manager || !manager->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Disconnecting from WiFi");

    if (manager->udp_socket >= 0) {
        close(manager->udp_socket);
        manager->udp_socket = -1;
    }

    ESP_ERROR_CHECK(esp_wifi_stop());
    manager->state = NETWORK_STATE_DISCONNECTED;

    ESP_LOGI(TAG, "WiFi disconnected");
    return ESP_OK;
}

network_state_t network_manager_get_state(network_manager_t *manager)
{
    if (!manager) {
        return NETWORK_STATE_ERROR;
    }
    return manager->state;
}

esp_err_t network_send_audio(network_manager_t *manager, const int16_t *audio_data, size_t frames)
{
    if (!manager || !audio_data || frames == 0 || manager->udp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (manager->state != NETWORK_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    // Create audio packet
    size_t data_size = frames * sizeof(int16_t);
    size_t packet_size = sizeof(audio_packet_t) + data_size;
    
    uint8_t *packet_buffer = malloc(packet_size);
    if (!packet_buffer) {
        return ESP_ERR_NO_MEM;
    }

    audio_packet_t *packet = (audio_packet_t*)packet_buffer;
    packet->timestamp = xTaskGetTickCount();
    packet->sequence = s_sequence_num++;
    packet->data_size = data_size;
    memcpy(packet->data, audio_data, data_size);

    // Send packet
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(manager->server_ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(manager->server_port);

    int sent = sendto(manager->udp_socket, packet_buffer, packet_size, 0, 
                      (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    free(packet_buffer);

    if (sent < 0) {
        ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

int network_receive_audio(network_manager_t *manager, int16_t *audio_buffer, size_t max_frames)
{
    if (!manager || !audio_buffer || max_frames == 0 || manager->udp_socket < 0) {
        return -1;
    }

    if (manager->state != NETWORK_STATE_CONNECTED) {
        return -1;
    }

    uint8_t recv_buffer[MAX_PACKET_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);

    int received = recvfrom(manager->udp_socket, recv_buffer, sizeof(recv_buffer), 0,
                           (struct sockaddr*)&src_addr, &src_len);

    if (received <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGE(TAG, "UDP receive failed: errno %d", errno);
        }
        return 0;  // No data available or error
    }

    if (received < sizeof(audio_packet_t)) {
        ESP_LOGW(TAG, "Received packet too small");
        return 0;
    }

    audio_packet_t *packet = (audio_packet_t*)recv_buffer;
    size_t audio_frames = packet->data_size / sizeof(int16_t);
    
    if (audio_frames > max_frames) {
        ESP_LOGW(TAG, "Received audio too large, truncating");
        audio_frames = max_frames;
    }

    memcpy(audio_buffer, packet->data, audio_frames * sizeof(int16_t));
    return audio_frames;
}

esp_err_t network_send_control(network_manager_t *manager, const char *message)
{
    if (!manager || !message || manager->udp_socket < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (manager->state != NETWORK_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(manager->server_ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(manager->server_port + 1);  // Control port

    int sent = sendto(manager->udp_socket, message, strlen(message), 0,
                      (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        ESP_LOGE(TAG, "Control message send failed: errno %d", errno);
        return ESP_FAIL;
    }

    return ESP_OK;
}

int network_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        return ap_info.rssi;
    }
    return -100;  // No signal
}

esp_err_t network_manager_set_server(network_manager_t *manager, const char *server_ip, uint16_t port)
{
    if (!manager || !server_ip) {
        return ESP_ERR_INVALID_ARG;
    }
    
    strncpy(manager->server_ip, server_ip, sizeof(manager->server_ip) - 1);
    manager->server_port = port;
    
    // If socket is already open, close it to force reconnection with new server
    if (manager->udp_socket >= 0) {
        close(manager->udp_socket);
        manager->udp_socket = -1;
    }
    
    ESP_LOGI(TAG, "Server updated to %s:%d", server_ip, port);
    return ESP_OK;
}

esp_err_t network_manager_deinit(network_manager_t *manager)
{
    if (!manager) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!manager->initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing network manager");

    if (manager->udp_socket >= 0) {
        close(manager->udp_socket);
        manager->udp_socket = -1;
    }

    esp_wifi_deinit();
    
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    memset(manager, 0, sizeof(network_manager_t));
    ESP_LOGI(TAG, "Network manager deinitialized");
    return ESP_OK;
}