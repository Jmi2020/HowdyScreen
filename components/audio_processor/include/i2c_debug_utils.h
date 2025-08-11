#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C Debug Utilities for ESP32-P4 HowdyScreen Audio Codecs
 * 
 * This module provides comprehensive I2C debugging utilities for diagnosing
 * codec communication issues with ES8311 (speaker) and ES7210 (microphone) codecs
 * on the Waveshare ESP32-P4-WIFI6-Touch-LCD-XC board.
 */

// Codec I2C addresses (from Waveshare BSP documentation)
#define ES8311_I2C_ADDR         0x18    // ES8311 DAC (speaker codec)
#define ES7210_I2C_ADDR         0x40    // ES7210 ADC (microphone codec)

// I2C scan range (standard 7-bit addresses)
#define I2C_SCAN_ADDR_MIN       0x08    // Minimum valid I2C address
#define I2C_SCAN_ADDR_MAX       0x78    // Maximum valid I2C address

// ES7210 register addresses for debugging
#define ES7210_RESET_REG        0x00
#define ES7210_CLOCK_OFF_REG    0x01
#define ES7210_MAINCLK_REG      0x02
#define ES7210_MASTER_CLK_REG   0x03
#define ES7210_LRCK_DIVIDER_H   0x04
#define ES7210_LRCK_DIVIDER_L   0x05
#define ES7210_POWER_DOWN_REG   0x06
#define ES7210_OSR_REG          0x07
#define ES7210_MODE_CONFIG_REG  0x21
#define ES7210_CHIPID1_REG      0xFD
#define ES7210_CHIPID0_REG      0xFE
#define ES7210_VERSION_REG      0xFF

// ES8311 register addresses for debugging
#define ES8311_RESET_REG        0x00
#define ES8311_CLK_MANAGER_REG  0x01
#define ES8311_CLK_MANAGER2_REG 0x02
#define ES8311_CLK_MANAGER3_REG 0x03
#define ES8311_ADC_REG          0x09
#define ES8311_DAC_REG          0x31
#define ES8311_CHIPID1_REG      0xFD
#define ES8311_CHIPID2_REG      0xFE
#define ES8311_VERSION_REG      0xFF

// Expected chip ID values
#define ES7210_EXPECTED_CHIP_ID 0x32
#define ES8311_EXPECTED_CHIP_ID 0x83

// Debug configuration structure
typedef struct {
    i2c_master_bus_handle_t i2c_bus_handle;
    bool verbose_output;
    bool scan_enabled;
    bool codec_verification_enabled;
} i2c_debug_config_t;

// Device discovery result
typedef struct {
    uint8_t address;
    bool responsive;
    char device_name[32];
} i2c_device_info_t;

/**
 * @brief Initialize I2C debug utilities
 * 
 * @param config Debug configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_debug_init(const i2c_debug_config_t *config);

/**
 * @brief Scan I2C bus for all devices
 * 
 * Scans addresses from 0x08 to 0x78 and reports all responsive devices.
 * This is the primary diagnostic tool for I2C communication issues.
 * 
 * @param devices Array to store discovered devices (optional, can be NULL)
 * @param max_devices Maximum number of devices to store
 * @param found_count Number of devices actually found (output parameter)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t i2c_scan_bus(i2c_device_info_t *devices, size_t max_devices, size_t *found_count);

/**
 * @brief Verify ES7210 microphone codec communication
 * 
 * Performs comprehensive verification of ES7210 codec:
 * - I2C communication test
 * - Chip ID verification
 * - Register read/write test
 * 
 * @return ESP_OK if codec is properly communicating, error code otherwise
 */
esp_err_t verify_es7210_communication(void);

/**
 * @brief Verify ES8311 speaker codec communication
 * 
 * Performs comprehensive verification of ES8311 codec:
 * - I2C communication test
 * - Chip ID verification
 * - Register read/write test
 * 
 * @return ESP_OK if codec is properly communicating, error code otherwise
 */
esp_err_t verify_es8311_communication(void);

/**
 * @brief Dump ES7210 codec registers for debugging
 * 
 * Reads and displays all important ES7210 registers.
 * Useful for diagnosing codec configuration issues.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t dump_es7210_registers(void);

/**
 * @brief Dump ES8311 codec registers for debugging
 * 
 * Reads and displays all important ES8311 registers.
 * Useful for diagnosing codec configuration issues.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t dump_es8311_registers(void);

/**
 * @brief Test I2C communication with specific device
 * 
 * Tests basic I2C communication with a specific device address.
 * Attempts probe operation and basic read/write if supported.
 * 
 * @param device_addr 7-bit I2C device address
 * @return ESP_OK if device responds, error code otherwise
 */
esp_err_t test_i2c_device(uint8_t device_addr);

/**
 * @brief Comprehensive I2C and codec diagnostic routine
 * 
 * Performs complete diagnostic sequence:
 * 1. I2C bus scan
 * 2. Codec identification
 * 3. Communication verification
 * 4. Register dumps (if codecs found)
 * 
 * This is the main diagnostic function to call when troubleshooting
 * audio codec issues.
 * 
 * @return ESP_OK if all diagnostics pass, error code otherwise
 */
esp_err_t run_i2c_diagnostics(void);

/**
 * @brief Read register from ES7210 codec
 * 
 * @param reg_addr Register address to read
 * @param data Pointer to store read value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t es7210_read_reg(uint8_t reg_addr, uint8_t *data);

/**
 * @brief Write register to ES7210 codec
 * 
 * @param reg_addr Register address to write
 * @param data Value to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t es7210_write_reg(uint8_t reg_addr, uint8_t data);

/**
 * @brief Read register from ES8311 codec
 * 
 * @param reg_addr Register address to read
 * @param data Pointer to store read value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t es8311_read_reg(uint8_t reg_addr, uint8_t *data);

/**
 * @brief Write register to ES8311 codec
 * 
 * @param reg_addr Register address to write
 * @param data Value to write
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t es8311_write_reg(uint8_t reg_addr, uint8_t data);

/**
 * @brief Check if I2C debug utilities are initialized
 * 
 * @return true if initialized, false otherwise
 */
bool i2c_debug_is_initialized(void);

/**
 * @brief Enable/disable verbose debug output
 * 
 * @param enable true to enable verbose output, false to disable
 */
void i2c_debug_set_verbose(bool enable);

#ifdef __cplusplus
}
#endif