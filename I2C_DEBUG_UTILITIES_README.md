# ESP32-P4 I2C Debug Utilities

## Overview

This document describes the comprehensive I2C debugging utilities implemented for the ESP32-P4 HowdyScreen project. These utilities help diagnose and troubleshoot I2C communication issues with the ES8311 (speaker) and ES7210 (microphone) audio codecs.

## Features

### 🔍 **Comprehensive I2C Bus Scanning**
- Scans all valid I2C addresses (0x08 to 0x78)
- Identifies known devices (ES8311 at 0x18, ES7210 at 0x40)
- Reports responsive devices with detailed information

### 🎯 **Codec Communication Verification**
- Verifies ES7210 microphone codec communication
- Verifies ES8311 speaker codec communication
- Reads and validates chip ID registers
- Tests register read/write operations

### 📋 **Register Dump Functionality**
- Complete register dumps for both codecs
- Formatted output with register names and descriptions
- Helps identify codec configuration issues

### ⚙️ **Configurable Debug Options**
- Verbose logging for detailed I2C operations
- Automatic scanning during initialization
- Optional register dumps for complete diagnostics
- Menuconfig integration for easy configuration

## File Structure

```
components/audio_processor/
├── include/
│   └── i2c_debug_utils.h          # Main header file
├── src/
│   ├── i2c_debug_utils.c          # Implementation
│   └── dual_i2s_manager.c         # Integration point
main/
├── i2c_diagnostic_example.c       # Usage examples
├── i2c_diagnostic_example.h       # Example header
└── Kconfig.projbuild              # Configuration options
```

## Configuration

### Menuconfig Options

Access via `idf.py menuconfig` → **HowdyScreen Configuration** → **Debug Settings**:

```
┌─ Debug Settings ─────────────────────────────────────┐
│ [*] Enable I2C Debug Utilities                       │
│ [ ] Enable Verbose I2C Debug Output                  │
│ [*] Auto-run I2C Scan on Startup                     │
│ [ ] Include Register Dumps in Auto Scan              │
└───────────────────────────────────────────────────────┘
```

#### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_I2C_DEBUG_ENABLED` | Enable I2C debug utilities | Y |
| `CONFIG_I2C_DEBUG_VERBOSE` | Verbose debug output | N |
| `CONFIG_I2C_DEBUG_AUTO_SCAN` | Auto-run scan on startup | Y |
| `CONFIG_I2C_DEBUG_REGISTER_DUMPS` | Include register dumps | N |

### Recommended Configurations

#### Development Mode
```c
CONFIG_I2C_DEBUG_ENABLED=y
CONFIG_I2C_DEBUG_VERBOSE=y
CONFIG_I2C_DEBUG_AUTO_SCAN=y
CONFIG_I2C_DEBUG_REGISTER_DUMPS=y
```

#### Production Mode
```c
CONFIG_I2C_DEBUG_ENABLED=y
CONFIG_I2C_DEBUG_VERBOSE=n
CONFIG_I2C_DEBUG_AUTO_SCAN=y
CONFIG_I2C_DEBUG_REGISTER_DUMPS=n
```

#### Minimal Mode
```c
CONFIG_I2C_DEBUG_ENABLED=n
```

## API Reference

### Initialization

```c
#include "i2c_debug_utils.h"

// Initialize debug utilities
i2c_debug_config_t config = {
    .i2c_bus_handle = bsp_i2c_get_handle(),
    .verbose_output = true,
    .scan_enabled = true,
    .codec_verification_enabled = true
};

esp_err_t ret = i2c_debug_init(&config);
```

### Core Functions

#### Bus Scanning
```c
// Scan I2C bus for devices
i2c_device_info_t devices[16];
size_t found_count;
esp_err_t ret = i2c_scan_bus(devices, 16, &found_count);
```

#### Codec Verification
```c
// Verify codec communication
esp_err_t ret1 = verify_es7210_communication();
esp_err_t ret2 = verify_es8311_communication();
```

#### Register Operations
```c
// Read/write codec registers
uint8_t value;
esp_err_t ret = es7210_read_reg(0xFD, &value);  // Chip ID
ret = es7210_write_reg(0x00, 0xFF);             // Reset
```

#### Register Dumps
```c
// Dump all important registers
esp_err_t ret1 = dump_es7210_registers();
esp_err_t ret2 = dump_es8311_registers();
```

#### Complete Diagnostics
```c
// Run comprehensive diagnostics
esp_err_t ret = run_i2c_diagnostics();
```

## Usage Examples

### Basic Usage

```c
#include "i2c_debug_utils.h"

void app_main(void) {
    // Initialize BSP
    bsp_i2c_init();
    
    // Configure debug utilities
    i2c_debug_config_t config = {
        .i2c_bus_handle = bsp_i2c_get_handle(),
        .verbose_output = false,
        .scan_enabled = true,
        .codec_verification_enabled = true
    };
    
    // Initialize and run diagnostics
    if (i2c_debug_init(&config) == ESP_OK) {
        run_i2c_diagnostics();
    }
}
```

### Manual Diagnostics

```c
// For troubleshooting during development
esp_err_t troubleshoot_i2c_issues(void) {
    ESP_LOGI("APP", "🔧 Starting I2C troubleshooting...");
    
    // Run comprehensive diagnostics
    esp_err_t ret = run_i2c_diagnostics();
    
    if (ret != ESP_OK) {
        ESP_LOGE("APP", "❌ I2C issues detected!");
        ESP_LOGE("APP", "Check hardware connections:");
        ESP_LOGE("APP", "- SDA: GPIO 7");
        ESP_LOGE("APP", "- SCL: GPIO 8"); 
        ESP_LOGE("APP", "- Power supply to codecs");
        ESP_LOGE("APP", "- Pull-up resistors (if external)");
    }
    
    return ret;
}
```

### Health Monitoring

```c
// Periodic health check in production
void codec_health_monitor_task(void *pvParameters) {
    while (1) {
        // Quick verification without full diagnostics
        bool es7210_ok = (verify_es7210_communication() == ESP_OK);
        bool es8311_ok = (verify_es8311_communication() == ESP_OK);
        
        if (!es7210_ok || !es8311_ok) {
            ESP_LOGW("MONITOR", "⚠️ Codec health issue detected");
            // Take corrective action...
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
    }
}
```

## Integration

### Automatic Integration

The I2C debug utilities are automatically integrated into the `dual_i2s_manager.c` file and will run during audio system initialization if enabled in menuconfig.

```c
// In dual_i2s_init() function
esp_err_t dual_i2s_init(const dual_i2s_config_t *config) {
    // ... BSP I2C initialization ...
    
    // I2C diagnostics run automatically here
    scan_i2c_devices();  // Uses comprehensive utilities if enabled
    
    // ... rest of audio initialization ...
}
```

### Manual Integration

You can also manually trigger diagnostics from your application:

```c
#include "i2c_diagnostic_example.h"

void app_main(void) {
    // Your initialization code...
    
    #ifdef DEBUG
    // Run comprehensive diagnostics in debug builds
    run_manual_i2c_diagnostics();
    #else
    // Quick health check in release builds
    quick_codec_health_check();
    #endif
    
    // Continue with application...
}
```

## Output Examples

### Successful Diagnostics
```
I (1234) I2C_DEBUG: ████████████████████████████████████████████████████████
I (1235) I2C_DEBUG: ██          ESP32-P4 I2C & CODEC DIAGNOSTICS          ██
I (1236) I2C_DEBUG: ████████████████████████████████████████████████████████

I (1237) I2C_DEBUG: Step 1: I2C Bus Scan
I (1238) I2C_DEBUG: ─────────────────────
I (1239) I2C_DEBUG: Device found at address 0x18
I (1240) I2C_DEBUG: Device found at address 0x40
I (1241) I2C_DEBUG: Total devices found: 2
I (1242) I2C_DEBUG: Expected codecs status:
I (1243) I2C_DEBUG:   ES7210 (0x40): FOUND
I (1244) I2C_DEBUG:   ES8311 (0x18): FOUND

I (1245) I2C_DEBUG: Step 2: Codec Communication Verification
I (1246) I2C_DEBUG: ─────────────────────────────────────────
I (1247) I2C_DEBUG: === ES7210 Communication Verification ===
I (1248) I2C_DEBUG: ✓ ES7210 responds to I2C probe
I (1249) I2C_DEBUG: ✓ ES7210 Chip ID: 0x7210, Version: 0x01
I (1250) I2C_DEBUG: ✓ ES7210 chip ID verified
I (1251) I2C_DEBUG: === ES7210 Communication Verification PASSED ===

I (1252) I2C_DEBUG: === ES8311 Communication Verification ===
I (1253) I2C_DEBUG: ✓ ES8311 responds to I2C probe
I (1254) I2C_DEBUG: ✓ ES8311 Chip ID: 0x8311, Version: 0x02
I (1255) I2C_DEBUG: ✓ ES8311 chip ID verified
I (1256) I2C_DEBUG: === ES8311 Communication Verification PASSED ===

I (1257) I2C_DEBUG: Step 3: Codec Register Dumps
I (1258) I2C_DEBUG: ─────────────────────────────
I (1259) I2C_DEBUG: === ES7210 Register Dump ===
I (1260) I2C_DEBUG:   0x00 RESET        = 0x32 (50)
I (1261) I2C_DEBUG:   0x01 CLOCK_OFF    = 0x00 (0)
I (1262) I2C_DEBUG:   0x02 MAINCLK      = 0x00 (0)
...

I (1270) I2C_DEBUG: ████████████████████████████████████████████████████████
I (1271) I2C_DEBUG: ██                DIAGNOSTICS PASSED                 ██
I (1272) I2C_DEBUG: ██            All codecs are communicating!           ██
I (1273) I2C_DEBUG: ████████████████████████████████████████████████████████
```

### Failed Diagnostics
```
E (1234) I2C_DEBUG: ████████████████████████████████████████████████████████
E (1235) I2C_DEBUG: ██                DIAGNOSTICS FAILED                 ██
E (1236) I2C_DEBUG: ██          Check I2C wiring and power supply         ██
E (1237) I2C_DEBUG: ████████████████████████████████████████████████████████

E (1238) I2C_DEBUG: Expected codecs status:
E (1239) I2C_DEBUG:   ES7210 (0x40): NOT FOUND
E (1240) I2C_DEBUG:   ES8311 (0x18): FOUND
```

## Troubleshooting Guide

### No Devices Found

**Symptoms:**
```
W (1234) I2C_DEBUG: ❌ No I2C devices found on the bus
W (1235) I2C_DEBUG: Check I2C wiring and codec power supply
```

**Solutions:**
1. Check I2C wiring (SDA: GPIO 7, SCL: GPIO 8)
2. Verify codec power supply (3.3V)
3. Check pull-up resistors (if external)
4. Verify BSP I2C initialization

### Codec Not Responding

**Symptoms:**
```
E (1234) I2C_DEBUG: ES7210 not responding to I2C probe: ESP_ERR_TIMEOUT
```

**Solutions:**
1. Check codec-specific power supply
2. Verify codec reset sequence
3. Check I2C address (ES7210: 0x40, ES8311: 0x18)
4. Add delays after power-up

### Register Read Failures

**Symptoms:**
```
E (1234) I2C_DEBUG: Failed to read ES7210 chip ID: ESP_ERR_INVALID_RESPONSE
```

**Solutions:**
1. Verify codec initialization sequence
2. Check MCLK generation (required for some registers)
3. Ensure codec is in correct state
4. Try software reset before reading

### Unexpected Chip IDs

**Symptoms:**
```
W (1234) I2C_DEBUG: ⚠ Unexpected ES7210 chip ID1: 0x99 (expected 0x32)
```

**Solutions:**
1. Verify you have the correct codec variant
2. Check if it's a newer revision
3. Update expected chip IDs if necessary
4. Verify I2C communication integrity

## Performance Considerations

### Memory Usage
- Static memory: ~2KB for debug utilities
- Dynamic memory: ~1KB during diagnostics
- Stack usage: ~512 bytes per function call

### Timing Impact
- Basic scan: ~100ms
- Full diagnostics: ~500ms
- Register dumps: ~200ms additional

### Recommendations
- Use basic scan in production
- Enable full diagnostics only in debug builds
- Avoid verbose logging in production
- Consider running diagnostics in separate task

## Best Practices

### Development
1. Enable all debug options during development
2. Use comprehensive diagnostics for troubleshooting
3. Log all I2C operations for analysis
4. Test with different codec configurations

### Production
1. Disable verbose logging
2. Use quick health checks instead of full scans
3. Implement error recovery based on diagnostic results
4. Monitor codec health periodically

### Debugging Workflow
1. **Start with basic scan** to identify devices
2. **Run codec verification** if devices found
3. **Check register dumps** for configuration issues
4. **Use verbose logging** for detailed analysis
5. **Implement fixes** based on findings

## Dependencies

### Required Components
- `driver` (I2C master driver)
- `esp_timer` (timing functions)
- `freertos` (task delay)
- `waveshare__esp32_p4_wifi6_touch_lcd_xc` (BSP)

### Optional Components
- `esp_log` (logging - highly recommended)

### Hardware Requirements
- ESP32-P4 with Waveshare ESP32-P4-WIFI6-Touch-LCD-XC board
- ES8311 speaker codec at I2C address 0x18
- ES7210 microphone codec at I2C address 0x40
- I2C pull-up resistors (usually on-board)

## Version History

### v1.0.0 - Initial Release
- Basic I2C bus scanning
- Codec communication verification
- Register dump functionality
- Menuconfig integration
- Automatic integration with dual_i2s_manager

## Support

For issues or questions related to I2C debugging utilities:

1. Check the troubleshooting guide above
2. Enable verbose logging for detailed analysis
3. Review hardware connections and power supply
4. Consult the ESP32-P4 and codec datasheets
5. Use the example functions for testing

## License

This code is part of the ESP32-P4 HowdyScreen project and follows the same licensing terms.