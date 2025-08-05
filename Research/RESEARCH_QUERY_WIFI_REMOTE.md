# Research Query: ESP32-P4 + ESP32-C6 WiFi Remote Integration

## Problem Statement
We need working examples and solutions for integrating ESP32-P4 (main processor) with ESP32-C6 (WiFi coprocessor) using the esp_wifi_remote and esp_hosted components. The build is failing with type compatibility errors between esp_wifi_remote v0.5.x and ESP-IDF v5.4.

## Specific Error
```
error: unknown type name 'wifi_tx_rate_config_t'; did you mean 'wifi_init_config_t'?
```

## Research Requirements

### 1. Working Examples
Find GitHub repositories or projects that successfully use:
- ESP32-P4 as main processor (no native WiFi)
- ESP32-C6 as WiFi coprocessor via SDIO
- esp_wifi_remote component (v0.5.x or compatible)
- esp_hosted component for SDIO transport
- ESP-IDF v5.4 or v5.3

### 2. Component Compatibility
Identify:
- Which version combinations of esp_wifi_remote and ESP-IDF work together
- Any patches or workarounds for the wifi_tx_rate_config_t type error
- Alternative component versions that are known to work

### 3. Configuration Examples
Look for:
- Complete sdkconfig files that properly disable native WiFi
- Kconfig settings for dual-chip WiFi setup
- CMakeLists.txt configurations that avoid symbol conflicts

### 4. SDIO Integration
Find examples showing:
- SDIO pin configurations for ESP32-P4
- Proper initialization sequence for WiFi remote
- Communication setup between P4 and C6

## Key Search Terms
- "ESP32-P4 ESP32-C6 WiFi remote"
- "esp_wifi_remote wifi_tx_rate_config_t error"
- "ESP32-P4 SDIO WiFi coprocessor"
- "esp_hosted esp_wifi_remote example"
- "ESP-IDF 5.4 esp_wifi_remote compatibility"

## Preferred Sources
1. GitHub repositories with working examples
2. Espressif official examples or documentation
3. ESP32 forum posts with solutions
4. Component issue trackers with fixes

## Expected Deliverables
1. Links to working example projects
2. Specific version combinations that work
3. Configuration files or patches needed
4. Step-by-step integration guide if available

## Additional Context
- Using Waveshare ESP32-P4-NANO board
- SDIO pins: CLK=36, CMD=37, D0=35, D1=34, D2=33, D3=48
- Need WiFi provisioning, mDNS, and UDP streaming
- Project uses LVGL for display management