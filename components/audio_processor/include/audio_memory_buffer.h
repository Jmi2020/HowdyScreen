#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio memory buffer for real-time audio streaming
 * 
 * Inspired by the Arduino AudioMemoryBuffer implementation
 * Provides thread-safe ring buffer for audio samples
 */
typedef struct {
    int16_t *buffer;
    size_t buffer_size;      // Total buffer size in samples
    size_t write_pos;        // Write position
    size_t read_pos;         // Read position
    size_t available_samples; // Number of samples available for reading
    SemaphoreHandle_t mutex; // Thread safety
    bool is_initialized;
} audio_memory_buffer_t;

/**
 * @brief Initialize audio memory buffer
 * 
 * @param amb Audio memory buffer instance
 * @param buffer_size Buffer size in samples
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_memory_buffer_init(audio_memory_buffer_t *amb, size_t buffer_size);

/**
 * @brief Deinitialize audio memory buffer
 * 
 * @param amb Audio memory buffer instance
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_memory_buffer_deinit(audio_memory_buffer_t *amb);

/**
 * @brief Write audio samples to buffer
 * 
 * @param amb Audio memory buffer instance
 * @param samples Audio samples to write
 * @param num_samples Number of samples to write
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_memory_buffer_write(audio_memory_buffer_t *amb, const int16_t *samples, size_t num_samples);

/**
 * @brief Read audio samples from buffer
 * 
 * @param amb Audio memory buffer instance
 * @param samples Output buffer for samples
 * @param num_samples Number of samples to read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_memory_buffer_read(audio_memory_buffer_t *amb, int16_t *samples, size_t num_samples);

/**
 * @brief Get number of samples available for reading
 * 
 * @param amb Audio memory buffer instance
 * @return size_t Number of available samples
 */
size_t audio_memory_buffer_available(audio_memory_buffer_t *amb);

/**
 * @brief Check if buffer is empty
 * 
 * @param amb Audio memory buffer instance
 * @return true if empty
 */
bool audio_memory_buffer_is_empty(audio_memory_buffer_t *amb);

/**
 * @brief Clear all data from buffer
 * 
 * @param amb Audio memory buffer instance
 * @return esp_err_t ESP_OK on success
 */
esp_err_t audio_memory_buffer_clear(audio_memory_buffer_t *amb);

#ifdef __cplusplus
}
#endif