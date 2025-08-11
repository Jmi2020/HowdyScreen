#pragma once

#include "esp_err.h"
#include "driver/i2s_std.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;       // Sample rate (16000 Hz recommended)
    uint8_t bits_per_sample;    // Bits per sample (16 recommended)
    uint8_t channels;           // Number of channels (1 for mono)
    uint8_t dma_buf_count;      // DMA buffer count
    uint16_t dma_buf_len;       // DMA buffer length
    uint8_t task_priority;      // Audio task priority
    uint8_t task_core;          // CPU core for audio task
} audio_processor_config_t;

typedef enum {
    AUDIO_EVENT_STARTED,
    AUDIO_EVENT_STOPPED,
    AUDIO_EVENT_DATA_READY,
    AUDIO_EVENT_ERROR
} audio_event_t;

typedef void (*audio_event_callback_t)(audio_event_t event, void *data, size_t len);

// HowdyTTS Audio Integration
typedef struct {
    bool enable_howdytts_streaming;     // Enable HowdyTTS UDP streaming
    bool enable_opus_encoding;          // Enable OPUS compression
    uint8_t opus_compression_level;     // OPUS compression level (1-10)
    bool enable_websocket_fallback;     // Enable WebSocket fallback
    void (*howdytts_audio_callback)(const int16_t* samples, size_t count, void* user_data);
    void *howdytts_user_data;
} audio_howdytts_config_t;

/**
 * @brief Initialize audio processor
 * 
 * @param config Audio processor configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_init(const audio_processor_config_t *config);

/**
 * @brief Start audio capture
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_start_capture(void);

/**
 * @brief Stop audio capture
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_stop_capture(void);

/**
 * @brief Start audio playback
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_start_playback(void);

/**
 * @brief Stop audio playback
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_stop_playback(void);

/**
 * @brief Set audio event callback
 * 
 * @param callback Callback function for audio events
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_callback(audio_event_callback_t callback);

/**
 * @brief Get audio buffer for processing
 * 
 * @param buffer Pointer to store buffer address
 * @param length Pointer to store buffer length
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_buffer(uint8_t **buffer, size_t *length);

/**
 * @brief Release audio buffer after processing
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_release_buffer(void);

/**
 * @brief Enqueue audio data for playback (non-blocking)
 * 
 * Queues raw PCM 16-bit mono data for the playback task. Data may be split
 * into fixed frame blocks internally (e.g., 20 ms @ 16 kHz = 320 samples).
 * 
 * @param data Audio data to play (PCM 16-bit mono)
 * @param length Length of audio data in bytes
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_write_data(const uint8_t *data, size_t length);

/**
 * @brief Get current playback queue depth in frames
 * 
 * Useful for UI/telemetry to visualize jitter buffer depth.
 * @param out_frames Pointer to store the number of queued frames
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_playback_depth(size_t *out_frames);

/**
 * @brief Configure HowdyTTS audio streaming integration
 * 
 * @param howdy_config HowdyTTS configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_configure_howdytts(const audio_howdytts_config_t *howdy_config);

/**
 * @brief Enable/disable dual protocol mode (WebSocket + UDP)
 * 
 * @param enable_dual_mode Enable dual mode operation
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_set_dual_protocol(bool enable_dual_mode);

/**
 * @brief Switch between WebSocket and UDP streaming protocols
 * 
 * @param use_websocket true for WebSocket, false for UDP
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_switch_protocol(bool use_websocket);

/**
 * @brief Get audio processing statistics for monitoring
 * 
 * @param frames_processed Total frames processed
 * @param avg_latency_ms Average processing latency
 * @param protocol_active Currently active protocol (0=UDP, 1=WebSocket)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_get_stats(uint32_t *frames_processed, float *avg_latency_ms, uint8_t *protocol_active);

#ifdef __cplusplus
}
#endif
