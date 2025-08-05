#pragma once

#include "esp_err.h"
#include "esp_websocket_client.h"
#include "audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration for audio interface integration
typedef struct audio_interface_status_t audio_interface_status_t;

// WebSocket client state
typedef enum {
    WS_CLIENT_STATE_DISCONNECTED = 0,
    WS_CLIENT_STATE_CONNECTING,
    WS_CLIENT_STATE_CONNECTED,
    WS_CLIENT_STATE_ERROR
} ws_client_state_t;

// WebSocket message types
typedef enum {
    WS_MSG_TYPE_AUDIO_STREAM = 0,
    WS_MSG_TYPE_TTS_RESPONSE,
    WS_MSG_TYPE_CONTROL,
    WS_MSG_TYPE_STATUS
} ws_message_type_t;

// WebSocket client configuration
typedef struct {
    char server_uri[256];
    uint16_t reconnect_timeout_ms;
    uint16_t keepalive_idle_sec;
    uint16_t keepalive_interval_sec;
    uint16_t keepalive_count;
    bool auto_reconnect;
    uint32_t buffer_size;
} ws_client_config_t;

// WebSocket event callback function
typedef void (*ws_event_callback_t)(ws_client_state_t state, ws_message_type_t msg_type, const uint8_t *data, size_t len);

/**
 * @brief Initialize WebSocket client for HowdyTTS communication
 * 
 * @param config Client configuration
 * @param event_callback Event callback function
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_init(const ws_client_config_t *config, ws_event_callback_t event_callback);

/**
 * @brief Start WebSocket client connection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_start(void);

/**
 * @brief Stop WebSocket client connection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_stop(void);

/**
 * @brief Send audio data to HowdyTTS server
 * 
 * @param audio_data Audio buffer
 * @param samples Number of samples
 * @param sample_rate Sample rate in Hz
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_send_audio(const int16_t *audio_data, size_t samples, uint32_t sample_rate);

/**
 * @brief Send binary audio data directly over WebSocket (inspired by Arduino implementation)
 * 
 * @param buffer Audio buffer (int16_t PCM samples)
 * @param samples Number of samples in buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_send_binary_audio(const int16_t *buffer, size_t samples);

/**
 * @brief Handle binary audio response from server
 * 
 * @param data Binary audio data received
 * @param length Length of audio data in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_handle_binary_response(const uint8_t *data, size_t length);

/**
 * @brief Check if WebSocket is ready for audio streaming
 * 
 * @return true if connected and ready
 */
bool ws_client_is_audio_ready(void);

/**
 * @brief Send text message to HowdyTTS server
 * 
 * @param text Text message
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_send_text(const char *text);

/**
 * @brief Get current WebSocket client state
 * 
 * @return ws_client_state_t Current state
 */
ws_client_state_t ws_client_get_state(void);

/**
 * @brief Send ping to server for keepalive
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_ping(void);

/**
 * @brief Get connection statistics
 * 
 * @param bytes_sent Number of bytes sent
 * @param bytes_received Number of bytes received
 * @param reconnect_count Number of reconnections
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_get_stats(uint32_t *bytes_sent, uint32_t *bytes_received, uint32_t *reconnect_count);

/**
 * @brief Set audio interface callback for bidirectional audio streaming
 * This enables the WebSocket client to:
 * - Receive captured audio from the audio interface coordinator
 * - Send TTS audio to the audio interface coordinator for playback
 * 
 * @param audio_callback Function to call when TTS audio is received
 * @param user_data User data for the callback
 * @return esp_err_t ESP_OK on success
 */
typedef esp_err_t (*ws_audio_callback_t)(const uint8_t *tts_audio, size_t audio_len, void *user_data);
esp_err_t ws_client_set_audio_callback(ws_audio_callback_t audio_callback, void *user_data);

/**
 * @brief Stream captured audio to HowdyTTS server (called by audio interface)
 * This function is called by the audio interface coordinator when audio is captured
 * 
 * @param captured_audio PCM audio data from microphone
 * @param audio_len Length of audio data in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_client_stream_captured_audio(const uint8_t *captured_audio, size_t audio_len);

#ifdef __cplusplus
}
#endif