#include "i2c_debug_utils.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "I2C_DEBUG";

// Static configuration
static i2c_debug_config_t s_debug_config = {0};
static bool s_initialized = false;

// Device handle storage
static i2c_master_dev_handle_t s_es7210_handle = NULL;
static i2c_master_dev_handle_t s_es8311_handle = NULL;

/**
 * @brief Initialize I2C debug utilities
 */
esp_err_t i2c_debug_init(const i2c_debug_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration pointer");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->i2c_bus_handle) {
        ESP_LOGE(TAG, "Invalid I2C bus handle");
        return ESP_ERR_INVALID_ARG;
    }

    s_debug_config = *config;
    s_initialized = true;

    // Create device handles for codecs if not already created
    if (!s_es7210_handle) {
        i2c_device_config_t es7210_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ES7210_I2C_ADDR,
            .scl_speed_hz = 100000, // 100kHz for reliability
        };
        
        esp_err_t ret = i2c_master_bus_add_device(s_debug_config.i2c_bus_handle, &es7210_cfg, &s_es7210_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add ES7210 device handle: %s", esp_err_to_name(ret));
        }
    }

    if (!s_es8311_handle) {
        i2c_device_config_t es8311_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ES8311_I2C_ADDR,
            .scl_speed_hz = 100000, // 100kHz for reliability
        };
        
        esp_err_t ret = i2c_master_bus_add_device(s_debug_config.i2c_bus_handle, &es8311_cfg, &s_es8311_handle);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to add ES8311 device handle: %s", esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "I2C debug utilities initialized");
    return ESP_OK;
}

/**
 * @brief Scan I2C bus for all devices
 */
esp_err_t i2c_scan_bus(i2c_device_info_t *devices, size_t max_devices, size_t *found_count)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!found_count) {
        ESP_LOGE(TAG, "found_count parameter is required");
        return ESP_ERR_INVALID_ARG;
    }

    *found_count = 0;
    
    ESP_LOGI(TAG, "=== I2C Bus Scan Starting ===");
    ESP_LOGI(TAG, "Scanning I2C addresses 0x%02X to 0x%02X...", I2C_SCAN_ADDR_MIN, I2C_SCAN_ADDR_MAX);
    
    for (uint8_t addr = I2C_SCAN_ADDR_MIN; addr <= I2C_SCAN_ADDR_MAX; addr++) {
        // Probe the device
        esp_err_t ret = i2c_master_probe(s_debug_config.i2c_bus_handle, addr, 100);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device found at address 0x%02X", addr);
            
            // Store device info if buffer provided
            if (devices && *found_count < max_devices) {
                devices[*found_count].address = addr;
                devices[*found_count].responsive = true;
                
                // Identify known devices
                if (addr == ES7210_I2C_ADDR) {
                    snprintf(devices[*found_count].device_name, sizeof(devices[*found_count].device_name), 
                             "ES7210 (MIC)");
                } else if (addr == ES8311_I2C_ADDR) {
                    snprintf(devices[*found_count].device_name, sizeof(devices[*found_count].device_name), 
                             "ES8311 (SPK)");
                } else {
                    snprintf(devices[*found_count].device_name, sizeof(devices[*found_count].device_name), 
                             "Unknown");
                }
            }
            
            (*found_count)++;
        } else if (s_debug_config.verbose_output) {
            ESP_LOGD(TAG, "No response at address 0x%02X", addr);
        }
        
        // Small delay to prevent bus flooding
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    ESP_LOGI(TAG, "=== I2C Bus Scan Complete ===");
    ESP_LOGI(TAG, "Total devices found: %zu", *found_count);
    
    // Specifically check for expected codecs
    bool es7210_found = false;
    bool es8311_found = false;
    
    for (size_t i = 0; i < *found_count; i++) {
        if (devices && devices[i].address == ES7210_I2C_ADDR) es7210_found = true;
        if (devices && devices[i].address == ES8311_I2C_ADDR) es8311_found = true;
    }
    
    // Direct probe if device info not available
    if (!devices) {
        es7210_found = (i2c_master_probe(s_debug_config.i2c_bus_handle, ES7210_I2C_ADDR, 100) == ESP_OK);
        es8311_found = (i2c_master_probe(s_debug_config.i2c_bus_handle, ES8311_I2C_ADDR, 100) == ESP_OK);
    }
    
    ESP_LOGI(TAG, "Expected codecs status:");
    ESP_LOGI(TAG, "  ES7210 (0x%02X): %s", ES7210_I2C_ADDR, es7210_found ? "FOUND" : "NOT FOUND");
    ESP_LOGI(TAG, "  ES8311 (0x%02X): %s", ES8311_I2C_ADDR, es8311_found ? "FOUND" : "NOT FOUND");
    
    if (!es7210_found || !es8311_found) {
        ESP_LOGW(TAG, "One or more expected codecs not found on I2C bus!");
        ESP_LOGW(TAG, "Check I2C wiring and codec power supply");
    }
    
    return ESP_OK;
}

/**
 * @brief Read register from ES7210 codec
 */
esp_err_t es7210_read_reg(uint8_t reg_addr, uint8_t *data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_es7210_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = i2c_master_transmit_receive(s_es7210_handle, &reg_addr, 1, data, 1, 1000);
    
    if (s_debug_config.verbose_output) {
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "ES7210 reg 0x%02X = 0x%02X", reg_addr, *data);
        } else {
            ESP_LOGD(TAG, "ES7210 reg 0x%02X read failed: %s", reg_addr, esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief Write register to ES7210 codec
 */
esp_err_t es7210_write_reg(uint8_t reg_addr, uint8_t data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_es7210_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t write_data[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(s_es7210_handle, write_data, 2, 1000);
    
    if (s_debug_config.verbose_output) {
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "ES7210 reg 0x%02X <- 0x%02X", reg_addr, data);
        } else {
            ESP_LOGD(TAG, "ES7210 reg 0x%02X write failed: %s", reg_addr, esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief Read register from ES8311 codec
 */
esp_err_t es8311_read_reg(uint8_t reg_addr, uint8_t *data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_es8311_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = i2c_master_transmit_receive(s_es8311_handle, &reg_addr, 1, data, 1, 1000);
    
    if (s_debug_config.verbose_output) {
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "ES8311 reg 0x%02X = 0x%02X", reg_addr, *data);
        } else {
            ESP_LOGD(TAG, "ES8311 reg 0x%02X read failed: %s", reg_addr, esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief Write register to ES8311 codec
 */
esp_err_t es8311_write_reg(uint8_t reg_addr, uint8_t data)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_es8311_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t write_data[2] = {reg_addr, data};
    esp_err_t ret = i2c_master_transmit(s_es8311_handle, write_data, 2, 1000);
    
    if (s_debug_config.verbose_output) {
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "ES8311 reg 0x%02X <- 0x%02X", reg_addr, data);
        } else {
            ESP_LOGD(TAG, "ES8311 reg 0x%02X write failed: %s", reg_addr, esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief Verify ES7210 microphone codec communication
 */
esp_err_t verify_es7210_communication(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "=== ES7210 Communication Verification ===");
    
    // Step 1: Basic I2C probe
    esp_err_t ret = i2c_master_probe(s_debug_config.i2c_bus_handle, ES7210_I2C_ADDR, 500);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES7210 not responding to I2C probe: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ ES7210 responds to I2C probe");
    
    // Step 2: Read chip ID registers
    uint8_t chip_id1, chip_id0, version;
    
    ret = es7210_read_reg(ES7210_CHIPID1_REG, &chip_id1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES7210 chip ID1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = es7210_read_reg(ES7210_CHIPID0_REG, &chip_id0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES7210 chip ID0: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = es7210_read_reg(ES7210_VERSION_REG, &version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES7210 version: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✓ ES7210 Chip ID: 0x%02X%02X, Version: 0x%02X", chip_id1, chip_id0, version);
    
    // Step 3: Verify expected chip ID
    if (chip_id1 != ES7210_EXPECTED_CHIP_ID) {
        ESP_LOGW(TAG, "⚠ Unexpected ES7210 chip ID1: 0x%02X (expected 0x%02X)", 
                 chip_id1, ES7210_EXPECTED_CHIP_ID);
        // Continue anyway, might be newer revision
    } else {
        ESP_LOGI(TAG, "✓ ES7210 chip ID verified");
    }
    
    // Step 4: Test register read/write capability
    uint8_t original_val;
    
    // Read original value from a safe register (reset register)
    ret = es7210_read_reg(ES7210_RESET_REG, &original_val);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ ES7210 register read test passed");
        ESP_LOGI(TAG, "  Reset register original value: 0x%02X", original_val);
    } else {
        ESP_LOGE(TAG, "ES7210 register read test failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "=== ES7210 Communication Verification PASSED ===");
    return ESP_OK;
}

/**
 * @brief Verify ES8311 speaker codec communication
 */
esp_err_t verify_es8311_communication(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "=== ES8311 Communication Verification ===");
    
    // Step 1: Basic I2C probe
    esp_err_t ret = i2c_master_probe(s_debug_config.i2c_bus_handle, ES8311_I2C_ADDR, 500);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 not responding to I2C probe: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "✓ ES8311 responds to I2C probe");
    
    // Step 2: Read chip ID registers
    uint8_t chip_id1, chip_id2, version;
    
    ret = es8311_read_reg(ES8311_CHIPID1_REG, &chip_id1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES8311 chip ID1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = es8311_read_reg(ES8311_CHIPID2_REG, &chip_id2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES8311 chip ID2: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = es8311_read_reg(ES8311_VERSION_REG, &version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ES8311 version: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "✓ ES8311 Chip ID: 0x%02X%02X, Version: 0x%02X", chip_id1, chip_id2, version);
    
    // Step 3: Verify expected chip ID
    if (chip_id1 != ES8311_EXPECTED_CHIP_ID) {
        ESP_LOGW(TAG, "⚠ Unexpected ES8311 chip ID1: 0x%02X (expected 0x%02X)", 
                 chip_id1, ES8311_EXPECTED_CHIP_ID);
        // Continue anyway, might be newer revision
    } else {
        ESP_LOGI(TAG, "✓ ES8311 chip ID verified");
    }
    
    // Step 4: Test register read/write capability
    uint8_t original_val;
    
    // Read original value from a safe register (reset register)
    ret = es8311_read_reg(ES8311_RESET_REG, &original_val);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✓ ES8311 register read test passed");
        ESP_LOGI(TAG, "  Reset register original value: 0x%02X", original_val);
    } else {
        ESP_LOGE(TAG, "ES8311 register read test failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "=== ES8311 Communication Verification PASSED ===");
    return ESP_OK;
}

/**
 * @brief Dump ES7210 codec registers for debugging
 */
esp_err_t dump_es7210_registers(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "=== ES7210 Register Dump ===");
    
    // Important ES7210 registers for debugging
    uint8_t registers[] = {
        ES7210_RESET_REG,           // 0x00 - Reset control
        ES7210_CLOCK_OFF_REG,       // 0x01 - Clock off
        ES7210_MAINCLK_REG,         // 0x02 - Main clock
        ES7210_MASTER_CLK_REG,      // 0x03 - Master clock
        ES7210_LRCK_DIVIDER_H,      // 0x04 - LRCK divider high
        ES7210_LRCK_DIVIDER_L,      // 0x05 - LRCK divider low  
        ES7210_POWER_DOWN_REG,      // 0x06 - Power down
        ES7210_OSR_REG,             // 0x07 - OSR
        ES7210_MODE_CONFIG_REG,     // 0x08 - Mode config
        ES7210_CHIPID1_REG,         // 0xFD - Chip ID 1
        ES7210_CHIPID0_REG,         // 0xFE - Chip ID 0
        ES7210_VERSION_REG          // 0xFF - Version
    };
    
    const char* register_names[] = {
        "RESET",
        "CLOCK_OFF", 
        "MAINCLK",
        "MASTER_CLK",
        "LRCK_DIV_H",
        "LRCK_DIV_L",
        "POWER_DOWN",
        "OSR",
        "MODE_CONFIG",
        "CHIP_ID1",
        "CHIP_ID0", 
        "VERSION"
    };
    
    bool any_failed = false;
    
    for (size_t i = 0; i < sizeof(registers); i++) {
        uint8_t value;
        esp_err_t ret = es7210_read_reg(registers[i], &value);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02X %-12s = 0x%02X (%d)", 
                     registers[i], register_names[i], value, value);
        } else {
            ESP_LOGE(TAG, "  0x%02X %-12s = READ FAILED (%s)", 
                     registers[i], register_names[i], esp_err_to_name(ret));
            any_failed = true;
        }
    }
    
    if (any_failed) {
        ESP_LOGW(TAG, "Some ES7210 registers could not be read - check I2C communication");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    ESP_LOGI(TAG, "=== ES7210 Register Dump Complete ===");
    return ESP_OK;
}

/**
 * @brief Dump ES8311 codec registers for debugging
 */
esp_err_t dump_es8311_registers(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "=== ES8311 Register Dump ===");
    
    // Important ES8311 registers for debugging
    uint8_t registers[] = {
        ES8311_RESET_REG,           // 0x00 - Reset control
        ES8311_CLK_MANAGER_REG,     // 0x01 - Clock manager
        ES8311_CLK_MANAGER2_REG,    // 0x02 - Clock manager 2
        ES8311_CLK_MANAGER3_REG,    // 0x03 - Clock manager 3
        ES8311_ADC_REG,             // 0x09 - ADC control
        ES8311_DAC_REG,             // 0x31 - DAC control
        ES8311_CHIPID1_REG,         // 0xFD - Chip ID 1
        ES8311_CHIPID2_REG,         // 0xFE - Chip ID 2
        ES8311_VERSION_REG          // 0xFF - Version
    };
    
    const char* register_names[] = {
        "RESET",
        "CLK_MGR",
        "CLK_MGR2", 
        "CLK_MGR3",
        "ADC_CTRL",
        "DAC_CTRL",
        "CHIP_ID1",
        "CHIP_ID2",
        "VERSION"
    };
    
    bool any_failed = false;
    
    for (size_t i = 0; i < sizeof(registers); i++) {
        uint8_t value;
        esp_err_t ret = es8311_read_reg(registers[i], &value);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  0x%02X %-12s = 0x%02X (%d)", 
                     registers[i], register_names[i], value, value);
        } else {
            ESP_LOGE(TAG, "  0x%02X %-12s = READ FAILED (%s)", 
                     registers[i], register_names[i], esp_err_to_name(ret));
            any_failed = true;
        }
    }
    
    if (any_failed) {
        ESP_LOGW(TAG, "Some ES8311 registers could not be read - check I2C communication");
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    ESP_LOGI(TAG, "=== ES8311 Register Dump Complete ===");
    return ESP_OK;
}

/**
 * @brief Test I2C communication with specific device
 */
esp_err_t test_i2c_device(uint8_t device_addr)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Testing I2C device at address 0x%02X...", device_addr);
    
    // Probe test
    esp_err_t ret = i2c_master_probe(s_debug_config.i2c_bus_handle, device_addr, 500);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device 0x%02X: Probe failed - %s", device_addr, esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Device 0x%02X: Probe successful", device_addr);
    
    // Try to identify device
    const char* device_name = "Unknown";
    if (device_addr == ES7210_I2C_ADDR) {
        device_name = "ES7210 (Microphone ADC)";
    } else if (device_addr == ES8311_I2C_ADDR) {
        device_name = "ES8311 (Speaker DAC)";
    }
    
    ESP_LOGI(TAG, "Device 0x%02X: Identified as %s", device_addr, device_name);
    
    return ESP_OK;
}

/**
 * @brief Comprehensive I2C and codec diagnostic routine
 */
esp_err_t run_i2c_diagnostics(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "I2C debug utilities not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "████████████████████████████████████████████████████████");
    ESP_LOGI(TAG, "██          ESP32-P4 I2C & CODEC DIAGNOSTICS          ██");
    ESP_LOGI(TAG, "████████████████████████████████████████████████████████");
    ESP_LOGI(TAG, "");
    
    esp_err_t overall_result = ESP_OK;
    
    // Step 1: I2C bus scan
    ESP_LOGI(TAG, "Step 1: I2C Bus Scan");
    ESP_LOGI(TAG, "─────────────────────");
    
    i2c_device_info_t devices[16];
    size_t found_count;
    esp_err_t ret = i2c_scan_bus(devices, sizeof(devices)/sizeof(devices[0]), &found_count);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus scan failed!");
        overall_result = ret;
    } else {
        ESP_LOGI(TAG, "I2C bus scan completed successfully");
    }
    
    ESP_LOGI(TAG, "");
    
    // Step 2: Codec verification
    ESP_LOGI(TAG, "Step 2: Codec Communication Verification");
    ESP_LOGI(TAG, "─────────────────────────────────────────");
    
    // Check ES7210 (microphone)
    ret = verify_es7210_communication();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES7210 verification FAILED");
        overall_result = ret;
    }
    
    ESP_LOGI(TAG, "");
    
    // Check ES8311 (speaker)
    ret = verify_es8311_communication();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 verification FAILED");
        overall_result = ret;
    }
    
    ESP_LOGI(TAG, "");
    
    // Step 3: Register dumps (only if codecs are working)
    if (overall_result == ESP_OK) {
        ESP_LOGI(TAG, "Step 3: Codec Register Dumps");
        ESP_LOGI(TAG, "─────────────────────────────");
        
        dump_es7210_registers();
        ESP_LOGI(TAG, "");
        dump_es8311_registers();
    } else {
        ESP_LOGI(TAG, "Step 3: Skipping register dumps due to communication failures");
    }
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "████████████████████████████████████████████████████████");
    
    if (overall_result == ESP_OK) {
        ESP_LOGI(TAG, "██                DIAGNOSTICS PASSED                 ██");
        ESP_LOGI(TAG, "██            All codecs are communicating!           ██");
    } else {
        ESP_LOGE(TAG, "██                DIAGNOSTICS FAILED                 ██");
        ESP_LOGE(TAG, "██          Check I2C wiring and power supply         ██");
    }
    
    ESP_LOGI(TAG, "████████████████████████████████████████████████████████");
    ESP_LOGI(TAG, "");
    
    return overall_result;
}

/**
 * @brief Check if I2C debug utilities are initialized
 */
bool i2c_debug_is_initialized(void)
{
    return s_initialized;
}

/**
 * @brief Enable/disable verbose debug output
 */
void i2c_debug_set_verbose(bool enable)
{
    s_debug_config.verbose_output = enable;
    ESP_LOGI(TAG, "Verbose output %s", enable ? "enabled" : "disabled");
}