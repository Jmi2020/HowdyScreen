#include "dual_i2s_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "DualI2S";

// Dual I2S manager state
static struct {
    dual_i2s_config_t config;
    dual_i2s_mode_t current_mode;
    bool is_initialized;
    bool mic_active;
    bool speaker_active;
    float volume;
    
    // Statistics
    uint32_t mic_samples_read;
    uint32_t speaker_samples_written;
    uint32_t mic_errors;
    uint32_t speaker_errors;
    
    // Thread safety
    SemaphoreHandle_t mutex;
} s_dual_i2s = {
    .current_mode = DUAL_I2S_MODE_MIC,
    .is_initialized = false,
    .mic_active = false,
    .speaker_active = false,
    .volume = 0.7f,
    .mutex = NULL
};

// Forward declarations
static esp_err_t setup_microphone_i2s(void);
static esp_err_t setup_speaker_i2s(void);
static esp_err_t stop_microphone_i2s(void);
static esp_err_t stop_speaker_i2s(void);

esp_err_t dual_i2s_init(const dual_i2s_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_dual_i2s.is_initialized) {
        ESP_LOGI(TAG, "Dual I2S already initialized");
        return ESP_OK;
    }
    
    // Create mutex for thread safety
    s_dual_i2s.mutex = xSemaphoreCreateMutex();
    if (!s_dual_i2s.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy configuration
    s_dual_i2s.config = *config;
    
    ESP_LOGI(TAG, "Initializing dual I2S system for ESP32-P4");
    ESP_LOGI(TAG, "Microphone: I2S_NUM_0, Speaker: I2S_NUM_1");
    ESP_LOGI(TAG, "Sample rates - Mic: %lu Hz, Speaker: %lu Hz", 
             config->mic_config.sample_rate, config->speaker_config.sample_rate);
    
    s_dual_i2s.is_initialized = true;
    
    return ESP_OK;
}

esp_err_t dual_i2s_deinit(void)
{
    if (!s_dual_i2s.is_initialized) {
        return ESP_OK;
    }
    
    // Stop all I2S operations
    dual_i2s_stop();
    
    // Uninstall I2S drivers
    stop_microphone_i2s();
    stop_speaker_i2s();
    
    if (s_dual_i2s.mutex) {
        vSemaphoreDelete(s_dual_i2s.mutex);
        s_dual_i2s.mutex = NULL;
    }
    
    s_dual_i2s.is_initialized = false;
    
    ESP_LOGI(TAG, "Dual I2S system deinitialized");
    ESP_LOGI(TAG, "Statistics - Mic samples: %lu, Speaker samples: %lu, Errors: %lu/%lu",
             s_dual_i2s.mic_samples_read, s_dual_i2s.speaker_samples_written,
             s_dual_i2s.mic_errors, s_dual_i2s.speaker_errors);
    
    return ESP_OK;
}

esp_err_t dual_i2s_set_mode(dual_i2s_mode_t mode)
{
    if (!s_dual_i2s.is_initialized) {
        ESP_LOGE(TAG, "Dual I2S not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_dual_i2s.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (s_dual_i2s.current_mode == mode) {
        xSemaphoreGive(s_dual_i2s.mutex);
        return ESP_OK;  // No change needed
    }
    
    ESP_LOGI(TAG, "Switching I2S mode from %d to %d", s_dual_i2s.current_mode, mode);
    
    // Stop current operations
    dual_i2s_stop();
    
    // Clear DMA buffers before mode change (inspired by Arduino implementation)
    dual_i2s_clear_dma_buffers(true, true);
    
    s_dual_i2s.current_mode = mode;
    
    // Configure I2S based on new mode
    esp_err_t ret = ESP_OK;
    
    switch (mode) {
        case DUAL_I2S_MODE_MIC:
            ret = setup_microphone_i2s();
            if (ret == ESP_OK) {
                s_dual_i2s.mic_active = true;
                s_dual_i2s.speaker_active = false;
            }
            break;
            
        case DUAL_I2S_MODE_SPEAKER:
            ret = setup_speaker_i2s();
            if (ret == ESP_OK) {
                s_dual_i2s.mic_active = false;
                s_dual_i2s.speaker_active = true;
            }
            break;
            
        case DUAL_I2S_MODE_SIMULTANEOUS:
            // Setup both I2S ports (ESP32-P4 supports this)
            ret = setup_microphone_i2s();
            if (ret == ESP_OK) {
                ret = setup_speaker_i2s();
                if (ret == ESP_OK) {
                    s_dual_i2s.mic_active = true;
                    s_dual_i2s.speaker_active = true;
                } else {
                    stop_microphone_i2s();
                }
            }
            break;
    }
    
    xSemaphoreGive(s_dual_i2s.mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "I2S mode set successfully - Mic: %s, Speaker: %s",
                 s_dual_i2s.mic_active ? "ON" : "OFF",
                 s_dual_i2s.speaker_active ? "ON" : "OFF");
    } else {
        ESP_LOGE(TAG, "Failed to set I2S mode: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

static esp_err_t setup_microphone_i2s(void)
{
    // Stop any existing microphone I2S
    i2s_driver_uninstall(I2S_NUM_0);
    
    // Configure I2S for microphone (inspired by Arduino setupMicrophone)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = s_dual_i2s.config.mic_config.sample_rate,
        .bits_per_sample = s_dual_i2s.config.mic_config.bits_per_sample,
        .channel_format = s_dual_i2s.config.mic_config.channel_format,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = s_dual_i2s.config.dma_buf_count,
        .dma_buf_len = s_dual_i2s.config.dma_buf_len,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = s_dual_i2s.config.mic_config.bck_pin,
        .ws_io_num = s_dual_i2s.config.mic_config.ws_pin,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = s_dual_i2s.config.mic_config.data_in_pin
    };
    
    esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install microphone I2S driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set microphone I2S pins: %s", esp_err_to_name(ret));
        i2s_driver_uninstall(I2S_NUM_0);
        return ret;
    }
    
    ESP_LOGI(TAG, "Microphone I2S initialized successfully");
    return ESP_OK;
}

static esp_err_t setup_speaker_i2s(void)
{
    // Stop any existing speaker I2S
    i2s_driver_uninstall(I2S_NUM_1);
    
    // Configure I2S for speaker (inspired by Arduino setupSpeakerI2S)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = s_dual_i2s.config.speaker_config.sample_rate,
        .bits_per_sample = s_dual_i2s.config.speaker_config.bits_per_sample,
        .channel_format = s_dual_i2s.config.speaker_config.channel_format,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = s_dual_i2s.config.dma_buf_count,
        .dma_buf_len = s_dual_i2s.config.dma_buf_len,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    i2s_pin_config_t pin_config = {
        .bck_io_num = s_dual_i2s.config.speaker_config.bck_pin,
        .ws_io_num = s_dual_i2s.config.speaker_config.ws_pin,
        .data_out_num = s_dual_i2s.config.speaker_config.data_out_pin,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    esp_err_t ret = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install speaker I2S driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2s_set_pin(I2S_NUM_1, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set speaker I2S pins: %s", esp_err_to_name(ret));
        i2s_driver_uninstall(I2S_NUM_1);
        return ret;
    }
    
    ESP_LOGI(TAG, "Speaker I2S initialized successfully");
    return ESP_OK;
}

static esp_err_t stop_microphone_i2s(void)
{
    esp_err_t ret = i2s_driver_uninstall(I2S_NUM_0);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Microphone I2S stopped");
    }
    return ret;
}

static esp_err_t stop_speaker_i2s(void)
{
    esp_err_t ret = i2s_driver_uninstall(I2S_NUM_1);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Speaker I2S stopped");
    }
    return ret;
}

esp_err_t dual_i2s_read_mic(int16_t *buffer, size_t samples, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!buffer || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_dual_i2s.mic_active) {
        ESP_LOGW(TAG, "Microphone I2S not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t bytes_to_read = samples * sizeof(int16_t);
    esp_err_t ret = i2s_read(I2S_NUM_0, buffer, bytes_to_read, bytes_read, 
                            pdMS_TO_TICKS(timeout_ms));
    
    if (ret == ESP_OK) {
        s_dual_i2s.mic_samples_read += (*bytes_read / sizeof(int16_t));
        ESP_LOGV(TAG, "Read %zu bytes from microphone", *bytes_read);
    } else {
        s_dual_i2s.mic_errors++;
        ESP_LOGW(TAG, "Microphone read error: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t dual_i2s_write_speaker(const int16_t *buffer, size_t samples, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!buffer || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_dual_i2s.speaker_active) {
        ESP_LOGW(TAG, "Speaker I2S not active");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Apply volume control (inspired by Arduino speaker_play volume adjustment)
    int16_t *volume_buffer = NULL;
    if (s_dual_i2s.volume != 1.0f) {
        volume_buffer = malloc(samples * sizeof(int16_t));
        if (volume_buffer) {
            for (size_t i = 0; i < samples; i++) {
                volume_buffer[i] = (int16_t)(buffer[i] * s_dual_i2s.volume);
            }
            buffer = volume_buffer;
        }
    }
    
    size_t bytes_to_write = samples * sizeof(int16_t);
    esp_err_t ret = i2s_write(I2S_NUM_1, buffer, bytes_to_write, bytes_written,
                             pdMS_TO_TICKS(timeout_ms));
    
    if (volume_buffer) {
        free(volume_buffer);
    }
    
    if (ret == ESP_OK) {
        s_dual_i2s.speaker_samples_written += (*bytes_written / sizeof(int16_t));
        ESP_LOGV(TAG, "Wrote %zu bytes to speaker", *bytes_written);
    } else {
        s_dual_i2s.speaker_errors++;
        ESP_LOGW(TAG, "Speaker write error: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t dual_i2s_start(void)
{
    esp_err_t ret = ESP_OK;
    
    if (s_dual_i2s.mic_active) {
        ret = i2s_start(I2S_NUM_0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start microphone I2S: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGD(TAG, "Microphone I2S started");
    }
    
    if (s_dual_i2s.speaker_active) {
        ret = i2s_start(I2S_NUM_1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start speaker I2S: %s", esp_err_to_name(ret));
            if (s_dual_i2s.mic_active) {
                i2s_stop(I2S_NUM_0);  // Stop mic if speaker fails
            }
            return ret;
        }
        ESP_LOGD(TAG, "Speaker I2S started");
    }
    
    return ret;
}

esp_err_t dual_i2s_stop(void)
{
    esp_err_t ret = ESP_OK;
    
    if (s_dual_i2s.mic_active) {
        esp_err_t mic_ret = i2s_stop(I2S_NUM_0);
        if (mic_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop microphone I2S: %s", esp_err_to_name(mic_ret));
            ret = mic_ret;
        }
    }
    
    if (s_dual_i2s.speaker_active) {
        esp_err_t speaker_ret = i2s_stop(I2S_NUM_1);
        if (speaker_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop speaker I2S: %s", esp_err_to_name(speaker_ret));
            ret = speaker_ret;
        }
    }
    
    return ret;
}

esp_err_t dual_i2s_clear_dma_buffers(bool clear_mic, bool clear_speaker)
{
    esp_err_t ret = ESP_OK;
    
    if (clear_mic && s_dual_i2s.mic_active) {
        esp_err_t mic_ret = i2s_zero_dma_buffer(I2S_NUM_0);
        if (mic_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear microphone DMA buffer: %s", esp_err_to_name(mic_ret));
            ret = mic_ret;
        } else {
            ESP_LOGD(TAG, "Microphone DMA buffer cleared");
        }
    }
    
    if (clear_speaker && s_dual_i2s.speaker_active) {
        esp_err_t speaker_ret = i2s_zero_dma_buffer(I2S_NUM_1);
        if (speaker_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear speaker DMA buffer: %s", esp_err_to_name(speaker_ret));
            ret = speaker_ret;
        } else {
            ESP_LOGD(TAG, "Speaker DMA buffer cleared");
        }
    }
    
    return ret;
}

dual_i2s_mode_t dual_i2s_get_mode(void)
{
    return s_dual_i2s.current_mode;
}

bool dual_i2s_is_mic_active(void)
{
    return s_dual_i2s.mic_active;
}

bool dual_i2s_is_speaker_active(void)
{
    return s_dual_i2s.speaker_active;
}

esp_err_t dual_i2s_set_volume(float volume)
{
    if (volume < 0.0f || volume > 1.0f) {
        ESP_LOGE(TAG, "Invalid volume level: %.2f", volume);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_dual_i2s.volume = volume;
    ESP_LOGI(TAG, "Volume set to %.2f", volume);
    return ESP_OK;
}

esp_err_t dual_i2s_get_stats(uint32_t *mic_samples_read, 
                             uint32_t *speaker_samples_written,
                             uint32_t *mic_errors,
                             uint32_t *speaker_errors)
{
    if (mic_samples_read) {
        *mic_samples_read = s_dual_i2s.mic_samples_read;
    }
    
    if (speaker_samples_written) {
        *speaker_samples_written = s_dual_i2s.speaker_samples_written;
    }
    
    if (mic_errors) {
        *mic_errors = s_dual_i2s.mic_errors;
    }
    
    if (speaker_errors) {
        *speaker_errors = s_dual_i2s.speaker_errors;
    }
    
    return ESP_OK;
}