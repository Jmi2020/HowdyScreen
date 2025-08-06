#include "esp32_p4_vad_feedback.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "VAD_Feedback";

// JSON message templates
#define JSON_WAKE_WORD_TEMPLATE \
    "{\"type\":\"wake_word_detection\"," \
    "\"detection_id\":%lu," \
    "\"device_id\":\"%s\"," \
    "\"timestamp\":%llu," \
    "\"confidence\":%.3f," \
    "\"energy_level\":%d," \
    "\"pattern_score\":%d," \
    "\"syllable_count\":%d," \
    "\"duration_ms\":%d," \
    "\"vad_active\":%s," \
    "\"snr_db\":%.1f}"

#define JSON_STATISTICS_TEMPLATE \
    "{\"type\":\"device_statistics\"," \
    "\"device_id\":\"%s\"," \
    "\"timestamp\":%llu," \
    "\"wake_word_stats\":{" \
        "\"total_detections\":%lu," \
        "\"true_positives\":%lu," \
        "\"false_positives\":%lu," \
        "\"avg_confidence\":%.3f," \
        "\"current_threshold\":%d" \
    "}," \
    "\"vad_stats\":{" \
        "\"voice_packets\":%lu," \
        "\"silence_packets\":%lu," \
        "\"avg_confidence\":%.3f" \
    "}}"

#define JSON_PING_TEMPLATE \
    "{\"type\":\"ping\"," \
    "\"device_id\":\"%s\"," \
    "\"timestamp\":%llu}"

/**
 * @brief Internal VAD feedback client structure
 */
typedef struct vad_feedback_client {
    // Configuration
    vad_feedback_config_t config;
    
    // WebSocket client
    esp_websocket_client_handle_t ws_client;
    bool connected;
    uint32_t connection_start_time;
    
    // Event handling
    vad_feedback_event_callback_t event_callback;
    void *callback_user_data;
    
    // Message queue for sending
    QueueHandle_t send_queue;
    TaskHandle_t send_task;
    
    // Statistics
    vad_feedback_stats_t stats;
    
    // Pending validations (for tracking feedback timing)
    struct {
        uint32_t detection_id;
        uint32_t timestamp_ms;
    } pending_validations[10];
    uint8_t pending_count;
    
    // Thread safety
    SemaphoreHandle_t mutex;
    
    // State
    bool initialized;
    bool training_mode;
} vad_feedback_client_t;

// Message queue item
typedef struct {
    char *json_data;
    size_t json_len;
} vad_feedback_message_t;

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data);
static void send_task(void *pvParameters);
static esp_err_t queue_json_message(vad_feedback_client_t *client, const char *json_data);
static void process_server_message(vad_feedback_client_t *client, const char *message);
static void add_pending_validation(vad_feedback_client_t *client, uint32_t detection_id);
static bool remove_pending_validation(vad_feedback_client_t *client, uint32_t detection_id, 
                                     uint32_t *elapsed_ms);

vad_feedback_handle_t vad_feedback_init(const vad_feedback_config_t *config,
                                       vad_feedback_event_callback_t event_callback,
                                       void *user_data)
{
    if (!config || !event_callback) {
        ESP_LOGE(TAG, "Invalid configuration or callback");
        return NULL;
    }
    
    vad_feedback_client_t *client = malloc(sizeof(vad_feedback_client_t));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate memory for VAD feedback client");
        return NULL;
    }
    
    memset(client, 0, sizeof(vad_feedback_client_t));
    
    // Copy configuration
    client->config = *config;
    client->event_callback = event_callback;
    client->callback_user_data = user_data;
    
    // Create mutex
    client->mutex = xSemaphoreCreateMutex();
    if (!client->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(client);
        return NULL;
    }
    
    // Create message queue
    client->send_queue = xQueueCreate(config->message_queue_size, sizeof(vad_feedback_message_t));
    if (!client->send_queue) {
        ESP_LOGE(TAG, "Failed to create message queue");
        vSemaphoreDelete(client->mutex);
        free(client);
        return NULL;
    }
    
    // Configure WebSocket client
    esp_websocket_client_config_t ws_config = {
        .uri = config->server_uri,
        .buffer_size = config->buffer_size,
        .reconnect_timeout_ms = config->reconnect_interval_ms,
        .network_timeout_ms = config->connection_timeout_ms,
        .disable_auto_reconnect = !config->auto_reconnect,
        .keep_alive_idle = config->keepalive_interval_ms / 1000,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
        .task_stack = 6144,  // Increased stack size for JSON processing
    };
    
    client->ws_client = esp_websocket_client_init(&ws_config);
    if (!client->ws_client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        vQueueDelete(client->send_queue);
        vSemaphoreDelete(client->mutex);
        free(client);
        return NULL;
    }
    
    // Register WebSocket event handler
    esp_websocket_register_events(client->ws_client, WEBSOCKET_EVENT_ANY, 
                                  websocket_event_handler, client);
    
    // Create send task
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        send_task,
        "vad_feedback_send",
        4096,
        client,
        5,  // Medium priority
        &client->send_task,
        0   // Core 0 (network core)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create send task");
        esp_websocket_client_destroy(client->ws_client);
        vQueueDelete(client->send_queue);
        vSemaphoreDelete(client->mutex);
        free(client);
        return NULL;
    }
    
    client->initialized = true;
    
    ESP_LOGI(TAG, "VAD feedback client initialized");
    ESP_LOGI(TAG, "Server URI: %s", config->server_uri);
    ESP_LOGI(TAG, "Device: %s (%s)", config->device_name, config->device_id);
    ESP_LOGI(TAG, "Wake word feedback: %s", config->enable_wake_word_feedback ? "enabled" : "disabled");
    ESP_LOGI(TAG, "Threshold adaptation: %s", config->enable_threshold_adaptation ? "enabled" : "disabled");
    
    return (vad_feedback_handle_t)client;
}

esp_err_t vad_feedback_deinit(vad_feedback_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Stop WebSocket connection
    if (client->connected) {
        vad_feedback_disconnect(handle);
    }
    
    // Delete send task
    if (client->send_task) {
        vTaskDelete(client->send_task);
    }
    
    // Clean up message queue
    vad_feedback_message_t msg;
    while (xQueueReceive(client->send_queue, &msg, 0) == pdTRUE) {
        free(msg.json_data);
    }
    vQueueDelete(client->send_queue);
    
    // Destroy WebSocket client
    if (client->ws_client) {
        esp_websocket_client_destroy(client->ws_client);
    }
    
    // Delete mutex
    vSemaphoreDelete(client->mutex);
    
    free(client);
    
    ESP_LOGI(TAG, "VAD feedback client deinitialized");
    return ESP_OK;
}

esp_err_t vad_feedback_connect(vad_feedback_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (client->connected) {
        ESP_LOGW(TAG, "VAD feedback client already connected");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Connecting VAD feedback WebSocket to %s", client->config.server_uri);
    
    esp_err_t ret = esp_websocket_client_start(client->ws_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t vad_feedback_disconnect(vad_feedback_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Disconnecting VAD feedback WebSocket");
    
    esp_err_t ret = esp_websocket_client_stop(client->ws_client);
    client->connected = false;
    
    return ret;
}

esp_err_t vad_feedback_send_wake_word_detection(vad_feedback_handle_t handle,
                                              uint32_t detection_id,
                                              const esp32_p4_wake_word_result_t *wake_word_result,
                                              const enhanced_vad_result_t *vad_result)
{
    if (!handle || !wake_word_result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected || !client->config.enable_wake_word_feedback) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint64_t timestamp = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    // Build JSON message
    char json_buffer[1024];
    int json_len = snprintf(json_buffer, sizeof(json_buffer), JSON_WAKE_WORD_TEMPLATE,
        detection_id,
        client->config.device_id,
        timestamp,
        wake_word_result->confidence_score,
        wake_word_result->energy_level,
        wake_word_result->pattern_match_score,
        wake_word_result->syllable_count,
        wake_word_result->detection_duration_ms,
        wake_word_result->vad_active ? "true" : "false",
        vad_result ? vad_result->snr_db : 0.0f
    );
    
    if (json_len < 0 || json_len >= sizeof(json_buffer)) {
        ESP_LOGE(TAG, "Failed to format wake word JSON message");
        return ESP_FAIL;
    }
    
    // Add to pending validations for timing tracking
    add_pending_validation(client, detection_id);
    
    esp_err_t ret = queue_json_message(client, json_buffer);
    if (ret == ESP_OK) {
        client->stats.messages_sent++;
        client->stats.bytes_transmitted += json_len;
        ESP_LOGD(TAG, "Queued wake word detection for validation (ID: %lu)", detection_id);
    }
    
    return ret;
}

esp_err_t vad_feedback_send_statistics(vad_feedback_handle_t handle,
                                      const esp32_p4_wake_word_stats_t *wake_word_stats,
                                      const enhanced_udp_audio_stats_t *vad_stats)
{
    if (!handle || !wake_word_stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint64_t timestamp = esp_timer_get_time() / 1000;
    
    // Build JSON statistics message
    char json_buffer[1536];
    int json_len = snprintf(json_buffer, sizeof(json_buffer), JSON_STATISTICS_TEMPLATE,
        client->config.device_id,
        timestamp,
        wake_word_stats->total_detections,
        wake_word_stats->true_positives,
        wake_word_stats->false_positives,
        wake_word_stats->average_confidence,
        wake_word_stats->current_energy_threshold,
        vad_stats ? vad_stats->voice_packets_sent : 0,
        vad_stats ? vad_stats->silence_packets_sent : 0,
        vad_stats ? vad_stats->average_vad_confidence : 0.0f
    );
    
    if (json_len < 0 || json_len >= sizeof(json_buffer)) {
        ESP_LOGE(TAG, "Failed to format statistics JSON message");
        return ESP_FAIL;
    }
    
    esp_err_t ret = queue_json_message(client, json_buffer);
    if (ret == ESP_OK) {
        client->stats.messages_sent++;
        client->stats.bytes_transmitted += json_len;
        ESP_LOGD(TAG, "Sent device statistics to server");
    }
    
    return ret;
}

esp_err_t vad_feedback_ping(vad_feedback_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint64_t timestamp = esp_timer_get_time() / 1000;
    
    char json_buffer[256];
    int json_len = snprintf(json_buffer, sizeof(json_buffer), JSON_PING_TEMPLATE,
        client->config.device_id,
        timestamp
    );
    
    if (json_len < 0 || json_len >= sizeof(json_buffer)) {
        ESP_LOGE(TAG, "Failed to format ping JSON message");
        return ESP_FAIL;
    }
    
    esp_err_t ret = queue_json_message(client, json_buffer);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Sent ping to VAD feedback server");
    }
    
    return ret;
}

bool vad_feedback_is_connected(vad_feedback_handle_t handle)
{
    if (!handle) {
        return false;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    return client->connected;
}

esp_err_t vad_feedback_get_stats(vad_feedback_handle_t handle, vad_feedback_stats_t *stats)
{
    if (!handle || !stats) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *stats = client->stats;
        
        // Calculate connection uptime
        if (client->connected && client->connection_start_time > 0) {
            stats->connection_uptime_s = (esp_timer_get_time() / 1000000) - client->connection_start_time;
        }
        
        // Calculate validation accuracy
        uint32_t total_validations = stats->positive_validations + stats->negative_validations;
        if (total_validations > 0) {
            stats->validation_accuracy = (float)stats->positive_validations / total_validations;
        }
        
        xSemaphoreGive(client->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vad_feedback_get_default_config(const char *server_ip,
                                         const char *device_id,
                                         vad_feedback_config_t *config)
{
    if (!server_ip || !device_id || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(vad_feedback_config_t));
    
    // Build WebSocket URI
    snprintf(config->server_uri, sizeof(config->server_uri), 
             "ws://%s:8001/vad_feedback", server_ip);
    config->server_port = 8001;
    config->connection_timeout_ms = 5000;
    
    // Device identification
    strncpy(config->device_id, device_id, sizeof(config->device_id) - 1);
    strncpy(config->device_name, "HowdyScreen", sizeof(config->device_name) - 1);
    strncpy(config->room, "unknown", sizeof(config->room) - 1);
    
    // Feedback settings
    config->enable_wake_word_feedback = true;
    config->enable_threshold_adaptation = true;
    config->enable_training_mode = false;
    config->feedback_timeout_ms = 3000;
    
    // Connection management
    config->auto_reconnect = true;
    config->reconnect_interval_ms = 10000;
    config->max_reconnect_attempts = 5;
    
    // Performance settings
    config->keepalive_interval_ms = 30000;
    config->message_queue_size = 20;
    config->buffer_size = 2048;
    
    return ESP_OK;
}

// WebSocket event handler
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data)
{
    vad_feedback_client_t *client = (vad_feedback_client_t*)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t*)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "VAD feedback WebSocket connected");
            client->connected = true;
            client->connection_start_time = esp_timer_get_time() / 1000000;
            client->stats.reconnections++;
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "VAD feedback WebSocket disconnected");
            client->connected = false;
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) { // Text frame
                char *message = malloc(data->data_len + 1);
                if (message) {
                    memcpy(message, data->data_ptr, data->data_len);
                    message[data->data_len] = '\0';
                    
                    client->stats.messages_received++;
                    client->stats.bytes_received += data->data_len;
                    
                    ESP_LOGD(TAG, "Received VAD feedback message: %s", message);
                    process_server_message(client, message);
                    
                    free(message);
                }
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "VAD feedback WebSocket error");
            client->connected = false;
            break;
            
        default:
            break;
    }
}

// Send task implementation
static void send_task(void *pvParameters)
{
    vad_feedback_client_t *client = (vad_feedback_client_t*)pvParameters;
    vad_feedback_message_t msg;
    
    ESP_LOGI(TAG, "VAD feedback send task started");
    
    while (1) {
        // Wait for messages in queue
        if (xQueueReceive(client->send_queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            if (client->connected && msg.json_data) {
                int sent = esp_websocket_client_send_text(client->ws_client, msg.json_data, 
                                                         strlen(msg.json_data), pdMS_TO_TICKS(1000));
                if (sent > 0) {
                    ESP_LOGV(TAG, "Sent JSON message: %s", msg.json_data);
                } else {
                    ESP_LOGW(TAG, "Failed to send JSON message");
                }
            }
            
            // Free message data
            if (msg.json_data) {
                free(msg.json_data);
            }
        }
        
        // Periodic keepalive ping
        static uint64_t last_ping = 0;
        uint64_t now = esp_timer_get_time() / 1000;
        
        if (client->connected && (now - last_ping) > client->config.keepalive_interval_ms) {
            vad_feedback_ping((vad_feedback_handle_t)client);
            last_ping = now;
        }
    }
    
    ESP_LOGI(TAG, "VAD feedback send task ended");
    vTaskDelete(NULL);
}

// Helper function implementations
static esp_err_t queue_json_message(vad_feedback_client_t *client, const char *json_data)
{
    if (!json_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_message_t msg;
    msg.json_len = strlen(json_data);
    msg.json_data = malloc(msg.json_len + 1);
    
    if (!msg.json_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON message");
        return ESP_ERR_NO_MEM;
    }
    
    strcpy(msg.json_data, json_data);
    
    if (xQueueSend(client->send_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue JSON message - queue full");
        free(msg.json_data);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

static void process_server_message(vad_feedback_client_t *client, const char *message)
{
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON message from server");
        return;
    }
    
    cJSON *type_obj = cJSON_GetObjectItem(json, "type");
    if (!cJSON_IsString(type_obj)) {
        ESP_LOGE(TAG, "Invalid message type in server message");
        cJSON_Delete(json);
        return;
    }
    
    const char *type = cJSON_GetStringValue(type_obj);
    
    if (strcmp(type, "wake_word_validation") == 0) {
        // Handle wake word validation response
        cJSON *detection_id_obj = cJSON_GetObjectItem(json, "detection_id");
        cJSON *validated_obj = cJSON_GetObjectItem(json, "validated");
        cJSON *confidence_obj = cJSON_GetObjectItem(json, "confidence");
        
        if (cJSON_IsNumber(detection_id_obj) && cJSON_IsBool(validated_obj)) {
            uint32_t detection_id = (uint32_t)cJSON_GetNumberValue(detection_id_obj);
            bool validated = cJSON_IsTrue(validated_obj);
            float server_confidence = cJSON_IsNumber(confidence_obj) ? 
                (float)cJSON_GetNumberValue(confidence_obj) : 0.0f;
            
            // Find and remove from pending validations
            uint32_t elapsed_ms = 0;
            bool found = remove_pending_validation(client, detection_id, &elapsed_ms);
            
            if (found) {
                client->stats.average_feedback_time_ms = 
                    (client->stats.average_feedback_time_ms * client->stats.wake_word_validations + 
                     elapsed_ms) / (client->stats.wake_word_validations + 1);
            }
            
            // Update statistics
            client->stats.wake_word_validations++;
            if (validated) {
                client->stats.positive_validations++;
            } else {
                client->stats.negative_validations++;
            }
            
            // Create validation structure
            vad_feedback_wake_word_validation_t validation = {
                .detection_id = detection_id,
                .validated = validated,
                .server_confidence = server_confidence,
                .processing_time_ms = elapsed_ms
            };
            
            // Call event callback
            if (client->event_callback) {
                client->event_callback(VAD_FEEDBACK_WAKE_WORD_VALIDATION, 
                                     &validation, sizeof(validation), 
                                     client->callback_user_data);
            }
            
            ESP_LOGI(TAG, "%s Server %s wake word detection (ID: %lu, confidence: %.3f, time: %lums)", 
                    validated ? "✅" : "❌",
                    validated ? "validated" : "rejected", 
                    detection_id, server_confidence, elapsed_ms);
        }
    } else if (strcmp(type, "threshold_update") == 0) {
        // Handle threshold update recommendation
        cJSON *energy_threshold_obj = cJSON_GetObjectItem(json, "energy_threshold");
        cJSON *confidence_threshold_obj = cJSON_GetObjectItem(json, "confidence_threshold");
        
        if (cJSON_IsNumber(energy_threshold_obj) && cJSON_IsNumber(confidence_threshold_obj)) {
            vad_feedback_threshold_update_t threshold_update = {
                .new_energy_threshold = (uint16_t)cJSON_GetNumberValue(energy_threshold_obj),
                .new_confidence_threshold = (float)cJSON_GetNumberValue(confidence_threshold_obj),
                .urgency = 128  // Default medium urgency
            };
            
            // Get optional fields
            cJSON *reason_obj = cJSON_GetObjectItem(json, "reason");
            if (cJSON_IsString(reason_obj)) {
                strncpy(threshold_update.reason, cJSON_GetStringValue(reason_obj), 
                       sizeof(threshold_update.reason) - 1);
            }
            
            // Call event callback
            if (client->event_callback && client->config.enable_threshold_adaptation) {
                client->event_callback(VAD_FEEDBACK_THRESHOLD_UPDATE, 
                                     &threshold_update, sizeof(threshold_update), 
                                     client->callback_user_data);
            }
            
            client->stats.threshold_updates++;
            ESP_LOGI(TAG, "🔧 Received threshold update: energy=%d, confidence=%.3f", 
                    threshold_update.new_energy_threshold, 
                    threshold_update.new_confidence_threshold);
        }
    } else if (strcmp(type, "pong") == 0) {
        ESP_LOGD(TAG, "Received pong from VAD feedback server");
    } else {
        ESP_LOGD(TAG, "Received unknown message type: %s", type);
    }
    
    cJSON_Delete(json);
}

static void add_pending_validation(vad_feedback_client_t *client, uint32_t detection_id)
{
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (client->pending_count < 10) {
            client->pending_validations[client->pending_count].detection_id = detection_id;
            client->pending_validations[client->pending_count].timestamp_ms = esp_timer_get_time() / 1000;
            client->pending_count++;
        }
        xSemaphoreGive(client->mutex);
    }
}

static bool remove_pending_validation(vad_feedback_client_t *client, uint32_t detection_id, 
                                     uint32_t *elapsed_ms)
{
    bool found = false;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (uint8_t i = 0; i < client->pending_count; i++) {
            if (client->pending_validations[i].detection_id == detection_id) {
                if (elapsed_ms) {
                    *elapsed_ms = (esp_timer_get_time() / 1000) - 
                                 client->pending_validations[i].timestamp_ms;
                }
                
                // Remove from array by shifting
                for (uint8_t j = i; j < client->pending_count - 1; j++) {
                    client->pending_validations[j] = client->pending_validations[j + 1];
                }
                client->pending_count--;
                found = true;
                break;
            }
        }
        xSemaphoreGive(client->mutex);
    }
    
    return found;
}