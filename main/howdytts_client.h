#pragma once

#include "esp_err.h"
#include "websocket_client.h"
#include "service_discovery.h"
#include "voice_assistant_ui.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// HowdyTTS client state
typedef enum {
    HOWDYTTS_STATE_DISCONNECTED = 0,
    HOWDYTTS_STATE_DISCOVERING,
    HOWDYTTS_STATE_CONNECTING,
    HOWDYTTS_STATE_CONNECTED,
    HOWDYTTS_STATE_ERROR
} howdytts_client_state_t;

// HowdyTTS client configuration
typedef struct {
    bool auto_discover;
    char server_uri[256];
    uint32_t discovery_timeout_ms;
    uint32_t connection_timeout_ms;
    bool enable_audio_streaming;
} howdytts_client_config_t;

// HowdyTTS client statistics
typedef struct {
    uint32_t connection_attempts;
    uint32_t successful_connections;
    uint32_t audio_messages_sent;
    uint32_t tts_responses_received;
    uint32_t current_latency_ms;
    uint32_t total_bytes_sent;
    uint32_t total_bytes_received;
} howdytts_client_stats_t;

/**
 * @brief Initialize HowdyTTS client with automatic server discovery
 * 
 * @param config Client configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_init(const howdytts_client_config_t *config);

/**
 * @brief Start HowdyTTS client (discover server and connect)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_start(void);

/**
 * @brief Stop HowdyTTS client
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_stop(void);

/**
 * @brief Send voice audio to HowdyTTS server
 * 
 * @param audio_data Audio buffer (16-bit PCM)
 * @param samples Number of samples
 * @param voice_detected Whether voice is currently detected
 * @param confidence Voice detection confidence (0.0-1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_send_voice(const int16_t *audio_data, size_t samples, 
                                    bool voice_detected, float confidence);

/**
 * @brief Send text message to HowdyTTS server
 * 
 * @param text Text to convert to speech
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_send_text(const char *text);

/**
 * @brief Get current client state
 * 
 * @return howdytts_client_state_t Current state
 */
howdytts_client_state_t howdytts_client_get_state(void);

/**
 * @brief Get client statistics
 * 
 * @param stats Output statistics structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_get_stats(howdytts_client_stats_t *stats);

/**
 * @brief Get information about connected server
 * 
 * @param server_info Output server information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_get_server_info(howdytts_server_info_t *server_info);

/**
 * @brief Force reconnection to HowdyTTS server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t howdytts_client_reconnect(void);

#ifdef __cplusplus
}
#endif