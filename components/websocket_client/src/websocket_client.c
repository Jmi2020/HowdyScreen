#include "websocket_client.h"
#include "howdytts_protocol.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "WebSocketClient";

// Client state
static struct {
    esp_websocket_client_handle_t client;
    ws_client_config_t config;
    ws_event_callback_t event_callback;
    ws_client_state_t state;
    SemaphoreHandle_t state_mutex;
    QueueHandle_t send_queue;
    TaskHandle_t send_task_handle;
    
    // Audio interface integration
    ws_audio_callback_t audio_callback;
    void *audio_user_data;
    
    // Statistics
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t reconnect_count;
    uint32_t last_ping_time;
    bool ping_pending;
} s_ws_client = {0};

// Message queue item
typedef struct {
    ws_message_type_t type;
    uint8_t *data;
    size_t len;
} ws_message_item_t;

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void send_task(void *pvParameters);
static esp_err_t set_client_state(ws_client_state_t new_state);
static esp_err_t queue_message(ws_message_type_t type, const uint8_t *data, size_t len);

esp_err_t ws_client_init(const ws_client_config_t *config, ws_event_callback_t event_callback)
{
    if (!config || !event_callback) {
        ESP_LOGE(TAG, "Invalid configuration or callback");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ws_client.client) {
        ESP_LOGI(TAG, "WebSocket client already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WebSocket client for HowdyTTS");

    // Copy configuration
    s_ws_client.config = *config;
    s_ws_client.event_callback = event_callback;

    // Create mutex for state protection
    s_ws_client.state_mutex = xSemaphoreCreateMutex();
    if (!s_ws_client.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create message queue for batching
    s_ws_client.send_queue = xQueueCreate(20, sizeof(ws_message_item_t));
    if (!s_ws_client.send_queue) {
        ESP_LOGE(TAG, "Failed to create send queue");
        vSemaphoreDelete(s_ws_client.state_mutex);
        return ESP_ERR_NO_MEM;
    }

    // Configure WebSocket client
    esp_websocket_client_config_t websocket_cfg = {
        .uri = s_ws_client.config.server_uri,
        .buffer_size = s_ws_client.config.buffer_size,
        .reconnect_timeout_ms = s_ws_client.config.reconnect_timeout_ms,
        .network_timeout_ms = 5000,
        .disable_auto_reconnect = !s_ws_client.config.auto_reconnect,
        .keep_alive_idle = s_ws_client.config.keepalive_idle_sec,
        .keep_alive_interval = s_ws_client.config.keepalive_interval_sec,
        .keep_alive_count = s_ws_client.config.keepalive_count,
    };

    s_ws_client.client = esp_websocket_client_init(&websocket_cfg);
    if (!s_ws_client.client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        vQueueDelete(s_ws_client.send_queue);
        vSemaphoreDelete(s_ws_client.state_mutex);
        return ESP_FAIL;
    }

    // Register event handler
    esp_websocket_register_events(s_ws_client.client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    // Initialize HowdyTTS protocol
    howdytts_session_config_t session_config = {
        .session_id = "esp32_howdy_001",
        .device_id = "ESP32P4",
        .audio_config = {
            .sample_rate = 16000,
            .channels = 1,
            .bits_per_sample = 16,
            .use_compression = false  // Start without compression, add later
        },
        .keepalive_interval_ms = 30000
    };
    strcpy(session_config.session_id, "esp32_howdy_001");
    strcpy(session_config.device_id, "ESP32P4");

    esp_err_t ret = howdytts_protocol_init(&session_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HowdyTTS protocol: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(s_ws_client.client);
        vQueueDelete(s_ws_client.send_queue);
        vSemaphoreDelete(s_ws_client.state_mutex);
        return ret;
    }

    // Create send task for message batching
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        send_task,
        "ws_send",
        8192,  // Increased from 4096 to prevent stack overflow
        NULL,
        5,  // Medium priority
        &s_ws_client.send_task_handle,
        0   // Core 0 (with network tasks)
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create send task");
        esp_websocket_client_destroy(s_ws_client.client);
        vQueueDelete(s_ws_client.send_queue);
        vSemaphoreDelete(s_ws_client.state_mutex);
        return ESP_FAIL;
    }

    set_client_state(WS_CLIENT_STATE_DISCONNECTED);
    
    ESP_LOGI(TAG, "WebSocket client initialized successfully");
    ESP_LOGI(TAG, "Server URI: %s", s_ws_client.config.server_uri);
    ESP_LOGI(TAG, "Buffer size: %lu bytes", s_ws_client.config.buffer_size);
    
    return ESP_OK;
}

esp_err_t ws_client_start(void)
{
    if (!s_ws_client.client) {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting WebSocket client connection to HowdyTTS server");
    
    set_client_state(WS_CLIENT_STATE_CONNECTING);
    
    esp_err_t ret = esp_websocket_client_start(s_ws_client.client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        set_client_state(WS_CLIENT_STATE_ERROR);
        return ret;
    }

    return ESP_OK;
}

esp_err_t ws_client_stop(void)
{
    if (!s_ws_client.client) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping WebSocket client");
    
    esp_err_t ret = esp_websocket_client_stop(s_ws_client.client);
    set_client_state(WS_CLIENT_STATE_DISCONNECTED);
    
    return ret;
}

esp_err_t ws_client_send_audio(const int16_t *audio_data, size_t samples, uint32_t sample_rate)
{
    if (!s_ws_client.client || !audio_data || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    // Queue audio message for batching
    size_t data_size = samples * sizeof(int16_t) + sizeof(uint32_t);  // Include sample rate
    uint8_t *message_data = malloc(data_size);
    if (!message_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio message");
        return ESP_ERR_NO_MEM;
    }

    // Pack sample rate and audio data
    memcpy(message_data, &sample_rate, sizeof(uint32_t));
    memcpy(message_data + sizeof(uint32_t), audio_data, samples * sizeof(int16_t));

    esp_err_t ret = queue_message(WS_MSG_TYPE_AUDIO_STREAM, message_data, data_size);
    if (ret != ESP_OK) {
        free(message_data);
        return ret;
    }

    return ESP_OK;
}

esp_err_t ws_client_send_text(const char *text)
{
    if (!s_ws_client.client || !text) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t text_len = strlen(text);
    uint8_t *message_data = malloc(text_len + 1);
    if (!message_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for text message");
        return ESP_ERR_NO_MEM;
    }

    strcpy((char*)message_data, text);
    
    esp_err_t ret = queue_message(WS_MSG_TYPE_CONTROL, message_data, text_len + 1);
    if (ret != ESP_OK) {
        free(message_data);
        return ret;
    }

    return ESP_OK;
}

esp_err_t ws_client_ping(void)
{
    if (!s_ws_client.client) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    char ping_message[128];  // Increased from 64 to 128 bytes for larger ping messages
    esp_err_t ret = howdytts_create_ping_message(ping_message, sizeof(ping_message));
    if (ret != ESP_OK) {
        return ret;
    }

    s_ws_client.ping_pending = true;
    s_ws_client.last_ping_time = esp_timer_get_time() / 1000;  // Convert to ms

    ret = esp_websocket_client_send_text(s_ws_client.client, ping_message, strlen(ping_message), portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send ping");
        s_ws_client.ping_pending = false;
        return ESP_FAIL;
    }

    s_ws_client.bytes_sent += strlen(ping_message);
    ESP_LOGD(TAG, "Ping sent to server");
    
    return ESP_OK;
}

ws_client_state_t ws_client_get_state(void)
{
    return s_ws_client.state;
}

esp_err_t ws_client_get_stats(uint32_t *bytes_sent, uint32_t *bytes_received, uint32_t *reconnect_count)
{
    if (!bytes_sent || !bytes_received || !reconnect_count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ws_client.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *bytes_sent = s_ws_client.bytes_sent;
        *bytes_received = s_ws_client.bytes_received;
        *reconnect_count = s_ws_client.reconnect_count;
        xSemaphoreGive(s_ws_client.state_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t set_client_state(ws_client_state_t new_state)
{
    if (xSemaphoreTake(s_ws_client.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ws_client_state_t old_state = s_ws_client.state;
        s_ws_client.state = new_state;
        xSemaphoreGive(s_ws_client.state_mutex);

        if (old_state != new_state) {
            ESP_LOGI(TAG, "WebSocket state changed: %d -> %d", old_state, new_state);
            
            // Notify application via callback
            if (s_ws_client.event_callback) {
                s_ws_client.event_callback(new_state, WS_MSG_TYPE_STATUS, NULL, 0);
            }
        }
        
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

static esp_err_t queue_message(ws_message_type_t type, const uint8_t *data, size_t len)
{
    ws_message_item_t item = {
        .type = type,
        .data = (uint8_t*)data,  // Transfer ownership
        .len = len
    };

    if (xQueueSend(s_ws_client.send_queue, &item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue message - queue full");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void send_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WebSocket send task started");
    
    ws_message_item_t item;
    char json_buffer[2048];  // Buffer for JSON messages
    
    while (1) {
        // Wait for messages in queue
        if (xQueueReceive(s_ws_client.send_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
                ESP_LOGW(TAG, "Dropping message - not connected");
                free(item.data);
                continue;
            }

            esp_err_t ret = ESP_OK;
            
            switch (item.type) {
                case WS_MSG_TYPE_AUDIO_STREAM: {
                    // Extract sample rate and audio data
                    uint32_t sample_rate;
                    memcpy(&sample_rate, item.data, sizeof(uint32_t));
                    int16_t *audio_data = (int16_t*)(item.data + sizeof(uint32_t));
                    size_t samples = (item.len - sizeof(uint32_t)) / sizeof(int16_t);
                    
                    ret = howdytts_create_audio_message(audio_data, samples, json_buffer, sizeof(json_buffer));
                    if (ret == ESP_OK) {
                        int bytes_sent = esp_websocket_client_send_text(s_ws_client.client, json_buffer, 
                                                                       strlen(json_buffer), pdMS_TO_TICKS(1000));
                        if (bytes_sent > 0) {
                            s_ws_client.bytes_sent += bytes_sent;
                            ESP_LOGD(TAG, "Audio message sent: %d samples", samples);
                        } else {
                            ESP_LOGE(TAG, "Failed to send audio message");
                        }
                    }
                    break;
                }
                
                case WS_MSG_TYPE_CONTROL: {
                    int bytes_sent = esp_websocket_client_send_text(s_ws_client.client, (char*)item.data, 
                                                                   strlen((char*)item.data), pdMS_TO_TICKS(1000));
                    if (bytes_sent > 0) {
                        s_ws_client.bytes_sent += bytes_sent;
                        ESP_LOGD(TAG, "Control message sent: %s", (char*)item.data);
                    } else {
                        ESP_LOGE(TAG, "Failed to send control message");
                    }
                    break;
                }
                
                default:
                    ESP_LOGW(TAG, "Unknown message type: %d", item.type);
                    break;
            }
            
            // Free the message data
            free(item.data);
        }
        
        // Periodic keepalive ping
        static uint32_t last_ping_check = 0;
        uint32_t now = esp_timer_get_time() / 1000;  // Convert to ms
        
        if (now - last_ping_check > s_ws_client.config.keepalive_idle_sec * 1000) {
            if (s_ws_client.state == WS_CLIENT_STATE_CONNECTED) {
                ws_client_ping();
            }
            last_ping_check = now;
        }
    }
    
    ESP_LOGI(TAG, "WebSocket send task ended");
    vTaskDelete(NULL);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected to HowdyTTS server");
            set_client_state(WS_CLIENT_STATE_CONNECTED);
            
            // Send session start message
            char session_msg[256];
            if (howdytts_create_session_start_message(session_msg, sizeof(session_msg)) == ESP_OK) {
                esp_websocket_client_send_text(s_ws_client.client, session_msg, strlen(session_msg), portMAX_DELAY);
                s_ws_client.bytes_sent += strlen(session_msg);
                ESP_LOGI(TAG, "Session start message sent");
            }
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected from HowdyTTS server");
            set_client_state(WS_CLIENT_STATE_DISCONNECTED);
            s_ws_client.reconnect_count++;
            break;
            
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGD(TAG, "WebSocket data received: %d bytes", data->data_len);
            s_ws_client.bytes_received += data->data_len;
            
            if (data->op_code == 0x01) {  // Text frame
                // Parse TTS response or other control messages
                char *message = malloc(data->data_len + 1);
                if (message) {
                    memcpy(message, data->data_ptr, data->data_len);
                    message[data->data_len] = '\0';
                    
                    ESP_LOGD(TAG, "Received message: %s", message);
                    
                    // Check if it's a pong response
                    if (strstr(message, "pong") && s_ws_client.ping_pending) {
                        s_ws_client.ping_pending = false;
                        uint32_t ping_time = (esp_timer_get_time() / 1000) - s_ws_client.last_ping_time;
                        ESP_LOGI(TAG, "Pong received - RTT: %lu ms", ping_time);
                    }
                    
                    // Notify application
                    if (s_ws_client.event_callback) {
                        s_ws_client.event_callback(s_ws_client.state, WS_MSG_TYPE_CONTROL, 
                                                 (const uint8_t*)message, data->data_len);
                    }
                    
                    free(message);
                }
            } else if (data->op_code == 0x02) {  // Binary frame (TTS audio)
                ESP_LOGI(TAG, "Received TTS audio data: %d bytes", data->data_len);
                
                // Handle binary audio response using our new function
                ws_client_handle_binary_response((const uint8_t*)data->data_ptr, data->data_len);
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error occurred");
            set_client_state(WS_CLIENT_STATE_ERROR);
            break;
            
        default:
            break;
    }
}

// New binary audio streaming functions inspired by Arduino implementation

esp_err_t ws_client_send_binary_audio(const int16_t *buffer, size_t samples)
{
    if (!buffer || samples == 0) {
        ESP_LOGE(TAG, "Invalid audio buffer parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WebSocket not connected - cannot send binary audio");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Calculate buffer size in bytes
    size_t buffer_size = samples * sizeof(int16_t);
    
    // Send binary audio data directly (similar to Arduino sendBinary)
    int ret = esp_websocket_client_send_bin(s_ws_client.client, 
                                           (const char*)buffer, 
                                           buffer_size, 
                                           portMAX_DELAY);
    
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send binary audio data");
        return ESP_FAIL;
    }
    
    s_ws_client.bytes_sent += buffer_size;
    ESP_LOGD(TAG, "Sent binary audio: %zu samples (%zu bytes)", samples, buffer_size);
    
    return ESP_OK;
}

esp_err_t ws_client_handle_binary_response(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        ESP_LOGW(TAG, "Received empty binary audio response");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Processing binary TTS audio response: %zu bytes", length);
    
    // Send TTS audio to audio interface coordinator for playback
    if (s_ws_client.audio_callback) {
        esp_err_t ret = s_ws_client.audio_callback(data, length, s_ws_client.audio_user_data);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Audio interface callback failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "No audio callback registered - TTS audio dropped");
    }
    
    // Also notify the main application callback
    if (s_ws_client.event_callback) {
        s_ws_client.event_callback(s_ws_client.state, WS_MSG_TYPE_TTS_RESPONSE, data, length);
    }
    
    return ESP_OK;
}

bool ws_client_is_audio_ready(void)
{
    return (s_ws_client.state == WS_CLIENT_STATE_CONNECTED && 
            s_ws_client.client != NULL);
}

// Audio Interface Integration Functions

esp_err_t ws_client_set_audio_callback(ws_audio_callback_t audio_callback, void *user_data)
{
    if (!audio_callback) {
        ESP_LOGE(TAG, "Audio callback cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_ws_client.audio_callback = audio_callback;
    s_ws_client.audio_user_data = user_data;
    
    ESP_LOGI(TAG, "Audio interface callback registered for bidirectional streaming");
    return ESP_OK;
}

esp_err_t ws_client_stream_captured_audio(const uint8_t *captured_audio, size_t audio_len)
{
    if (!captured_audio || audio_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_ws_client.state != WS_CLIENT_STATE_CONNECTED) {
        ESP_LOGD(TAG, "WebSocket not connected - dropping audio chunk");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send captured audio as binary data to HowdyTTS server
    int ret = esp_websocket_client_send_bin(s_ws_client.client, 
                                           (const char*)captured_audio, 
                                           audio_len, 
                                           pdMS_TO_TICKS(100));  // Non-blocking with timeout
    
    if (ret < 0) {
        ESP_LOGW(TAG, "Failed to stream captured audio to server");
        return ESP_FAIL;
    }
    
    s_ws_client.bytes_sent += audio_len;
    ESP_LOGD(TAG, "Streamed captured audio: %zu bytes to HowdyTTS server", audio_len);
    
    return ESP_OK;
}