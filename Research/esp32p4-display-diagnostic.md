# ESP32-P4 HowdyScreen Display Diagnostic Guide

## Project Path
```
/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen
```

## Critical Issues to Check

### 1. MIPI-DSI Escape Clock Frequency (CRITICAL)
**Issue**: ESP32-P4 MIPI-DSI displays often fail with default 10MHz escape clock
**Fix Required**: 
```bash
# Check in ESP-IDF components
grep -r "MIPI_DSI_DEFAULT_ESCAPE_CLOCK_FREQ_MHZ" .
# If found with value 10, change to 20
```

**Files to check**:
- `managed_components/espressif__esp_lcd/src/esp_lcd_mipi_dsi_bus.c`
- `components/*/esp_lcd_mipi_dsi_bus.c`
- Any custom display driver files

### 2. Power Sequencing Delay
**Check main.c/main.cpp for initialization sequence**:
```c
// Look for setup() or app_main()
// Should have delay before display init:
void app_main() {
    vTaskDelay(pdMS_TO_TICKS(500)); // Add if missing
    display_init();
}
```

### 3. Display Pin Configuration
**Check these files for pin definitions**:
- `main/main.c` or `main/main.cpp`
- `components/*/display_config.h`
- `sdkconfig` (search for GPIO configurations)

**Key pins to verify**:
```c
// Common ESP32-P4 display pins
#define LCD_RST_GPIO    GPIO_NUM_27  // Reset pin
#define LCD_BL_GPIO     GPIO_NUM_26  // Backlight PWM
#define LCD_PWR_GPIO    GPIO_NUM_XX  // Power enable (if used)
```

### 4. Backlight Control
**Search for backlight initialization**:
```bash
grep -r "backlight\|PWM\|ledc" main/ components/
```

**Should find code like**:
```c
// Backlight PWM setup
ledc_timer_config_t ledc_timer = {
    .duty_resolution = LEDC_TIMER_10_BIT,
    .freq_hz = 5000,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .timer_num = LEDC_TIMER_0
};
```

### 5. Display Driver Initialization
**Check for proper MIPI-DSI initialization**:
```bash
# Look for display init functions
grep -r "mipi_dsi\|dsi_bus\|display_init\|lcd_init" main/ components/
```

### 6. SDL Configuration Check
**Important sdkconfig entries to verify**:
```bash
grep -E "SPIRAM|PSRAM|MIPI|DSI|LCD|DISPLAY" sdkconfig
```

**Should include**:
- `CONFIG_SPIRAM_MODE_OCT=y` (for Octal PSRAM if used)
- `CONFIG_SPIRAM_SPEED_120M=y` (if supported)
- MIPI-DSI related configs

### 7. Build Configuration
**Check CMakeLists.txt files**:
```bash
# Main CMakeLists.txt
cat CMakeLists.txt

# Component CMakeLists.txt files
find components -name "CMakeLists.txt" -exec cat {} \;
```

### 8. Error Messages in Code
**Search for error handling**:
```bash
grep -r "ESP_LOG\|ESP_ERROR\|printf" main/ | grep -i "display\|lcd\|screen\|dsi"
```

## Diagnostic Commands to Run

### Step 1: Check Project Structure
```bash
cd /Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen
find . -name "*.c" -o -name "*.cpp" -o -name "*.h" | grep -E "(main|display|lcd|screen)" | head -20
```

### Step 2: Examine Main Application
```bash
# Check main app file
cat main/main.c || cat main/main.cpp | head -100
```

### Step 3: Check Display Component
```bash
# List display-related components
ls -la components/
find components -name "*display*" -o -name "*lcd*" -o -name "*screen*"
```

### Step 4: Verify ESP-IDF Version
```bash
# Check IDF version in use
grep "IDF_VERSION" sdkconfig || idf.py --version
```

### Step 5: Check for Known Issues
```bash
# Look for TODO or FIXME comments about display
grep -r "TODO\|FIXME\|XXX\|HACK" . | grep -i "display\|lcd\|screen"
```

## Quick Fixes to Try

### Fix 1: Add Power Delay
```c
// In app_main() or setup(), add at the very beginning:
vTaskDelay(pdMS_TO_TICKS(1000)); // 1 second delay for power stabilization
```

### Fix 2: Force Backlight On
```c
// After display init, force backlight to 100%
gpio_set_direction(LCD_BL_GPIO, GPIO_MODE_OUTPUT);
gpio_set_level(LCD_BL_GPIO, 1); // or 0 if active low
```

### Fix 3: Add Display Reset Sequence
```c
// Proper reset sequence
gpio_set_direction(LCD_RST_GPIO, GPIO_MODE_OUTPUT);
gpio_set_level(LCD_RST_GPIO, 0);
vTaskDelay(pdMS_TO_TICKS(10));
gpio_set_level(LCD_RST_GPIO, 1);
vTaskDelay(pdMS_TO_TICKS(120));
```

### Fix 4: Enable Debug Logging
```c
// Add to sdkconfig.defaults
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
```

## Testing Sequence

1. **Flash and Monitor**:
```bash
idf.py -p /dev/cu.usbserial-* flash monitor
```

2. **Check Boot Messages**: Look for:
   - Display initialization messages
   - Error messages about MIPI-DSI
   - GPIO configuration errors
   - Memory allocation failures

3. **Physical Checks**:
   - Is backlight LED on?
   - Any flicker during boot?
   - Does reset button change anything?

## Common Error Patterns

### Pattern 1: "spi_master: check_trans_valid" Error
- Indicates SPI configuration mismatch
- Check SPI mode and frequency settings

### Pattern 2: "ESP_ERR_NOT_FOUND" 
- Component not properly registered
- Check CMakeLists.txt includes

### Pattern 3: Watchdog Timer Reset
- Display init taking too long
- Add watchdog feed in init sequence

### Pattern 4: Brown-out Reset
- Power supply insufficient
- Check USB cable quality and power source

## Next Steps

1. Run diagnostics in order
2. Share findings from error messages
3. Provide main.c/cpp content for review
4. Check sdkconfig for display settings
5. Verify component structure

## Notes for Claude Code

When examining this project:
1. Focus on initialization sequence timing
2. Verify all GPIO assignments match hardware
3. Check for ESP32-P4 specific MIPI-DSI requirements
4. Look for missing error handling
5. Ensure proper component registration in CMake

The blank screen is most likely due to:
- Incorrect MIPI-DSI escape clock (10MHz vs 20MHz)
- Missing power sequencing delay
- Incorrect backlight GPIO or polarity
- Display not properly reset before initialization