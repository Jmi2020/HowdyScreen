/**
 * @file i2c_diagnostic_example.c
 * @brief Example integration of I2C debug utilities into ESP32-P4 HowdyScreen project
 * 
 * This file demonstrates how to use the comprehensive I2C debugging utilities
 * for diagnosing audio codec communication issues.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "i2c_debug_utils.h"

static const char *TAG = "I2C_DIAGNOSTIC";

/**
 * @brief Manual I2C diagnostic function
 * 
 * Call this function from your main application when you need to manually
 * trigger comprehensive I2C diagnostics. Useful for debugging codec issues
 * during development.
 * 
 * @return ESP_OK if diagnostics completed successfully, error code otherwise
 */
esp_err_t run_manual_i2c_diagnostics(void)
{
    ESP_LOGI(TAG, "🚀 Manual I2C Diagnostics Triggered");
    ESP_LOGI(TAG, "=====================================");
    
    // Initialize BSP I2C if not already done
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get I2C bus handle
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (!i2c_handle) {
        ESP_LOGE(TAG, "I2C handle not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure I2C debug utilities
    i2c_debug_config_t debug_config = {
        .i2c_bus_handle = i2c_handle,
        .verbose_output = true,  // Enable verbose output for manual diagnostics
        .scan_enabled = true,
        .codec_verification_enabled = true
    };
    
    // Initialize I2C debug utilities
    ret = i2c_debug_init(&debug_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C debug utilities: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Run comprehensive diagnostics
    ESP_LOGI(TAG, "Running full I2C diagnostics with register dumps...");
    ret = run_i2c_diagnostics();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "🎉 Manual I2C diagnostics completed successfully!");
        ESP_LOGI(TAG, "✅ All codecs are communicating properly");
    } else {
        ESP_LOGE(TAG, "❌ I2C diagnostics found issues");
        ESP_LOGE(TAG, "Check hardware connections and power supply");
    }
    
    return ret;
}

/**
 * @brief Quick codec verification function
 * 
 * Performs basic codec communication verification without full diagnostics.
 * Faster than full diagnostics, suitable for regular health checks.
 * 
 * @return ESP_OK if both codecs are responding, error code otherwise
 */
esp_err_t quick_codec_health_check(void)
{
    ESP_LOGI(TAG, "🏥 Quick Codec Health Check");
    ESP_LOGI(TAG, "==============================");
    
    // Initialize BSP I2C if not already done
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Get I2C bus handle
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (!i2c_handle) {
        ESP_LOGE(TAG, "I2C handle not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure I2C debug utilities (minimal config for speed)
    i2c_debug_config_t debug_config = {
        .i2c_bus_handle = i2c_handle,
        .verbose_output = false,  // Disable verbose for quick check
        .scan_enabled = true,
        .codec_verification_enabled = true
    };
    
    // Initialize I2C debug utilities
    ret = i2c_debug_init(&debug_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C debug utilities: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Quick verification of both codecs
    ESP_LOGI(TAG, "Checking ES7210 (microphone codec)...");
    ret = verify_es7210_communication();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ES7210 health check failed");
        return ret;
    }
    ESP_LOGI(TAG, "✅ ES7210 is healthy");
    
    ESP_LOGI(TAG, "Checking ES8311 (speaker codec)...");
    ret = verify_es8311_communication();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ ES8311 health check failed");
        return ret;
    }
    ESP_LOGI(TAG, "✅ ES8311 is healthy");
    
    ESP_LOGI(TAG, "🎉 Quick health check passed - both codecs are responding!");
    return ESP_OK;
}

/**
 * @brief Test specific codec registers
 * 
 * Demonstrates how to use the register dump functions to debug
 * specific codec configuration issues.
 */
void test_codec_register_access(void)
{
    ESP_LOGI(TAG, "🧪 Testing Codec Register Access");
    ESP_LOGI(TAG, "=================================");
    
    // Initialize I2C debug utilities first
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BSP I2C: %s", esp_err_to_name(ret));
        return;
    }
    
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (!i2c_handle) {
        ESP_LOGE(TAG, "I2C handle not available");
        return;
    }
    
    i2c_debug_config_t debug_config = {
        .i2c_bus_handle = i2c_handle,
        .verbose_output = true,
        .scan_enabled = true,
        .codec_verification_enabled = true
    };
    
    if (i2c_debug_init(&debug_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C debug utilities");
        return;
    }
    
    // Test ES7210 register dumps
    ESP_LOGI(TAG, "📋 Dumping ES7210 registers:");
    ret = dump_es7210_registers();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ES7210 register dump completed");
    } else {
        ESP_LOGW(TAG, "⚠️ ES7210 register dump failed");
    }
    
    ESP_LOGI(TAG, "");
    
    // Test ES8311 register dumps
    ESP_LOGI(TAG, "📋 Dumping ES8311 registers:");
    ret = dump_es8311_registers();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ ES8311 register dump completed");
    } else {
        ESP_LOGW(TAG, "⚠️ ES8311 register dump failed");
    }
    
    // Example of reading specific registers
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔍 Reading specific registers:");
    
    uint8_t es7210_chip_id, es8311_chip_id;
    
    if (es7210_read_reg(0xFD, &es7210_chip_id) == ESP_OK) {
        ESP_LOGI(TAG, "ES7210 Chip ID: 0x%02X", es7210_chip_id);
    } else {
        ESP_LOGW(TAG, "Failed to read ES7210 chip ID");
    }
    
    if (es8311_read_reg(0xFD, &es8311_chip_id) == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 Chip ID: 0x%02X", es8311_chip_id);
    } else {
        ESP_LOGW(TAG, "Failed to read ES8311 chip ID");
    }
}

/**
 * @brief Example usage in main application
 * 
 * This shows how you can integrate the I2C diagnostics into your main
 * application initialization or as debugging commands.
 */
void example_main_integration(void)
{
    ESP_LOGI(TAG, "🚀 ESP32-P4 HowdyScreen I2C Diagnostics Example");
    ESP_LOGI(TAG, "================================================");
    
    // Option 1: Run full diagnostics during initialization
    // (Good for development and troubleshooting)
    #ifdef CONFIG_I2C_DEBUG_ENABLED
    if (CONFIG_I2C_DEBUG_AUTO_SCAN) {
        ESP_LOGI(TAG, "Running automatic I2C diagnostics...");
        run_manual_i2c_diagnostics();
    }
    #endif
    
    // Option 2: Run periodic health checks
    // (Good for production monitoring)
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Performing periodic codec health check...");
    quick_codec_health_check();
    
    // Option 3: Debug specific issues
    // (Good when troubleshooting specific problems)
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Testing detailed register access...");
    test_codec_register_access();
    
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🎉 I2C diagnostics example completed!");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "💡 Usage Tips:");
    ESP_LOGI(TAG, "- Use run_manual_i2c_diagnostics() for complete troubleshooting");
    ESP_LOGI(TAG, "- Use quick_codec_health_check() for regular monitoring");
    ESP_LOGI(TAG, "- Use register dumps when debugging specific codec issues");
    ESP_LOGI(TAG, "- Enable CONFIG_I2C_DEBUG_VERBOSE for detailed I2C operation logs");
}