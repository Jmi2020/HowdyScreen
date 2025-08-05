#include "audio_memory_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "AudioMemoryBuffer";

esp_err_t audio_memory_buffer_init(audio_memory_buffer_t *amb, size_t buffer_size)
{
    if (!amb || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate buffer memory (use DMA capable memory for audio)
    amb->buffer = heap_caps_malloc(buffer_size * sizeof(int16_t), MALLOC_CAP_DMA);
    if (!amb->buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer memory");
        return ESP_ERR_NO_MEM;
    }

    // Create mutex for thread safety
    amb->mutex = xSemaphoreCreateMutex();
    if (!amb->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        heap_caps_free(amb->buffer);
        return ESP_ERR_NO_MEM;
    }

    // Initialize buffer parameters
    amb->buffer_size = buffer_size;
    amb->write_pos = 0;
    amb->read_pos = 0;
    amb->available_samples = 0;
    amb->is_initialized = true;

    // Clear buffer
    memset(amb->buffer, 0, buffer_size * sizeof(int16_t));

    ESP_LOGI(TAG, "Audio memory buffer initialized: %zu samples", buffer_size);
    return ESP_OK;
}

esp_err_t audio_memory_buffer_deinit(audio_memory_buffer_t *amb)
{
    if (!amb || !amb->is_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Take mutex before cleanup
    if (xSemaphoreTake(amb->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        amb->is_initialized = false;
        xSemaphoreGive(amb->mutex);
    }

    // Free resources
    if (amb->buffer) {
        heap_caps_free(amb->buffer);
        amb->buffer = NULL;
    }

    if (amb->mutex) {
        vSemaphoreDelete(amb->mutex);
        amb->mutex = NULL;
    }

    ESP_LOGI(TAG, "Audio memory buffer deinitialized");
    return ESP_OK;
}

esp_err_t audio_memory_buffer_write(audio_memory_buffer_t *amb, const int16_t *samples, size_t num_samples)
{
    if (!amb || !amb->is_initialized || !samples || num_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(amb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex for write");
        return ESP_ERR_TIMEOUT;
    }

    size_t samples_written = 0;
    
    for (size_t i = 0; i < num_samples; i++) {
        // Check if buffer is full
        if (amb->available_samples >= amb->buffer_size) {
            // Overwrite oldest data (ring buffer behavior)
            amb->read_pos = (amb->read_pos + 1) % amb->buffer_size;
        } else {
            amb->available_samples++;
        }
        
        // Write sample
        amb->buffer[amb->write_pos] = samples[i];
        amb->write_pos = (amb->write_pos + 1) % amb->buffer_size;
        samples_written++;
    }

    xSemaphoreGive(amb->mutex);
    
    ESP_LOGD(TAG, "Written %zu samples to buffer", samples_written);
    return ESP_OK;
}

esp_err_t audio_memory_buffer_read(audio_memory_buffer_t *amb, int16_t *samples, size_t num_samples)
{
    if (!amb || !amb->is_initialized || !samples || num_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(amb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex for read");
        return ESP_ERR_TIMEOUT;
    }

    size_t samples_read = 0;
    size_t samples_to_read = (num_samples < amb->available_samples) ? num_samples : amb->available_samples;
    
    for (size_t i = 0; i < samples_to_read; i++) {
        samples[i] = amb->buffer[amb->read_pos];
        amb->read_pos = (amb->read_pos + 1) % amb->buffer_size;
        amb->available_samples--;
        samples_read++;
    }
    
    // Fill remaining with zeros if requested more than available
    for (size_t i = samples_read; i < num_samples; i++) {
        samples[i] = 0;
    }

    xSemaphoreGive(amb->mutex);
    
    ESP_LOGD(TAG, "Read %zu samples from buffer", samples_read);
    return ESP_OK;
}

size_t audio_memory_buffer_available(audio_memory_buffer_t *amb)
{
    if (!amb || !amb->is_initialized) {
        return 0;
    }

    size_t available = 0;
    if (xSemaphoreTake(amb->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        available = amb->available_samples;
        xSemaphoreGive(amb->mutex);
    }

    return available;
}

bool audio_memory_buffer_is_empty(audio_memory_buffer_t *amb)
{
    return (audio_memory_buffer_available(amb) == 0);
}

esp_err_t audio_memory_buffer_clear(audio_memory_buffer_t *amb)
{
    if (!amb || !amb->is_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(amb->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take mutex for clear");
        return ESP_ERR_TIMEOUT;
    }

    amb->write_pos = 0;
    amb->read_pos = 0;
    amb->available_samples = 0;
    memset(amb->buffer, 0, amb->buffer_size * sizeof(int16_t));

    xSemaphoreGive(amb->mutex);
    
    ESP_LOGI(TAG, "Audio buffer cleared");
    return ESP_OK;
}