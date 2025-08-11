/*
 * Comprehensive Hardware Diagnostic for ESP32-P4 Display
 * Tests each component individually with detailed logging
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "hal/gpio_hal.h"

static const char *TAG = "HW_DIAGNOSTIC";

// Pin definitions based on BSP
#define BACKLIGHT_GPIO 26
#define LCD_RESET_GPIO 27
#define I2C_SDA_GPIO 7
#define I2C_SCL_GPIO 8
#define I2C_PORT I2C_NUM_0

// Touch controller I2C address
#define TOUCH_I2C_ADDR 0x5A  // CST9217

void test_gpio_pin(int gpio_num, const char* pin_name)
{
    ESP_LOGI(TAG, "=== Testing GPIO%d (%s) ===", gpio_num, pin_name);
    
    // Configure as output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << gpio_num),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  FAILED to configure GPIO%d: %s", gpio_num, esp_err_to_name(ret));
        return;
    }
    
    // Test HIGH and LOW states
    ESP_LOGI(TAG, "  Setting GPIO%d HIGH", gpio_num);
    gpio_set_level(gpio_num, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "  Setting GPIO%d LOW", gpio_num);
    gpio_set_level(gpio_num, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Read back the value
    int level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "  Read back level: %d", level);
    
    // Configure as input to check if it's being pulled by external circuit
    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio_num, GPIO_FLOATING);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "  Floating input level: %d", level);
    
    // Test with pull-up
    gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(100));
    level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "  Pull-up level: %d", level);
    
    // Test with pull-down
    gpio_set_pull_mode(gpio_num, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(100));
    level = gpio_get_level(gpio_num);
    ESP_LOGI(TAG, "  Pull-down level: %d", level);
    
    ESP_LOGI(TAG, "  GPIO%d test complete\n", gpio_num);
}

void scan_i2c_bus(void)
{
    ESP_LOGI(TAG, "=== I2C Bus Scan ===");
    
    // Use new I2C driver API (compatible with BSP)
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "  FAILED to create I2C master bus: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "  Scanning I2C bus on SDA=%d, SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);
    
    int devices_found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        // Create temporary I2C device for probing
        i2c_device_config_t dev_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        
        i2c_master_dev_handle_t dev_handle;
        ret = i2c_master_bus_add_device(i2c_bus, &dev_config, &dev_handle);
        if (ret != ESP_OK) {
            continue; // Skip this address if device creation fails
        }
        
        // Try to probe the device with a simple transaction
        uint8_t dummy_data = 0;
        ret = i2c_master_transmit(dev_handle, &dummy_data, 1, 50);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at address 0x%02X", addr);
            devices_found++;
            
            if (addr == TOUCH_I2C_ADDR) {
                ESP_LOGI(TAG, "    -> This is the CST9217 touch controller!");
            }
        }
        
        i2c_master_bus_rm_device(dev_handle);
    }
    
    if (devices_found == 0) {
        ESP_LOGW(TAG, "  No I2C devices found!");
    } else {
        ESP_LOGI(TAG, "  Total devices found: %d", devices_found);
    }
    
    i2c_del_master_bus(i2c_bus);
    ESP_LOGI(TAG, "  I2C scan complete\n");
}

void test_backlight_pwm(void)
{
    ESP_LOGI(TAG, "=== Backlight PWM Test ===");
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << BACKLIGHT_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    ESP_LOGI(TAG, "  Testing different backlight levels...");
    
    // Test with software PWM at different duty cycles
    for (int duty = 0; duty <= 100; duty += 25) {
        ESP_LOGI(TAG, "  Duty cycle: %d%%", duty);
        
        for (int cycle = 0; cycle < 100; cycle++) {
            // Active LOW - invert duty cycle
            if (cycle < duty) {
                gpio_set_level(BACKLIGHT_GPIO, 0); // ON
            } else {
                gpio_set_level(BACKLIGHT_GPIO, 1); // OFF
            }
            vTaskDelay(pdMS_TO_TICKS(1)); // 1ms per cycle = 1kHz PWM
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Show each level for 1 second
    }
    
    ESP_LOGI(TAG, "  PWM test complete\n");
}

void check_power_rails(void)
{
    ESP_LOGI(TAG, "=== Power Rail Check ===");
    ESP_LOGI(TAG, "  Note: Cannot directly measure voltages without ADC");
    ESP_LOGI(TAG, "  Checking GPIO states that might indicate power issues...");
    
    // Check if certain GPIOs are stuck high/low which might indicate power issues
    int test_gpios[] = {26, 27, 7, 8, 36, 37, 35, 34, 33, 48, 47};
    const char* gpio_names[] = {"BACKLIGHT", "LCD_RESET", "I2C_SDA", "I2C_SCL", 
                                "SDIO_CLK", "SDIO_CMD", "SDIO_D0", "SDIO_D1", 
                                "SDIO_D2", "SDIO_D3", "C6_RESET"};
    
    for (int i = 0; i < sizeof(test_gpios)/sizeof(test_gpios[0]); i++) {
        gpio_set_direction(test_gpios[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(test_gpios[i], GPIO_FLOATING);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        int level = gpio_get_level(test_gpios[i]);
        ESP_LOGI(TAG, "  GPIO%d (%s): %s", test_gpios[i], gpio_names[i], 
                 level ? "HIGH" : "LOW");
    }
    
    ESP_LOGI(TAG, "  Power check complete\n");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-P4 Display Hardware Diagnostic ===");
    ESP_LOGI(TAG, "This will test each hardware component individually");
    
    // Print system info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-P4 with %d cores, silicon revision v%d.%d", 
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s\n", esp_get_idf_version());
    
    // Test 1: Power rails
    check_power_rails();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 2: Backlight GPIO
    ESP_LOGI(TAG, "=== BACKLIGHT TEST (GPIO26) ===");
    ESP_LOGI(TAG, "Watch the display - backlight should turn on/off");
    test_gpio_pin(BACKLIGHT_GPIO, "BACKLIGHT");
    
    // Leave backlight in known state (ON for active low)
    gpio_set_direction(BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BACKLIGHT_GPIO, 0); // Active LOW = ON
    ESP_LOGI(TAG, "Backlight left ON (GPIO26 = LOW)");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 3: LCD Reset GPIO
    ESP_LOGI(TAG, "=== LCD RESET TEST (GPIO27) ===");
    test_gpio_pin(LCD_RESET_GPIO, "LCD_RESET");
    
    // Perform proper reset sequence
    gpio_set_direction(LCD_RESET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_RESET_GPIO, 0); // Assert reset
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RESET_GPIO, 1); // Release reset
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "LCD reset sequence complete");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 4: I2C bus scan
    scan_i2c_bus();
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Test 5: Backlight PWM
    test_backlight_pwm();
    
    // Final backlight test - rapid blinking
    ESP_LOGI(TAG, "=== FINAL TEST: Rapid Backlight Blinking ===");
    ESP_LOGI(TAG, "If you see the backlight blinking, GPIO control works");
    
    gpio_set_direction(BACKLIGHT_GPIO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 20; i++) {
        gpio_set_level(BACKLIGHT_GPIO, i % 2); // Toggle between 0 and 1
        vTaskDelay(pdMS_TO_TICKS(250));
        ESP_LOGI(TAG, "Blink %d/20 - Backlight %s", i+1, (i % 2) ? "OFF" : "ON");
    }
    
    // Leave backlight ON
    gpio_set_level(BACKLIGHT_GPIO, 0);
    
    ESP_LOGI(TAG, "=== DIAGNOSTIC COMPLETE ===");
    ESP_LOGI(TAG, "Summary:");
    ESP_LOGI(TAG, "- If backlight never turned on: Check display power connection");
    ESP_LOGI(TAG, "- If no I2C devices found: Check I2C connections or touch controller");
    ESP_LOGI(TAG, "- If GPIOs stuck HIGH/LOW: Possible short circuit or power issue");
    ESP_LOGI(TAG, "- Check serial log above for any FAILED messages");
    
    // Keep alive
    while (1) {
        ESP_LOGI(TAG, "Diagnostic idle... Free heap: %lu", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}