#include "audio_pipeline.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "audio_pipeline";

// Hardware configuration for ESP32-P4 with ES8311 codec
#define I2S_MCLK        GPIO_NUM_13
#define I2S_SCLK        GPIO_NUM_12  
#define I2S_ASDOUT      GPIO_NUM_11  // Audio serial data out
#define I2S_LRCK        GPIO_NUM_10  // Left/right channel clock
#define I2S_DSDIN       GPIO_NUM_9   // Audio serial data in
#define AUDIO_PA_CTRL   GPIO_NUM_53  // Power amplifier control

// I2C pins for ES8311 control
#define I2C_SDA         GPIO_NUM_7
#define I2C_SCL         GPIO_NUM_8

// Audio configuration
#define SAMPLE_RATE     16000
#define CHANNELS        1
#define BITS_PER_SAMPLE 16
#define FRAME_SIZE      320          // 20ms @ 16kHz
#define AUDIO_BUFFER_SIZE (FRAME_SIZE * 2)

// ES8311 register definitions
#define ES8311_REG_00   0x00  // Reset
#define ES8311_REG_01   0x01  // Clock Manager
#define ES8311_REG_02   0x02  // Clock Manager
#define ES8311_REG_03   0x03  // Clock Manager
#define ES8311_REG_04   0x04  // Clock Manager
#define ES8311_REG_05   0x05  // System
#define ES8311_REG_06   0x06  // System
#define ES8311_REG_07   0x07  // System
#define ES8311_REG_08   0x08  // System
#define ES8311_REG_09   0x09  // System
#define ES8311_REG_0A   0x0A  // System
#define ES8311_REG_0B   0x0B  // System
#define ES8311_REG_0C   0x0C  // System
#define ES8311_REG_0D   0x0D  // Chip
#define ES8311_REG_0E   0x0E  // Chip
#define ES8311_REG_0F   0x0F  // Chip
#define ES8311_REG_10   0x10  // Chip
#define ES8311_REG_11   0x11  // Chip
#define ES8311_REG_12   0x12  // Chip
#define ES8311_REG_13   0x13  // Chip
#define ES8311_REG_14   0x14  // Chip
#define ES8311_REG_15   0x15  // ADC
#define ES8311_REG_16   0x16  // ADC
#define ES8311_REG_17   0x17  // ADC
#define ES8311_REG_18   0x18  // ADC
#define ES8311_REG_19   0x19  // ADC
#define ES8311_REG_1A   0x1A  // ADC
#define ES8311_REG_1B   0x1B  // ADC
#define ES8311_REG_1C   0x1C  // ADC
#define ES8311_REG_32   0x32  // DAC
#define ES8311_REG_33   0x33  // DAC
#define ES8311_REG_34   0x34  // DAC
#define ES8311_REG_35   0x35  // DAC
#define ES8311_REG_36   0x36  // DAC
#define ES8311_REG_37   0x37  // DAC

#define ES8311_I2C_ADDR 0x18

static esp_err_t es8311_write_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t data)
{
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), -1);
}

static esp_err_t es8311_read_reg(i2c_master_dev_handle_t dev_handle, uint8_t reg, uint8_t *data)
{
    return i2c_master_transmit_receive(dev_handle, &reg, 1, data, 1, -1);
}

static esp_err_t es8311_codec_init(i2c_master_dev_handle_t dev_handle)
{
    ESP_LOGI(TAG, "Initializing ES8311 codec");

    // Reset codec
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_00, 0x1F), TAG, "Reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_00, 0x00), TAG, "Reset release failed");

    // Configure clock management for 16kHz
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_01, 0x7F), TAG, "Clock config 1 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_02, 0x88), TAG, "Clock config 2 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_03, 0x09), TAG, "Clock config 3 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_04, 0x00), TAG, "Clock config 4 failed");

    // System configuration
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_05, 0x00), TAG, "System config 1 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_06, 0x04), TAG, "System config 2 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_07, 0x00), TAG, "System config 3 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_08, 0xFF), TAG, "System config 4 failed");

    // ADC configuration  
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_15, 0x40), TAG, "ADC config 1 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_16, 0x00), TAG, "ADC config 2 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_17, 0xBF), TAG, "ADC config 3 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_18, 0x26), TAG, "ADC config 4 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_19, 0x06), TAG, "ADC config 5 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_1A, 0x00), TAG, "ADC config 6 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_1B, 0x00), TAG, "ADC config 7 failed");

    // DAC configuration
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_32, 0xBF), TAG, "DAC config 1 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_33, 0x00), TAG, "DAC config 2 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_34, 0x08), TAG, "DAC config 3 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_35, 0x00), TAG, "DAC config 4 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_36, 0x00), TAG, "DAC config 5 failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_37, 0x08), TAG, "DAC config 6 failed");

    // Enable ADC and DAC
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_0D, 0x01), TAG, "Enable failed");
    ESP_RETURN_ON_ERROR(es8311_write_reg(dev_handle, ES8311_REG_0E, 0x02), TAG, "Power up failed");

    ESP_LOGI(TAG, "ES8311 codec initialized successfully");
    return ESP_OK;
}

esp_err_t audio_pipeline_init(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(pipeline, 0, sizeof(audio_pipeline_t));

    ESP_LOGI(TAG, "Initializing audio pipeline");

    // Initialize I2C for codec control
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_bus_config, &pipeline->i2c_bus), TAG, "I2C bus init failed");

    i2c_device_config_t codec_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(pipeline->i2c_bus, &codec_cfg, &pipeline->codec_dev), TAG, "Codec device add failed");

    // Initialize I2S channels
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &pipeline->tx_handle, &pipeline->rx_handle), TAG, "I2S channel creation failed");

    // Configure I2S standard mode
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK,
            .bclk = I2S_SCLK,
            .ws = I2S_LRCK,
            .dout = I2S_ASDOUT,
            .din = I2S_DSDIN,
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(pipeline->tx_handle, &std_cfg), TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(pipeline->rx_handle, &std_cfg), TAG, "I2S RX init failed");

    // Initialize ES8311 codec
    ESP_RETURN_ON_ERROR(es8311_codec_init(pipeline->codec_dev), TAG, "Codec init failed");

    pipeline->initialized = true;
    ESP_LOGI(TAG, "Audio pipeline initialized successfully");
    return ESP_OK;
}

esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline)
{
    if (!pipeline || !pipeline->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting audio pipeline");
    
    ESP_RETURN_ON_ERROR(i2s_channel_enable(pipeline->tx_handle), TAG, "I2S TX enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(pipeline->rx_handle), TAG, "I2S RX enable failed");

    ESP_LOGI(TAG, "Audio pipeline started");
    return ESP_OK;
}

esp_err_t audio_pipeline_stop(audio_pipeline_t *pipeline)
{
    if (!pipeline || !pipeline->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping audio pipeline");
    
    ESP_RETURN_ON_ERROR(i2s_channel_disable(pipeline->tx_handle), TAG, "I2S TX disable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_disable(pipeline->rx_handle), TAG, "I2S RX disable failed");

    ESP_LOGI(TAG, "Audio pipeline stopped");
    return ESP_OK;
}

size_t audio_pipeline_read(audio_pipeline_t *pipeline, int16_t *buffer, size_t frames)
{
    if (!pipeline || !pipeline->initialized || !buffer) {
        return 0;
    }

    size_t bytes_read;
    // Use bounded timeout to prevent system lockup - 20ms max for real-time constraints
    esp_err_t ret = i2s_channel_read(pipeline->rx_handle, buffer, frames * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(20));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read failed: %s", esp_err_to_name(ret));
        return 0;
    }

    return bytes_read / sizeof(int16_t);
}

size_t audio_pipeline_write(audio_pipeline_t *pipeline, const int16_t *buffer, size_t frames)
{
    if (!pipeline || !pipeline->initialized || !buffer) {
        return 0;
    }

    size_t bytes_written;
    // Use bounded timeout to prevent system lockup - 20ms max for real-time constraints
    esp_err_t ret = i2s_channel_write(pipeline->tx_handle, buffer, frames * sizeof(int16_t), &bytes_written, pdMS_TO_TICKS(20));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
        return 0;
    }

    return bytes_written / sizeof(int16_t);
}

void audio_analyze_buffer(const int16_t *buffer, size_t frames, audio_analysis_t *analysis)
{
    if (!buffer || !analysis || frames == 0) {
        return;
    }

    // Calculate RMS level
    float sum_squares = 0.0;
    int16_t max_sample = 0;
    
    for (size_t i = 0; i < frames; i++) {
        int16_t sample = buffer[i];
        sum_squares += (float)(sample * sample);
        if (abs(sample) > abs(max_sample)) {
            max_sample = sample;
        }
    }
    
    float rms = sqrtf(sum_squares / frames);
    analysis->overall_level = rms / 32767.0f;  // Normalize to 0-1
    
    // Simple frequency analysis (placeholder - would use FFT in real implementation)
    // For now, use amplitude-based estimation
    analysis->bass = analysis->overall_level * 0.3f;
    analysis->mid = analysis->overall_level * 0.5f; 
    analysis->treble = analysis->overall_level * 0.2f;
    
    // Voice activity detection (simple threshold-based)
    analysis->voice_detected = analysis->overall_level > 0.01f;
}

esp_err_t audio_pipeline_deinit(audio_pipeline_t *pipeline)
{
    if (!pipeline) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!pipeline->initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing audio pipeline");

    if (pipeline->tx_handle) {
        i2s_del_channel(pipeline->tx_handle);
    }
    if (pipeline->rx_handle) {
        i2s_del_channel(pipeline->rx_handle);
    }
    if (pipeline->codec_dev) {
        i2c_master_bus_rm_device(pipeline->codec_dev);
    }
    if (pipeline->i2c_bus) {
        i2c_del_master_bus(pipeline->i2c_bus);
    }

    memset(pipeline, 0, sizeof(audio_pipeline_t));
    ESP_LOGI(TAG, "Audio pipeline deinitialized");
    return ESP_OK;
}