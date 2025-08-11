#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C Diagnostic Example Functions for ESP32-P4 HowdyScreen
 * 
 * This header provides example functions demonstrating how to use the
 * comprehensive I2C debugging utilities in your application.
 */

/**
 * @brief Run complete I2C diagnostics manually
 * 
 * Performs comprehensive I2C bus scan, codec verification, and register dumps.
 * Use this function when troubleshooting I2C communication issues.
 * 
 * @return ESP_OK if diagnostics passed, error code otherwise
 */
esp_err_t run_manual_i2c_diagnostics(void);

/**
 * @brief Quick codec health check
 * 
 * Performs basic codec communication verification without full diagnostics.
 * Suitable for regular health monitoring in production.
 * 
 * @return ESP_OK if both codecs are responding, error code otherwise
 */
esp_err_t quick_codec_health_check(void);

/**
 * @brief Test codec register access
 * 
 * Demonstrates register dump functionality and specific register reads.
 * Useful for debugging codec configuration issues.
 */
void test_codec_register_access(void);

/**
 * @brief Example main application integration
 * 
 * Shows different ways to integrate I2C diagnostics into your application:
 * - Automatic diagnostics during initialization
 * - Periodic health checks
 * - On-demand troubleshooting
 */
void example_main_integration(void);

#ifdef __cplusplus
}
#endif