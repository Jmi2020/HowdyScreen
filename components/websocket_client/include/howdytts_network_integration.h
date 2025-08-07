/**
 * @file howdytts_network_integration.h
 * @brief HowdyTTS Native Protocol Integration for ESP32-P4 HowdyScreen
 * 
 * Provides native HowdyTTS protocol support with:
 * - UDP discovery protocol (port 8001)
 * - PCM audio streaming via UDP (port 8003) 
 * - HTTP state management server (port 8080)
 * - Automatic server discovery and device registration
 * - Room-based device management
 * - Real-time status monitoring
 * 
 * Optimized for ESP32-P4 with minimal memory overhead (<10KB for audio streaming)
 * and raw PCM streaming for maximum performance and reliability.
 */

#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HowdyTTS Protocol Configuration
#define HOWDYTTS_DISCOVERY_PORT         8001
#define HOWDYTTS_AUDIO_PORT            8003
#define HOWDYTTS_HTTP_PORT             8080

// Audio Configuration (PCM Streaming)
#define HOWDYTTS_SAMPLE_RATE           16000    // 16kHz mono
#define HOWDYTTS_CHANNELS              1        // Mono audio
#define HOWDYTTS_FRAME_SIZE            320      // 20ms frames (320 samples)
#define HOWDYTTS_AUDIO_FORMAT          PCM_16   // Raw 16-bit PCM
#define HOWDYTTS_BANDWIDTH             256000   // 256 kbps uncompressed
#define HOWDYTTS_PACKET_SIZE           640      // 320 samples × 2 bytes
#define HOWDYTTS_PACKET_LOSS_THRESHOLD 0.01     // 1% maximum

// Discovery Protocol
#define HOWDYTTS_DISCOVERY_REQUEST     "HOWDYTTS_DISCOVERY"
#define HOWDYTTS_DISCOVERY_RESPONSE    "HOWDYSCREEN_ESP32P4_%s_ROOM_%s"

// Device Configuration
#define HOWDYTTS_DEVICE_TYPE           "ESP32P4_HowdyScreen"
#define HOWDYTTS_MAX_DEVICE_ID_LEN     32
#define HOWDYTTS_MAX_ROOM_NAME_LEN     32
#define HOWDYTTS_MAX_DEVICE_NAME_LEN   64

// Network Timeouts
#define HOWDYTTS_DISCOVERY_TIMEOUT_MS  10000
#define HOWDYTTS_CONNECTION_TIMEOUT_MS 5000
#define HOWDYTTS_KEEPALIVE_INTERVAL_MS 30000
#define HOWDYTTS_RETRY_MAX_COUNT       5

// Memory Optimization
#define HOWDYTTS_AUDIO_BUFFER_COUNT    4       // Circular buffer count
#define HOWDYTTS_MAX_PACKET_QUEUE      8       // Maximum queued packets
#define HOWDYTTS_STATS_BUFFER_SIZE     1024    // Statistics buffer size

/**
 * @brief HowdyTTS Protocol Modes
 */
typedef enum {
    HOWDYTTS_PROTOCOL_UDP_ONLY = 0,    ///< UDP only (HowdyTTS native)
    HOWDYTTS_PROTOCOL_WEBSOCKET_ONLY,  ///< WebSocket only (fallback)
    HOWDYTTS_PROTOCOL_DUAL,            ///< Intelligent switching (recommended)
    HOWDYTTS_PROTOCOL_AUTO             ///< Automatic protocol detection
} howdytts_protocol_mode_t;

/**
 * @brief HowdyTTS Audio Formats
 */
typedef enum {
    HOWDYTTS_AUDIO_PCM_16 = 0,         ///< Raw 16-bit PCM (recommended)
    HOWDYTTS_AUDIO_ADPCM,              ///< Simple ADPCM compression (future)
    HOWDYTTS_AUDIO_OPUS                ///< OPUS compression (future)
} howdytts_audio_format_t;

/**
 * @brief HowdyTTS Connection States
 */
typedef enum {
    HOWDYTTS_STATE_DISCONNECTED = 0,   ///< Not connected
    HOWDYTTS_STATE_DISCOVERING,        ///< Searching for servers
    HOWDYTTS_STATE_CONNECTING,         ///< Establishing connection
    HOWDYTTS_STATE_CONNECTED,          ///< Connected and ready
    HOWDYTTS_STATE_STREAMING,          ///< Audio streaming active
    HOWDYTTS_STATE_ERROR               ///< Error state
} howdytts_connection_state_t;

/**
 * @brief HowdyTTS Voice Assistant States (from server)
 */
typedef enum {
    HOWDYTTS_VA_STATE_WAITING = 0,     ///< Waiting for wake word
    HOWDYTTS_VA_STATE_LISTENING,       ///< Recording user input
    HOWDYTTS_VA_STATE_THINKING,        ///< Processing LLM response
    HOWDYTTS_VA_STATE_SPEAKING,        ///< Playing TTS response
    HOWDYTTS_VA_STATE_ENDING           ///< Conversation ending
} howdytts_va_state_t;

/**
 * @brief HowdyTTS Server Information
 */
typedef struct {
    char hostname[64];                 ///< Server hostname
    char ip_address[16];               ///< Server IP address
    uint16_t discovery_port;           ///< Discovery port (8001)
    uint16_t audio_port;               ///< Audio streaming port (8003)
    uint16_t http_port;                ///< HTTP state port (8080)
    int32_t rssi;                      ///< Signal strength
    uint32_t last_seen;                ///< Last communication timestamp
    bool is_available;                 ///< Server availability
    float latency_ms;                  ///< Connection latency
} howdytts_server_info_t;

/**
 * @brief HowdyTTS Device Configuration
 */
typedef struct {
    char device_id[HOWDYTTS_MAX_DEVICE_ID_LEN];         ///< Unique device identifier
    char device_name[HOWDYTTS_MAX_DEVICE_NAME_LEN];     ///< Human-readable name
    char room[HOWDYTTS_MAX_ROOM_NAME_LEN];              ///< Room assignment
    char default_server_ip[16];                         ///< Default server IP for direct connection
    howdytts_protocol_mode_t protocol_mode;             ///< Protocol mode
    howdytts_audio_format_t audio_format;               ///< Audio format
    uint32_t sample_rate;                               ///< Audio sample rate
    uint16_t frame_size;                                ///< Audio frame size
    bool enable_audio_stats;                            ///< Enable statistics
    bool enable_fallback;                               ///< Enable WebSocket fallback
    uint32_t discovery_timeout_ms;                      ///< Discovery timeout
    uint8_t connection_retry_count;                     ///< Retry attempts
} howdytts_integration_config_t;

/**
 * @brief HowdyTTS Audio Statistics
 */
typedef struct {
    uint32_t packets_sent;             ///< Total packets sent
    uint32_t packets_received;         ///< Total packets received
    uint32_t packets_lost;             ///< Lost packets
    uint32_t bytes_sent;               ///< Total bytes sent
    uint32_t bytes_received;           ///< Total bytes received
    float packet_loss_rate;            ///< Packet loss percentage
    float average_latency_ms;          ///< Average latency
    uint32_t audio_underruns;          ///< Audio buffer underruns
    uint32_t audio_overruns;           ///< Audio buffer overruns
    uint32_t connection_count;         ///< Connection attempts
    uint32_t last_update_time;         ///< Last statistics update
} howdytts_audio_stats_t;

/**
 * @brief HowdyTTS PCM Audio Packet Structure
 */
typedef struct __attribute__((packed)) {
    uint32_t sequence;                 ///< Packet sequence number
    uint32_t timestamp;                ///< Sample timestamp
    uint16_t samples;                  ///< Number of samples (320 for 20ms)
    uint16_t reserved;                 ///< Reserved for alignment
    int16_t audio_data[];              ///< Raw PCM audio data
} howdytts_pcm_packet_t;

/**
 * @brief HowdyTTS Event Types
 */
typedef enum {
    HOWDYTTS_EVENT_DISCOVERY_STARTED = 0,  ///< Discovery process started
    HOWDYTTS_EVENT_SERVER_DISCOVERED,      ///< Server discovered
    HOWDYTTS_EVENT_CONNECTION_ESTABLISHED, ///< Connection established
    HOWDYTTS_EVENT_CONNECTION_LOST,        ///< Connection lost
    HOWDYTTS_EVENT_AUDIO_STREAMING_STARTED,///< Audio streaming started
    HOWDYTTS_EVENT_AUDIO_STREAMING_STOPPED,///< Audio streaming stopped
    HOWDYTTS_EVENT_VA_STATE_CHANGED,       ///< Voice assistant state changed
    HOWDYTTS_EVENT_PROTOCOL_SWITCHED,      ///< Protocol switched (UDP/WebSocket)
    HOWDYTTS_EVENT_ERROR                   ///< Error occurred
} howdytts_event_type_t;

/**
 * @brief HowdyTTS Event Data
 */
typedef struct {
    howdytts_event_type_t event_type;  ///< Event type
    union {
        howdytts_server_info_t server_info;        ///< Server information
        howdytts_connection_state_t connection_state; ///< Connection state
        howdytts_va_state_t va_state;              ///< Voice assistant state
        howdytts_protocol_mode_t protocol_mode;    ///< Protocol mode
        esp_err_t error_code;                      ///< Error code
    } data;
    char message[128];                 ///< Event message
    uint32_t timestamp;                ///< Event timestamp
} howdytts_event_data_t;

/**
 * @brief Audio Data Callback Function
 * 
 * Called when audio data is ready for streaming to HowdyTTS server
 * 
 * @param audio_data Pointer to audio data (PCM 16-bit)
 * @param samples Number of samples
 * @param user_data User data pointer
 * @return ESP_OK on success
 */
typedef esp_err_t (*howdytts_audio_callback_t)(const int16_t *audio_data, size_t samples, void *user_data);

/**
 * @brief TTS Audio Callback Function
 * 
 * Called when TTS audio is received from HowdyTTS server
 * 
 * @param tts_audio Pointer to TTS audio data
 * @param samples Number of samples
 * @param user_data User data pointer
 * @return ESP_OK on success
 */
typedef esp_err_t (*howdytts_tts_callback_t)(const int16_t *tts_audio, size_t samples, void *user_data);

/**
 * @brief Event Callback Function
 * 
 * Called when HowdyTTS events occur
 * 
 * @param event Event data
 * @param user_data User data pointer
 */
typedef void (*howdytts_event_callback_t)(const howdytts_event_data_t *event, void *user_data);

/**
 * @brief Voice Assistant State Callback Function
 * 
 * Called when voice assistant state changes from server
 * 
 * @param va_state Voice assistant state
 * @param state_text Optional state text
 * @param user_data User data pointer
 */
typedef void (*howdytts_va_state_callback_t)(howdytts_va_state_t va_state, const char *state_text, void *user_data);

/**
 * @brief HowdyTTS Integration Callbacks
 */
typedef struct {
    howdytts_audio_callback_t audio_callback;     ///< Audio data callback
    howdytts_tts_callback_t tts_callback;         ///< TTS audio callback
    howdytts_event_callback_t event_callback;     ///< Event callback
    howdytts_va_state_callback_t va_state_callback; ///< VA state callback
    void *user_data;                              ///< User data pointer
} howdytts_integration_callbacks_t;

/**
 * @brief Initialize HowdyTTS integration
 * 
 * @param config Integration configuration
 * @param callbacks Event callbacks
 * @return ESP_OK on success
 */
esp_err_t howdytts_integration_init(const howdytts_integration_config_t *config,
                                   const howdytts_integration_callbacks_t *callbacks);

/**
 * @brief Start HowdyTTS server discovery
 * 
 * @param timeout_ms Discovery timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t howdytts_discovery_start(uint32_t timeout_ms);

/**
 * @brief Stop HowdyTTS server discovery
 * 
 * @return ESP_OK on success
 */
esp_err_t howdytts_discovery_stop(void);

/**
 * @brief Connect to HowdyTTS server
 * 
 * @param server_info Server information
 * @return ESP_OK on success
 */
esp_err_t howdytts_connect_to_server(const howdytts_server_info_t *server_info);

/**
 * @brief Disconnect from HowdyTTS server
 * 
 * @return ESP_OK on success
 */
esp_err_t howdytts_disconnect(void);

/**
 * @brief Stream audio data to HowdyTTS server
 * 
 * @param audio_data Pointer to PCM audio data
 * @param samples Number of samples
 * @return ESP_OK on success
 */
esp_err_t howdytts_stream_audio(const int16_t *audio_data, size_t samples);

/**
 * @brief Start audio streaming session
 * 
 * @return ESP_OK on success
 */
esp_err_t howdytts_start_audio_streaming(void);

/**
 * @brief Stop audio streaming session
 * 
 * @return ESP_OK on success
 */
esp_err_t howdytts_stop_audio_streaming(void);

/**
 * @brief Get current connection state
 * 
 * @return Current connection state
 */
howdytts_connection_state_t howdytts_get_connection_state(void);

/**
 * @brief Get current voice assistant state
 * 
 * @return Current voice assistant state
 */
howdytts_va_state_t howdytts_get_va_state(void);

/**
 * @brief Get audio streaming statistics
 * 
 * @param stats Pointer to statistics structure
 * @return ESP_OK on success
 */
esp_err_t howdytts_get_audio_stats(howdytts_audio_stats_t *stats);

/**
 * @brief Get discovered servers list
 * 
 * @param servers Array of server info structures
 * @param max_servers Maximum number of servers
 * @param server_count Actual number of servers found
 * @return ESP_OK on success
 */
esp_err_t howdytts_get_discovered_servers(howdytts_server_info_t *servers, 
                                         size_t max_servers, 
                                         size_t *server_count);

/**
 * @brief Update device status (audio level, battery, signal strength)
 * 
 * @param audio_level Current audio level (0.0-1.0)
 * @param battery_level Battery percentage (-1 if not available)
 * @param signal_strength WiFi signal strength in dBm
 * @return ESP_OK on success
 */
esp_err_t howdytts_update_device_status(float audio_level, int battery_level, int signal_strength);

/**
 * @brief Set protocol mode
 * 
 * @param mode Protocol mode
 * @return ESP_OK on success
 */
esp_err_t howdytts_set_protocol_mode(howdytts_protocol_mode_t mode);

/**
 * @brief Get protocol mode
 * 
 * @return Current protocol mode
 */
howdytts_protocol_mode_t howdytts_get_protocol_mode(void);

/**
 * @brief Check if HowdyTTS integration is available
 * 
 * @return true if available
 */
bool howdytts_is_available(void);

/**
 * @brief Deinitialize HowdyTTS integration
 * 
 * @return ESP_OK on success
 */
esp_err_t howdytts_integration_deinit(void);

#ifdef __cplusplus
}
#endif