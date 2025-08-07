#pragma once

#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Dual I2S audio mode (inspired by Arduino implementation)
 */
typedef enum {
    DUAL_I2S_MODE_MIC = 0,      // Microphone input mode
    DUAL_I2S_MODE_SPEAKER,      // Speaker output mode  
    DUAL_I2S_MODE_SIMULTANEOUS  // Both mic and speaker active
} dual_i2s_mode_t;

/**
 * @brief I2S port configuration for ESP32-P4
 */
typedef struct {
    // Microphone I2S configuration (I2S_NUM_0)
    struct {
        gpio_num_t bck_pin;     // Bit clock
        gpio_num_t ws_pin;      // Word select
        gpio_num_t data_in_pin; // Data input
        uint32_t sample_rate;   // Sample rate (16000 Hz)
        i2s_data_bit_width_t bits_per_sample;
        i2s_slot_mode_t channel_format;
    } mic_config;
    
    // Speaker I2S configuration (I2S_NUM_1) 
    struct {
        gpio_num_t bck_pin;     // Bit clock
        gpio_num_t ws_pin;      // Word select  
        gpio_num_t data_out_pin;// Data output
        uint32_t sample_rate;   // Sample rate (16000 Hz)
        i2s_data_bit_width_t bits_per_sample;
        i2s_slot_mode_t channel_format;
    } speaker_config;
    
    // DMA configuration
    uint16_t dma_buf_count;     // Number of DMA buffers
    uint16_t dma_buf_len;       // DMA buffer length
    
} dual_i2s_config_t;

/**
 * @brief Performance metrics structure for real-time monitoring
 */
typedef struct {
    float average_processing_time_us;   // Average processing time per operation
    uint32_t max_processing_time_us;    // Maximum processing time recorded
    uint32_t total_operations;          // Total number of operations
    uint32_t buffer_underruns;          // Number of buffer underrun events
    uint32_t mode_switches;             // Number of mode switch operations
    uint32_t estimated_audio_latency_ms; // Estimated total audio pipeline latency
    size_t memory_usage_bytes;          // Total memory usage by I2S system
} dual_i2s_performance_metrics_t;

/**
 * @brief Initialize dual I2S system for ESP32-P4
 * 
 * Inspired by Arduino setupSpeakerI2S and setupMicrophone functions
 * 
 * @param config Dual I2S configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_init(const dual_i2s_config_t *config);

/**
 * @brief Deinitialize dual I2S system
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_deinit(void);

/**
 * @brief Set I2S operating mode
 * 
 * Inspired by Arduino InitI2SSpeakerOrMic function
 * 
 * @param mode Operating mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_set_mode(dual_i2s_mode_t mode);

/**
 * @brief Read audio data from microphone
 * 
 * @param buffer Output buffer for audio samples
 * @param samples Number of samples to read
 * @param bytes_read Number of bytes actually read
 * @param timeout_ms Timeout in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_read_mic(int16_t *buffer, size_t samples, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Write audio data to speaker
 * 
 * Inspired by Arduino writeToAudioBuffer and playBuffer functions
 * 
 * @param buffer Audio samples to write
 * @param samples Number of samples to write
 * @param bytes_written Number of bytes actually written
 * @param timeout_ms Timeout in milliseconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_write_speaker(const int16_t *buffer, size_t samples, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Start I2S ports based on current mode
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_start(void);

/**
 * @brief Stop I2S ports
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_stop(void);

/**
 * @brief Clear DMA buffers (inspired by Arduino i2s_zero_dma_buffer usage)
 * 
 * @param clear_mic Clear microphone DMA buffer
 * @param clear_speaker Clear speaker DMA buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_clear_dma_buffers(bool clear_mic, bool clear_speaker);

/**
 * @brief Get current I2S mode
 * 
 * @return dual_i2s_mode_t Current mode
 */
dual_i2s_mode_t dual_i2s_get_mode(void);

/**
 * @brief Check if microphone is active
 * 
 * @return true if microphone I2S is active
 */
bool dual_i2s_is_mic_active(void);

/**
 * @brief Check if speaker is active
 * 
 * @return true if speaker I2S is active
 */
bool dual_i2s_is_speaker_active(void);

/**
 * @brief Set volume for speaker output (0.0 to 1.0)
 * 
 * @param volume Volume level
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_set_volume(float volume);

/**
 * @brief Get I2S statistics
 * 
 * @param mic_samples_read Total microphone samples read
 * @param speaker_samples_written Total speaker samples written
 * @param mic_errors Microphone I2S errors
 * @param speaker_errors Speaker I2S errors
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_get_stats(uint32_t *mic_samples_read, 
                             uint32_t *speaker_samples_written,
                             uint32_t *mic_errors,
                             uint32_t *speaker_errors);

/**
 * @brief Get real-time performance metrics for latency optimization
 * 
 * @param metrics Output structure for performance data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_get_performance_metrics(dual_i2s_performance_metrics_t *metrics);

/**
 * @brief Reset performance counters for fresh measurements
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t dual_i2s_reset_performance_counters(void);

#ifdef __cplusplus
}
#endif