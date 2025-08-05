#include "howdytts_protocol.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "HowdyTTSProtocol";

// Protocol state
static struct {
    howdytts_session_config_t session_config;
    bool initialized;
    uint32_t message_counter;
    
    // Statistics
    uint32_t messages_sent;
    uint32_t audio_frames_sent;
    uint32_t bytes_compressed;
    
} s_protocol = {0};

esp_err_t howdytts_protocol_init(const howdytts_session_config_t *session_config)
{
    if (!session_config) {
        ESP_LOGE(TAG, "Invalid session configuration");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing HowdyTTS protocol");
    ESP_LOGI(TAG, "Session ID: %s", session_config->session_id);
    ESP_LOGI(TAG, "Device ID: %s", session_config->device_id);
    ESP_LOGI(TAG, "Audio: %lu Hz, %d ch, %d bit", 
             session_config->audio_config.sample_rate,
             session_config->audio_config.channels, 
             session_config->audio_config.bits_per_sample);

    s_protocol.session_config = *session_config;
    s_protocol.initialized = true;
    s_protocol.message_counter = 0;
    
    // Reset statistics
    s_protocol.messages_sent = 0;
    s_protocol.audio_frames_sent = 0;
    s_protocol.bytes_compressed = 0;

    ESP_LOGI(TAG, "HowdyTTS protocol initialized successfully");
    return ESP_OK;
}

esp_err_t howdytts_create_session_start_message(char *message_buffer, size_t buffer_size)
{
    if (!s_protocol.initialized || !message_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON *event = cJSON_CreateString("session_start");
    cJSON *session_id = cJSON_CreateString(s_protocol.session_config.session_id);
    cJSON *device_id = cJSON_CreateString(s_protocol.session_config.device_id);
    cJSON *timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);  // ms since boot

    // Audio configuration
    cJSON *audio_config = cJSON_CreateObject();
    cJSON *sample_rate = cJSON_CreateNumber(s_protocol.session_config.audio_config.sample_rate);
    cJSON *channels = cJSON_CreateNumber(s_protocol.session_config.audio_config.channels);
    cJSON *bits_per_sample = cJSON_CreateNumber(s_protocol.session_config.audio_config.bits_per_sample);
    cJSON *use_compression = cJSON_CreateBool(s_protocol.session_config.audio_config.use_compression);

    if (!event || !session_id || !device_id || !timestamp || !audio_config || 
        !sample_rate || !channels || !bits_per_sample || !use_compression) {
        ESP_LOGE(TAG, "Failed to create JSON elements");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Build audio config object
    cJSON_AddItemToObject(audio_config, "sample_rate", sample_rate);
    cJSON_AddItemToObject(audio_config, "channels", channels);
    cJSON_AddItemToObject(audio_config, "bits_per_sample", bits_per_sample);
    cJSON_AddItemToObject(audio_config, "use_compression", use_compression);

    // Build root message
    cJSON_AddItemToObject(root, "event", event);
    cJSON_AddItemToObject(root, "session_id", session_id);
    cJSON_AddItemToObject(root, "device_id", device_id);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "audio_config", audio_config);

    char *json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "Message buffer too small: need %zu, have %zu", json_len, buffer_size);
        free(json_string);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(message_buffer, json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    s_protocol.messages_sent++;
    s_protocol.message_counter++;
    
    ESP_LOGI(TAG, "Session start message created");
    return ESP_OK;
}

esp_err_t howdytts_create_audio_message(const int16_t *audio_data, size_t samples, 
                                       char *message_buffer, size_t buffer_size)
{
    if (!s_protocol.initialized || !audio_data || samples == 0 || !message_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    // Calculate base64 encoded size
    size_t audio_bytes = samples * sizeof(int16_t);
    size_t encoded_len = 0;
    
    // Get required buffer size
    mbedtls_base64_encode(NULL, 0, &encoded_len, (const unsigned char*)audio_data, audio_bytes);
    
    char *encoded_audio = malloc(encoded_len + 1);
    if (!encoded_audio) {
        ESP_LOGE(TAG, "Failed to allocate base64 buffer");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Encode audio data
    size_t actual_len = 0;
    int ret = mbedtls_base64_encode((unsigned char*)encoded_audio, encoded_len + 1, &actual_len,
                                   (const unsigned char*)audio_data, audio_bytes);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 encoding failed: %d", ret);
        free(encoded_audio);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Create JSON message
    cJSON *event = cJSON_CreateString("audio_stream");
    cJSON *session_id = cJSON_CreateString(s_protocol.session_config.session_id);
    cJSON *timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON *sequence = cJSON_CreateNumber(s_protocol.message_counter);

    // Media object
    cJSON *media = cJSON_CreateObject();
    cJSON *track = cJSON_CreateString("inbound");
    cJSON *payload = cJSON_CreateString(encoded_audio);
    cJSON *sample_count = cJSON_CreateNumber(samples);
    cJSON *sample_rate = cJSON_CreateNumber(s_protocol.session_config.audio_config.sample_rate);

    if (!event || !session_id || !timestamp || !sequence || !media || 
        !track || !payload || !sample_count || !sample_rate) {
        ESP_LOGE(TAG, "Failed to create JSON elements");
        free(encoded_audio);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    // Build media object
    cJSON_AddItemToObject(media, "track", track);
    cJSON_AddItemToObject(media, "payload", payload);
    cJSON_AddItemToObject(media, "samples", sample_count);
    cJSON_AddItemToObject(media, "sample_rate", sample_rate);

    // Build root message
    cJSON_AddItemToObject(root, "event", event);
    cJSON_AddItemToObject(root, "session_id", session_id);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "sequence", sequence);
    cJSON_AddItemToObject(root, "media", media);

    char *json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        free(encoded_audio);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "Message buffer too small: need %zu, have %zu", json_len, buffer_size);
        free(json_string);
        free(encoded_audio);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(message_buffer, json_string);
    
    free(json_string);
    free(encoded_audio);
    cJSON_Delete(root);
    
    s_protocol.messages_sent++;
    s_protocol.audio_frames_sent++;
    s_protocol.message_counter++;
    
    ESP_LOGD(TAG, "Audio message created: %zu samples", samples);
    return ESP_OK;
}

esp_err_t howdytts_create_voice_message(bool voice_detected, float confidence,
                                       char *message_buffer, size_t buffer_size)
{
    if (!s_protocol.initialized || !message_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    const char *event_type = voice_detected ? "voice_detected" : "voice_ended";
    
    cJSON *event = cJSON_CreateString(event_type);
    cJSON *session_id = cJSON_CreateString(s_protocol.session_config.session_id);
    cJSON *timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);
    cJSON *conf = cJSON_CreateNumber(confidence);

    if (!event || !session_id || !timestamp || !conf) {
        ESP_LOGE(TAG, "Failed to create JSON elements");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(root, "event", event);
    cJSON_AddItemToObject(root, "session_id", session_id);
    cJSON_AddItemToObject(root, "timestamp", timestamp);
    cJSON_AddItemToObject(root, "confidence", conf);

    char *json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "Message buffer too small: need %zu, have %zu", json_len, buffer_size);
        free(json_string);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(message_buffer, json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    s_protocol.messages_sent++;
    s_protocol.message_counter++;
    
    ESP_LOGI(TAG, "Voice message created: %s (%.2f)", event_type, confidence);
    return ESP_OK;
}

esp_err_t howdytts_create_ping_message(char *message_buffer, size_t buffer_size)
{
    if (!s_protocol.initialized || !message_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON *event = cJSON_CreateString("ping");
    cJSON *session_id = cJSON_CreateString(s_protocol.session_config.session_id);
    cJSON *timestamp = cJSON_CreateNumber(esp_timer_get_time() / 1000);

    if (!event || !session_id || !timestamp) {
        ESP_LOGE(TAG, "Failed to create JSON elements");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddItemToObject(root, "event", event);
    cJSON_AddItemToObject(root, "session_id", session_id);
    cJSON_AddItemToObject(root, "timestamp", timestamp);

    char *json_string = cJSON_Print(root);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to generate JSON string");
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "Message buffer too small: need %zu, have %zu", json_len, buffer_size);
        free(json_string);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }

    strcpy(message_buffer, json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    s_protocol.messages_sent++;
    
    return ESP_OK;
}

esp_err_t howdytts_parse_tts_response(const char *json_message, int16_t *audio_data, 
                                     size_t max_samples, size_t *samples_decoded)
{
    if (!s_protocol.initialized || !json_message || !audio_data || !samples_decoded) {
        return ESP_ERR_INVALID_ARG;
    }

    *samples_decoded = 0;

    cJSON *root = cJSON_Parse(json_message);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON message");
        return ESP_FAIL;
    }

    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (!event || !cJSON_IsString(event)) {
        ESP_LOGE(TAG, "Invalid or missing event field");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Check if this is a TTS response
    if (strcmp(cJSON_GetStringValue(event), "tts_response") != 0) {
        ESP_LOGD(TAG, "Not a TTS response: %s", cJSON_GetStringValue(event));
        cJSON_Delete(root);
        return ESP_OK;  // Not an error, just not what we're looking for
    }

    cJSON *media = cJSON_GetObjectItem(root, "media");
    if (!media) {
        ESP_LOGE(TAG, "Missing media object");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *payload = cJSON_GetObjectItem(media, "payload");
    if (!payload || !cJSON_IsString(payload)) {
        ESP_LOGE(TAG, "Invalid or missing payload");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    const char *encoded_audio = cJSON_GetStringValue(payload);
    size_t encoded_len = strlen(encoded_audio);

    // Decode base64 audio
    size_t decoded_len = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &decoded_len, 
                                   (const unsigned char*)encoded_audio, encoded_len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "Base64 decode size calculation failed: %d", ret);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    size_t max_decoded_bytes = max_samples * sizeof(int16_t);
    if (decoded_len > max_decoded_bytes) {
        ESP_LOGW(TAG, "TTS audio too large: %zu bytes (max %zu)", decoded_len, max_decoded_bytes);
        decoded_len = max_decoded_bytes;
    }

    size_t actual_decoded = 0;
    ret = mbedtls_base64_decode((unsigned char*)audio_data, decoded_len, &actual_decoded,
                               (const unsigned char*)encoded_audio, encoded_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d", ret);
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    *samples_decoded = actual_decoded / sizeof(int16_t);
    
    ESP_LOGI(TAG, "TTS response decoded: %zu samples", *samples_decoded);
    
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t howdytts_batch_audio_frames(const int16_t **audio_frames, const size_t *frame_sizes,
                                     size_t num_frames, char *message_buffer, size_t buffer_size)
{
    if (!s_protocol.initialized || !audio_frames || !frame_sizes || num_frames == 0 || 
        !message_buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate total samples
    size_t total_samples = 0;
    for (size_t i = 0; i < num_frames; i++) {
        total_samples += frame_sizes[i];
    }

    // Concatenate audio frames
    int16_t *batched_audio = malloc(total_samples * sizeof(int16_t));
    if (!batched_audio) {
        ESP_LOGE(TAG, "Failed to allocate batched audio buffer");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    for (size_t i = 0; i < num_frames; i++) {
        memcpy(batched_audio + offset, audio_frames[i], frame_sizes[i] * sizeof(int16_t));
        offset += frame_sizes[i];
    }

    // Create single audio message with batched data
    esp_err_t ret = howdytts_create_audio_message(batched_audio, total_samples, 
                                                 message_buffer, buffer_size);
    
    free(batched_audio);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Batched %zu frames into single message (%zu samples)", num_frames, total_samples);
    }
    
    return ret;
}

const char* howdytts_get_session_id(void)
{
    return s_protocol.initialized ? s_protocol.session_config.session_id : NULL;
}

esp_err_t howdytts_get_stats(uint32_t *messages_sent, uint32_t *audio_frames_sent, uint32_t *bytes_compressed)
{
    if (!messages_sent || !audio_frames_sent || !bytes_compressed) {
        return ESP_ERR_INVALID_ARG;
    }

    *messages_sent = s_protocol.messages_sent;
    *audio_frames_sent = s_protocol.audio_frames_sent;
    *bytes_compressed = s_protocol.bytes_compressed;

    return ESP_OK;
}