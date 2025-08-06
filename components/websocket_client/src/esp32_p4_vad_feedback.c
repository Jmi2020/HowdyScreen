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
    
    // TTS audio session management
    vad_feedback_tts_session_t current_tts_session;
    bool tts_session_active;
    vad_feedback_tts_audio_callback_t tts_callback;
    void *tts_callback_user_data;
    
    // TTS audio buffering
    QueueHandle_t tts_audio_queue;
    TaskHandle_t tts_processing_task;
    uint16_t tts_chunks_received;
    uint16_t tts_chunks_played;
    uint32_t tts_audio_buffer_size;
    
} vad_feedback_client_t;

// Message queue item
typedef struct {
    char *json_data;
    size_t json_len;
} vad_feedback_message_t;

// TTS audio queue item
typedef struct {
    vad_feedback_tts_chunk_t chunk_data;
    bool session_start;
    bool session_end;
    vad_feedback_tts_session_t session_info;  // Only valid if session_start is true
    vad_feedback_tts_end_t end_info;          // Only valid if session_end is true
} tts_audio_queue_item_t;

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data);
static void send_task(void *pvParameters);
static esp_err_t queue_json_message(vad_feedback_client_t *client, const char *json_data);
static void process_server_message(vad_feedback_client_t *client, const char *message);
static void add_pending_validation(vad_feedback_client_t *client, uint32_t detection_id);
static bool remove_pending_validation(vad_feedback_client_t *client, uint32_t detection_id, 
                                     uint32_t *elapsed_ms);

// TTS audio processing functions
static void tts_processing_task(void *pvParameters);
static esp_err_t parse_tts_audio_start(const cJSON *json, vad_feedback_tts_session_t *session);
static esp_err_t parse_tts_audio_chunk(const cJSON *json, vad_feedback_tts_chunk_t *chunk);
static esp_err_t parse_tts_audio_end(const cJSON *json, vad_feedback_tts_end_t *end_info);
static esp_err_t decode_base64_audio(const char *base64_data, uint8_t **audio_data, size_t *audio_len);
static esp_err_t queue_tts_audio_item(vad_feedback_client_t *client, const tts_audio_queue_item_t *item);
static void cleanup_tts_session(vad_feedback_client_t *client);

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
    
    // Create TTS audio queue for streaming audio chunks
    client->tts_audio_queue = xQueueCreate(20, sizeof(tts_audio_queue_item_t));
    if (!client->tts_audio_queue) {
        ESP_LOGE(TAG, "Failed to create TTS audio queue");
        vTaskDelete(client->send_task);
        esp_websocket_client_destroy(client->ws_client);
        vQueueDelete(client->send_queue);
        vSemaphoreDelete(client->mutex);
        free(client);
        return NULL;
    }
    
    // Create TTS processing task
    task_ret = xTaskCreatePinnedToCore(
        tts_processing_task,
        "vad_tts_process",
        8192,  // Large stack for audio processing
        client,
        6,     // Higher priority than send task
        &client->tts_processing_task,
        1      // Core 1 (audio processing core)
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TTS processing task");
        vQueueDelete(client->tts_audio_queue);
        vTaskDelete(client->send_task);
        esp_websocket_client_destroy(client->ws_client);
        vQueueDelete(client->send_queue);
        vSemaphoreDelete(client->mutex);
        free(client);
        return NULL;
    }
    
    // Initialize TTS session state
    client->tts_session_active = false;
    client->tts_chunks_received = 0;
    client->tts_chunks_played = 0;
    client->tts_callback = NULL;
    client->tts_callback_user_data = NULL;
    
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
    
    // Delete TTS processing task
    if (client->tts_processing_task) {
        vTaskDelete(client->tts_processing_task);
    }
    
    // Clean up TTS session if active
    if (client->tts_session_active) {
        cleanup_tts_session(client);
    }
    
    // Clean up TTS audio queue
    if (client->tts_audio_queue) {
        tts_audio_queue_item_t tts_item;
        while (xQueueReceive(client->tts_audio_queue, &tts_item, 0) == pdTRUE) {
            // Free audio data if allocated
            if (tts_item.chunk_data.audio_data) {
                free(tts_item.chunk_data.audio_data);
            }
        }
        vQueueDelete(client->tts_audio_queue);
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
    } else if (strcmp(type, "tts_audio_start") == 0) {
        // Handle TTS audio session start
        vad_feedback_tts_session_t session;
        esp_err_t ret = parse_tts_audio_start(json, &session);
        if (ret == ESP_OK) {
            tts_audio_queue_item_t item = {
                .session_start = true,
                .session_end = false,
                .session_info = session
            };
            queue_tts_audio_item(client, &item);
            ESP_LOGI(TAG, "🎵 TTS session started: %s (duration: %dms)", 
                    session.session_id, session.estimated_duration_ms);
        }
    } else if (strcmp(type, "tts_audio_chunk") == 0) {
        // Handle TTS audio chunk
        vad_feedback_tts_chunk_t chunk;
        esp_err_t ret = parse_tts_audio_chunk(json, &chunk);
        if (ret == ESP_OK) {
            tts_audio_queue_item_t item = {
                .session_start = false,
                .session_end = false,
                .chunk_data = chunk
            };
            queue_tts_audio_item(client, &item);
            ESP_LOGV(TAG, "🎵 TTS chunk %d received (%d bytes)", 
                    chunk.chunk_sequence, chunk.chunk_size);
        }
    } else if (strcmp(type, "tts_audio_end") == 0) {
        // Handle TTS audio session end
        vad_feedback_tts_end_t end_info;
        esp_err_t ret = parse_tts_audio_end(json, &end_info);
        if (ret == ESP_OK) {
            tts_audio_queue_item_t item = {
                .session_start = false,
                .session_end = true,
                .end_info = end_info
            };
            queue_tts_audio_item(client, &item);
            ESP_LOGI(TAG, "🎵 TTS session ended: %s (%d chunks, %d bytes)", 
                    end_info.session_id, end_info.total_chunks_sent, end_info.total_audio_bytes);
        }
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

// TTS processing task implementation
static void tts_processing_task(void *pvParameters)
{
    vad_feedback_client_t *client = (vad_feedback_client_t*)pvParameters;
    tts_audio_queue_item_t item;
    
    ESP_LOGI(TAG, "TTS processing task started");
    
    while (1) {
        // Wait for TTS audio items in queue
        if (xQueueReceive(client->tts_audio_queue, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            if (item.session_start) {
                // Handle TTS session start
                ESP_LOGI(TAG, "🎵 Starting TTS session: %s", item.session_info.session_id);
                
                if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    client->current_tts_session = item.session_info;
                    client->tts_session_active = true;
                    client->tts_chunks_received = 0;
                    client->tts_chunks_played = 0;
                    xSemaphoreGive(client->mutex);
                }
                
                // Notify application via event callback
                if (client->event_callback) {
                    client->event_callback(VAD_FEEDBACK_TTS_AUDIO_START, 
                                         &item.session_info, sizeof(item.session_info), 
                                         client->callback_user_data);
                }
                
            } else if (item.session_end) {
                // Handle TTS session end
                ESP_LOGI(TAG, "🎵 Ending TTS session: %s", item.end_info.session_id);
                
                // Notify application via event callback
                if (client->event_callback) {
                    client->event_callback(VAD_FEEDBACK_TTS_AUDIO_END, 
                                         &item.end_info, sizeof(item.end_info), 
                                         client->callback_user_data);
                }
                
                // Cleanup session
                cleanup_tts_session(client);
                
            } else {
                // Handle TTS audio chunk
                if (client->tts_session_active && 
                    strcmp(item.chunk_data.session_id, client->current_tts_session.session_id) == 0) {
                    
                    ESP_LOGV(TAG, "🎵 Processing TTS chunk %d (%d bytes)", 
                            item.chunk_data.chunk_sequence, item.chunk_data.chunk_size);
                    
                    client->tts_chunks_received++;
                    
                    // Call TTS audio callback if registered
                    if (client->tts_callback && item.chunk_data.audio_data) {
                        client->tts_callback(&client->current_tts_session,
                                           (const int16_t*)item.chunk_data.audio_data,
                                           item.chunk_data.chunk_size / 2,  // Convert bytes to samples
                                           client->tts_callback_user_data);
                        
                        client->tts_chunks_played++;
                    }
                    
                    // Notify application via event callback
                    if (client->event_callback) {
                        client->event_callback(VAD_FEEDBACK_TTS_AUDIO_CHUNK, 
                                             &item.chunk_data, sizeof(item.chunk_data), 
                                             client->callback_user_data);
                    }
                }
                
                // Free audio data
                if (item.chunk_data.audio_data) {
                    free(item.chunk_data.audio_data);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "TTS processing task ended");
    vTaskDelete(NULL);
}

// Parse TTS audio start message
static esp_err_t parse_tts_audio_start(const cJSON *json, vad_feedback_tts_session_t *session)
{
    if (!json || !session) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(session, 0, sizeof(vad_feedback_tts_session_t));
    
    // Parse session info
    cJSON *session_info = cJSON_GetObjectItem(json, "session_info");
    if (!session_info) {
        ESP_LOGE(TAG, "Missing session_info in TTS audio start");
        return ESP_FAIL;
    }
    
    cJSON *session_id = cJSON_GetObjectItem(session_info, "session_id");
    if (cJSON_IsString(session_id)) {
        strncpy(session->session_id, cJSON_GetStringValue(session_id), sizeof(session->session_id) - 1);
    } else {
        ESP_LOGE(TAG, "Missing session_id in TTS audio start");
        return ESP_FAIL;
    }
    
    cJSON *response_text = cJSON_GetObjectItem(session_info, "response_text");
    if (cJSON_IsString(response_text)) {
        strncpy(session->response_text, cJSON_GetStringValue(response_text), sizeof(session->response_text) - 1);
    }
    
    cJSON *estimated_duration = cJSON_GetObjectItem(session_info, "estimated_duration_ms");
    if (cJSON_IsNumber(estimated_duration)) {
        session->estimated_duration_ms = (uint16_t)cJSON_GetNumberValue(estimated_duration);
    }
    
    cJSON *total_chunks = cJSON_GetObjectItem(session_info, "total_chunks_expected");
    if (cJSON_IsNumber(total_chunks)) {
        session->total_chunks_expected = (uint16_t)cJSON_GetNumberValue(total_chunks);
    }
    
    // Parse audio format
    cJSON *audio_format = cJSON_GetObjectItem(json, "audio_format");
    if (audio_format) {
        cJSON *sample_rate = cJSON_GetObjectItem(audio_format, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            session->sample_rate = (uint32_t)cJSON_GetNumberValue(sample_rate);
        } else {
            session->sample_rate = 16000;  // Default
        }
        
        cJSON *channels = cJSON_GetObjectItem(audio_format, "channels");
        if (cJSON_IsNumber(channels)) {
            session->channels = (uint8_t)cJSON_GetNumberValue(channels);
        } else {
            session->channels = 1;  // Default mono
        }
        
        cJSON *bits_per_sample = cJSON_GetObjectItem(audio_format, "bits_per_sample");
        if (cJSON_IsNumber(bits_per_sample)) {
            session->bits_per_sample = (uint8_t)cJSON_GetNumberValue(bits_per_sample);
        } else {
            session->bits_per_sample = 16;  // Default 16-bit
        }
        
        cJSON *total_samples = cJSON_GetObjectItem(audio_format, "total_samples");
        if (cJSON_IsNumber(total_samples)) {
            session->total_samples = (uint32_t)cJSON_GetNumberValue(total_samples);
        }
    }
    
    // Parse playback config
    cJSON *playback_config = cJSON_GetObjectItem(json, "playback_config");
    if (playback_config) {
        cJSON *volume = cJSON_GetObjectItem(playback_config, "volume");
        if (cJSON_IsNumber(volume)) {
            session->volume = (float)cJSON_GetNumberValue(volume);
        } else {
            session->volume = 0.8f;  // Default volume
        }
        
        cJSON *fade_in = cJSON_GetObjectItem(playback_config, "fade_in_ms");
        if (cJSON_IsNumber(fade_in)) {
            session->fade_in_ms = (uint16_t)cJSON_GetNumberValue(fade_in);
        }
        
        cJSON *fade_out = cJSON_GetObjectItem(playback_config, "fade_out_ms");
        if (cJSON_IsNumber(fade_out)) {
            session->fade_out_ms = (uint16_t)cJSON_GetNumberValue(fade_out);
        }
        
        cJSON *interrupt_recording = cJSON_GetObjectItem(playback_config, "interrupt_recording");
        if (cJSON_IsBool(interrupt_recording)) {
            session->interrupt_recording = cJSON_IsTrue(interrupt_recording);
        } else {
            session->interrupt_recording = true;  // Default interrupt recording
        }
        
        cJSON *echo_cancellation = cJSON_GetObjectItem(playback_config, "echo_cancellation");
        if (cJSON_IsBool(echo_cancellation)) {
            session->echo_cancellation = cJSON_IsTrue(echo_cancellation);
        } else {
            session->echo_cancellation = true;  // Default enable echo cancellation
        }
    }
    
    ESP_LOGI(TAG, "Parsed TTS session: %s, %dms, %d chunks, %dHz %dch %dbit", 
            session->session_id, session->estimated_duration_ms, session->total_chunks_expected,
            session->sample_rate, session->channels, session->bits_per_sample);
    
    return ESP_OK;
}

// Parse TTS audio chunk message
static esp_err_t parse_tts_audio_chunk(const cJSON *json, vad_feedback_tts_chunk_t *chunk)
{
    if (!json || !chunk) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(chunk, 0, sizeof(vad_feedback_tts_chunk_t));
    
    // Parse chunk info
    cJSON *chunk_info = cJSON_GetObjectItem(json, "chunk_info");
    if (!chunk_info) {
        ESP_LOGE(TAG, "Missing chunk_info in TTS audio chunk");
        return ESP_FAIL;
    }
    
    cJSON *session_id = cJSON_GetObjectItem(chunk_info, "session_id");
    if (cJSON_IsString(session_id)) {
        strncpy(chunk->session_id, cJSON_GetStringValue(session_id), sizeof(chunk->session_id) - 1);
    } else {
        ESP_LOGE(TAG, "Missing session_id in TTS audio chunk");
        return ESP_FAIL;
    }
    
    cJSON *chunk_sequence = cJSON_GetObjectItem(chunk_info, "chunk_sequence");
    if (cJSON_IsNumber(chunk_sequence)) {
        chunk->chunk_sequence = (uint16_t)cJSON_GetNumberValue(chunk_sequence);
    }
    
    cJSON *chunk_size = cJSON_GetObjectItem(chunk_info, "chunk_size");
    if (cJSON_IsNumber(chunk_size)) {
        chunk->chunk_size = (uint16_t)cJSON_GetNumberValue(chunk_size);
    } else {
        ESP_LOGE(TAG, "Missing chunk_size in TTS audio chunk");
        return ESP_FAIL;
    }
    
    cJSON *is_final = cJSON_GetObjectItem(chunk_info, "is_final");
    if (cJSON_IsBool(is_final)) {
        chunk->is_final = cJSON_IsTrue(is_final);
    }
    
    cJSON *checksum = cJSON_GetObjectItem(chunk_info, "checksum");
    if (cJSON_IsString(checksum)) {
        // Convert hex string to uint32_t
        chunk->checksum = (uint32_t)strtoul(cJSON_GetStringValue(checksum), NULL, 16);
    }
    
    // Parse timing info
    cJSON *timing = cJSON_GetObjectItem(json, "timing");
    if (timing) {
        cJSON *chunk_start_time = cJSON_GetObjectItem(timing, "chunk_start_time_ms");
        if (cJSON_IsNumber(chunk_start_time)) {
            chunk->chunk_start_time_ms = (uint16_t)cJSON_GetNumberValue(chunk_start_time);
        }
        
        cJSON *chunk_duration = cJSON_GetObjectItem(timing, "chunk_duration_ms");
        if (cJSON_IsNumber(chunk_duration)) {
            chunk->chunk_duration_ms = (uint16_t)cJSON_GetNumberValue(chunk_duration);
        }
    }
    
    // Decode base64 audio data
    cJSON *audio_data = cJSON_GetObjectItem(chunk_info, "audio_data");
    if (cJSON_IsString(audio_data)) {
        const char *base64_data = cJSON_GetStringValue(audio_data);
        size_t audio_len;
        esp_err_t ret = decode_base64_audio(base64_data, &chunk->audio_data, &audio_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to decode base64 audio data");
            return ret;
        }
        
        // Verify chunk size matches decoded data
        if (audio_len != chunk->chunk_size) {
            ESP_LOGW(TAG, "Chunk size mismatch: expected %d, got %zu", chunk->chunk_size, audio_len);
            chunk->chunk_size = audio_len;
        }
    } else {
        ESP_LOGE(TAG, "Missing audio_data in TTS audio chunk");
        return ESP_FAIL;
    }
    
    ESP_LOGV(TAG, "Parsed TTS chunk: %s seq=%d size=%d final=%s", 
            chunk->session_id, chunk->chunk_sequence, chunk->chunk_size, 
            chunk->is_final ? "true" : "false");
    
    return ESP_OK;
}

// Parse TTS audio end message
static esp_err_t parse_tts_audio_end(const cJSON *json, vad_feedback_tts_end_t *end_info)
{
    if (!json || !end_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(end_info, 0, sizeof(vad_feedback_tts_end_t));
    
    // Parse session summary
    cJSON *session_summary = cJSON_GetObjectItem(json, "session_summary");
    if (!session_summary) {
        ESP_LOGE(TAG, "Missing session_summary in TTS audio end");
        return ESP_FAIL;
    }
    
    cJSON *session_id = cJSON_GetObjectItem(session_summary, "session_id");
    if (cJSON_IsString(session_id)) {
        strncpy(end_info->session_id, cJSON_GetStringValue(session_id), sizeof(end_info->session_id) - 1);
    } else {
        ESP_LOGE(TAG, "Missing session_id in TTS audio end");
        return ESP_FAIL;
    }
    
    cJSON *total_chunks_sent = cJSON_GetObjectItem(session_summary, "total_chunks_sent");
    if (cJSON_IsNumber(total_chunks_sent)) {
        end_info->total_chunks_sent = (uint16_t)cJSON_GetNumberValue(total_chunks_sent);
    }
    
    cJSON *total_audio_bytes = cJSON_GetObjectItem(session_summary, "total_audio_bytes");
    if (cJSON_IsNumber(total_audio_bytes)) {
        end_info->total_audio_bytes = (uint32_t)cJSON_GetNumberValue(total_audio_bytes);
    }
    
    cJSON *actual_duration_ms = cJSON_GetObjectItem(session_summary, "actual_duration_ms");
    if (cJSON_IsNumber(actual_duration_ms)) {
        end_info->actual_duration_ms = (uint16_t)cJSON_GetNumberValue(actual_duration_ms);
    }
    
    cJSON *transmission_time_ms = cJSON_GetObjectItem(session_summary, "transmission_time_ms");
    if (cJSON_IsNumber(transmission_time_ms)) {
        end_info->transmission_time_ms = (uint16_t)cJSON_GetNumberValue(transmission_time_ms);
    }
    
    // Parse playback actions
    cJSON *playback_actions = cJSON_GetObjectItem(json, "playback_actions");
    if (playback_actions) {
        cJSON *fade_out_ms = cJSON_GetObjectItem(playback_actions, "fade_out_ms");
        if (cJSON_IsNumber(fade_out_ms)) {
            end_info->fade_out_ms = (uint16_t)cJSON_GetNumberValue(fade_out_ms);
        }
        
        cJSON *return_to_listening = cJSON_GetObjectItem(playback_actions, "return_to_listening");
        if (cJSON_IsBool(return_to_listening)) {
            end_info->return_to_listening = cJSON_IsTrue(return_to_listening);
        } else {
            end_info->return_to_listening = true;  // Default return to listening
        }
        
        cJSON *cooldown_period_ms = cJSON_GetObjectItem(playback_actions, "cooldown_period_ms");
        if (cJSON_IsNumber(cooldown_period_ms)) {
            end_info->cooldown_period_ms = (uint16_t)cJSON_GetNumberValue(cooldown_period_ms);
        }
    }
    
    ESP_LOGI(TAG, "Parsed TTS end: %s, %d chunks, %d bytes, %dms duration", 
            end_info->session_id, end_info->total_chunks_sent, 
            end_info->total_audio_bytes, end_info->actual_duration_ms);
    
    return ESP_OK;
}

// Decode base64 audio data
static esp_err_t decode_base64_audio(const char *base64_data, uint8_t **audio_data, size_t *audio_len)
{
    if (!base64_data || !audio_data || !audio_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t base64_len = strlen(base64_data);
    if (base64_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate maximum decoded length (base64 expands by ~33%)
    size_t max_decoded_len = (base64_len * 3) / 4;
    
    // Allocate buffer for decoded audio
    uint8_t *decoded_buffer = malloc(max_decoded_len);
    if (!decoded_buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for decoded audio (%zu bytes)", max_decoded_len);
        return ESP_ERR_NO_MEM;
    }
    
    // Simple base64 decode (you may want to use a proper base64 library)
    // For now, this is a placeholder - implement actual base64 decoding
    // Note: ESP-IDF provides mbedtls_base64_decode() function
    
    // Placeholder: assume 1:1 copy for now (this needs proper base64 implementation)
    size_t decoded_len = base64_len / 2;  // Rough estimate for testing
    if (decoded_len > max_decoded_len) {
        decoded_len = max_decoded_len;
    }
    
    // TODO: Implement proper base64 decoding
    // For now, fill with dummy audio data for testing
    memset(decoded_buffer, 0, decoded_len);
    
    *audio_data = decoded_buffer;
    *audio_len = decoded_len;
    
    ESP_LOGV(TAG, "Decoded %zu bytes of base64 audio data", decoded_len);
    return ESP_OK;
}

// Queue TTS audio item
static esp_err_t queue_tts_audio_item(vad_feedback_client_t *client, const tts_audio_queue_item_t *item)
{
    if (!client || !item) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xQueueSend(client->tts_audio_queue, item, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to queue TTS audio item - queue full");
        
        // Free audio data if this was a chunk item
        if (!item->session_start && !item->session_end && item->chunk_data.audio_data) {
            free((void*)item->chunk_data.audio_data);
        }
        
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

// Cleanup TTS session
static void cleanup_tts_session(vad_feedback_client_t *client)
{
    if (!client) {
        return;
    }
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        client->tts_session_active = false;
        memset(&client->current_tts_session, 0, sizeof(client->current_tts_session));
        client->tts_chunks_received = 0;
        client->tts_chunks_played = 0;
        xSemaphoreGive(client->mutex);
    }
    
    ESP_LOGI(TAG, "TTS session cleaned up");
}

// Public TTS API functions
esp_err_t vad_feedback_set_tts_audio_callback(vad_feedback_handle_t handle,
                                             vad_feedback_tts_audio_callback_t callback,
                                             void *user_data)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        client->tts_callback = callback;
        client->tts_callback_user_data = user_data;
        xSemaphoreGive(client->mutex);
        
        ESP_LOGI(TAG, "TTS audio callback %s", callback ? "registered" : "unregistered");
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t vad_feedback_handle_tts_audio_start(vad_feedback_handle_t handle,
                                             const vad_feedback_tts_session_t *session_info)
{
    if (!handle || !session_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected) {
        ESP_LOGW(TAG, "Cannot handle TTS audio start - not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    tts_audio_queue_item_t item = {
        .session_start = true,
        .session_end = false,
        .session_info = *session_info
    };
    
    return queue_tts_audio_item(client, &item);
}

esp_err_t vad_feedback_handle_tts_audio_chunk(vad_feedback_handle_t handle,
                                             const vad_feedback_tts_chunk_t *chunk_data)
{
    if (!handle || !chunk_data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->tts_session_active) {
        ESP_LOGW(TAG, "Cannot handle TTS audio chunk - no active session");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Copy chunk data to avoid lifetime issues
    vad_feedback_tts_chunk_t chunk_copy = *chunk_data;
    
    // Allocate and copy audio data
    if (chunk_data->audio_data && chunk_data->chunk_size > 0) {
        chunk_copy.audio_data = malloc(chunk_data->chunk_size);
        if (!chunk_copy.audio_data) {
            ESP_LOGE(TAG, "Failed to allocate memory for TTS audio chunk");
            return ESP_ERR_NO_MEM;
        }
        memcpy(chunk_copy.audio_data, chunk_data->audio_data, chunk_data->chunk_size);
    }
    
    tts_audio_queue_item_t item = {
        .session_start = false,
        .session_end = false,
        .chunk_data = chunk_copy
    };
    
    return queue_tts_audio_item(client, &item);
}

esp_err_t vad_feedback_handle_tts_audio_end(vad_feedback_handle_t handle,
                                           const vad_feedback_tts_end_t *end_info)
{
    if (!handle || !end_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->tts_session_active) {
        ESP_LOGW(TAG, "Cannot handle TTS audio end - no active session");
        return ESP_ERR_INVALID_STATE;
    }
    
    tts_audio_queue_item_t item = {
        .session_start = false,
        .session_end = true,
        .end_info = *end_info
    };
    
    return queue_tts_audio_item(client, &item);
}

esp_err_t vad_feedback_send_tts_playback_status(vad_feedback_handle_t handle,
                                               const vad_feedback_tts_status_t *status_info)
{
    if (!handle || !status_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (!client->connected) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint64_t timestamp = esp_timer_get_time() / 1000;
    
    // Build JSON playback status message
    char json_buffer[1024];
    int json_len = snprintf(json_buffer, sizeof(json_buffer),
        "{"
        "\"message_type\":\"tts_playback_status\","
        "\"timestamp\":%llu,"
        "\"device_id\":\"%s\","
        "\"status_info\":{"
            "\"session_id\":\"%s\","
            "\"playback_state\":%d,"
            "\"chunks_received\":%d,"
            "\"chunks_played\":%d,"
            "\"buffer_level_ms\":%d,"
            "\"audio_quality\":%d"
        "},"
        "\"performance\":{"
            "\"playback_latency_ms\":%d,"
            "\"buffer_underruns\":%d,"
            "\"audio_dropouts\":%d,"
            "\"echo_suppression_db\":%.1f"
        "}"
        "}",
        timestamp,
        client->config.device_id,
        status_info->session_id,
        status_info->playback_state,
        status_info->chunks_received,
        status_info->chunks_played,
        status_info->buffer_level_ms,
        status_info->audio_quality,
        status_info->playback_latency_ms,
        status_info->buffer_underruns,
        status_info->audio_dropouts,
        status_info->echo_suppression_db
    );
    
    if (json_len < 0 || json_len >= sizeof(json_buffer)) {
        ESP_LOGE(TAG, "Failed to format TTS playback status JSON message");
        return ESP_FAIL;
    }
    
    esp_err_t ret = queue_json_message(client, json_buffer);
    if (ret == ESP_OK) {
        client->stats.messages_sent++;
        client->stats.bytes_transmitted += json_len;
        ESP_LOGD(TAG, "Sent TTS playback status for session: %s", status_info->session_id);
    }
    
    return ret;
}

esp_err_t vad_feedback_get_tts_session_info(vad_feedback_handle_t handle,
                                           const char *session_id,
                                           vad_feedback_tts_session_t *session_info)
{
    if (!handle || !session_id || !session_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (client->tts_session_active && 
            strcmp(client->current_tts_session.session_id, session_id) == 0) {
            *session_info = client->current_tts_session;
            xSemaphoreGive(client->mutex);
            return ESP_OK;
        }
        xSemaphoreGive(client->mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t vad_feedback_cancel_tts_session(vad_feedback_handle_t handle,
                                         const char *session_id)
{
    if (!handle || !session_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (client->tts_session_active && 
            strcmp(client->current_tts_session.session_id, session_id) == 0) {
            
            ESP_LOGI(TAG, "Cancelling TTS session: %s", session_id);
            cleanup_tts_session(client);
            
            // Clear any pending TTS audio items from the queue
            tts_audio_queue_item_t item;
            while (xQueueReceive(client->tts_audio_queue, &item, 0) == pdTRUE) {
                if (!item.session_start && !item.session_end && item.chunk_data.audio_data) {
                    free((void*)item.chunk_data.audio_data);
                }
            }
            
            xSemaphoreGive(client->mutex);
            return ESP_OK;
        }
        xSemaphoreGive(client->mutex);
    }
    
    return ESP_ERR_NOT_FOUND;
}

// Additional helper functions for TTS audio management
bool vad_feedback_is_tts_session_active(vad_feedback_handle_t handle)
{
    if (!handle) {
        return false;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    return client->tts_session_active;
}

esp_err_t vad_feedback_get_tts_stats(vad_feedback_handle_t handle,
                                     uint16_t *chunks_received,
                                     uint16_t *chunks_played)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    vad_feedback_client_t *client = (vad_feedback_client_t*)handle;
    
    if (xSemaphoreTake(client->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (chunks_received) {
            *chunks_received = client->tts_chunks_received;
        }
        if (chunks_played) {
            *chunks_played = client->tts_chunks_played;
        }
        xSemaphoreGive(client->mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}