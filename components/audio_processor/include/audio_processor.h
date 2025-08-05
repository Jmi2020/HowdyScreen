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
 * @brief Write audio data for playback
 * 
 * @param data Audio data to play
 * @param length Length of audio data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_processor_write_data(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif