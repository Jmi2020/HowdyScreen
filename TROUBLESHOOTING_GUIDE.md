# ESP32-P4 HowdyScreen Troubleshooting Guide

## Overview

This comprehensive troubleshooting guide covers common integration issues, system diagnostics, error code references, and performance optimization tips for the ESP32-P4 HowdyScreen project.

## Table of Contents

1. [Common Integration Issues](#common-integration-issues)
2. [System Diagnostics and Monitoring](#system-diagnostics-and-monitoring)
3. [Error Code Reference](#error-code-reference)
4. [Performance Optimization](#performance-optimization)
5. [Hardware Troubleshooting](#hardware-troubleshooting)
6. [Network Connectivity Issues](#network-connectivity-issues)
7. [Audio System Problems](#audio-system-problems)
8. [Display and UI Issues](#display-and-ui-issues)

---

## Common Integration Issues

### Phase 6A Deployment Issues (RESOLVED - August 5, 2025)

#### Critical Runtime Crash: LVGL Memory Allocation Error

**Symptoms**:
```
Guru Meditation Error: Core 0 panic'ed (Load access fault)
MEPC: 0x4002c120 RA: 0x4002c28a (search_suitable_block at lv_tlsf.c:563)
```

**Root Cause**: UI manager attempting to create LVGL objects before LVGL library initialization.

**Solution**:
```c
// In main application - CORRECT initialization sequence:
lv_display_t *display = bsp_display_start();        // Initialize LVGL first
ESP_ERROR_CHECK(bsp_display_backlight_on());        // Enable display backlight
ESP_ERROR_CHECK(ui_manager_init());                 // Then create UI objects

// Add BSP dependency in main/CMakeLists.txt:
REQUIRES driver esp_timer esp_event ui_manager wifi_manager 
         websocket_client audio_processor esp32_p4_wifi6_touch_lcd_xc
```

#### Display Black Screen Issue

**Symptoms**: 
- Display initialization logs show success
- Screen remains completely black
- Touch controller initializes properly

**Root Cause**: Backlight not enabled after display initialization.

**Solution**:
```c
// After bsp_display_start(), always call:
ESP_ERROR_CHECK(bsp_display_backlight_on());

// Log output should show:
// I ESP32_P4_XC: Setting LCD backlight: 100%
```

#### WiFi Connection Using Wrong Credentials

**Symptoms**:
```
I WiFiManager: Connecting to SSID: YOUR_WIFI_SSID
E WiFiManager: Failed to connect to SSID: YOUR_WIFI_SSID
```

**Root Cause**: WiFi manager using hardcoded placeholder credentials instead of menuconfig values.

**Solution**:
```c
// In components/wifi_manager/src/wifi_manager.c:
esp_err_t wifi_manager_auto_connect(void)
{
    // Use configured WiFi credentials from Kconfig
    const char *default_ssid = CONFIG_HOWDY_WIFI_SSID;
    const char *default_password = CONFIG_HOWDY_WIFI_PASSWORD;
    
    ESP_LOGI(TAG, "Auto-connecting to saved WiFi...");
    ESP_LOGI(TAG, "Connecting to SSID: %s", default_ssid);
    return wifi_manager_connect(default_ssid, default_password);
}
```

**Verify menuconfig settings**:
```bash
idf.py menuconfig
# Navigate to: Component config → HowdyScreen Configuration
# Set: WiFi SSID and WiFi Password
```

#### Fatal Application Crash on WiFi Failure

**Symptoms**:
```
ESP_ERROR_CHECK failed: esp_err_t 0xffffffff (ESP_FAIL)
abort() was called at PC 0x4ff0cc4d on core 0
```

**Root Cause**: `ESP_ERROR_CHECK()` causing fatal termination when WiFi connection fails.

**Solution**:
```c
// Replace ESP_ERROR_CHECK with proper error handling:
esp_err_t wifi_result = wifi_manager_auto_connect();
if (wifi_result != ESP_OK) {
    ESP_LOGW(TAG, "⚠️ WiFi auto-connect failed: %s", esp_err_to_name(wifi_result));
    ui_manager_update_status("WiFi connection failed - will retry");
}
// Application continues running and retries connection
```

#### Quick Resolution Checklist for Phase 6A

```bash
# 1. Verify BSP dependency is included
grep "esp32_p4_wifi6_touch_lcd_xc" main/CMakeLists.txt

# 2. Check initialization sequence in main app
grep -A5 "bsp_display_start" main/*.c
grep -A2 "bsp_display_backlight_on" main/*.c

# 3. Verify WiFi credentials configuration
grep "CONFIG_HOWDY_WIFI" components/wifi_manager/src/wifi_manager.c

# 4. Ensure error handling (not ESP_ERROR_CHECK) for WiFi
grep -B2 -A2 "wifi_manager_auto_connect" main/*.c

# 5. Test build and deployment
idf.py clean build flash monitor
```

### Build and Configuration Problems

#### Issue: Build Fails with Component Dependencies

**Symptoms**:
```
CMake Error: Could not find a package configuration file provided by "audio_processor"
```

**Solution**:
```cmake
# In main/CMakeLists.txt, ensure proper component dependency
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES "audio_processor" "ui_manager" "websocket_client" "service_discovery" "bsp"
)
```

**Verification**:
```bash
# Check component structure
ls -la components/
# Verify each component has CMakeLists.txt
find components/ -name "CMakeLists.txt"
```

#### Issue: ESP32-P4 Target Not Recognized

**Symptoms**:
```
Error: Target 'esp32p4' is not supported
```

**Solution**:
```bash
# Update ESP-IDF to v5.3 or later
cd $IDF_PATH
git fetch
git checkout v5.3
git submodule update --init --recursive
./install.sh esp32p4

# Set target explicitly
idf.py set-target esp32p4
```

#### Issue: WiFi Remote Component Not Found

**Symptoms**:
```
Component 'esp_wifi_remote' not found
```

**Solution**:
```yaml
# Add to main/idf_component.yml
dependencies:
  espressif/esp_wifi_remote: ~0.3.0
  espressif/esp_hosted: '*'
```

```bash
# Clean and rebuild component manager cache
rm -rf managed_components/
idf.py reconfigure
```

### Initialization Sequence Problems

#### Issue: Components Initialize in Wrong Order

**Symptoms**:
- Display not working after audio init
- I2C conflicts between components
- System crashes during startup

**Solution**:
```c
// Correct initialization sequence
esp_err_t system_init(void) {
    esp_err_t ret = ESP_OK;
    
    // 1. Basic hardware first
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(initialize_audio_hardware());
    
    // 2. Display system
    lv_display_t *display = bsp_display_start();
    if (!display) return ESP_FAIL;
    
    // 3. Application components
    ESP_ERROR_CHECK(audio_processor_init(&audio_config));
    ESP_ERROR_CHECK(ui_manager_init());
    
    // 4. Network components (last)
    ESP_ERROR_CHECK(network_manager_init(&network_config));
    ESP_ERROR_CHECK(service_discovery_init(discovery_callback));
    
    return ret;
}
```

#### Issue: Memory Allocation Failures

**Symptoms**:
```
E (1234) AUDIO_PROC: Failed to allocate DMA buffer: ESP_ERR_NO_MEM
```

**Solution**:
```c
// Configure memory allocation strategy
void configure_memory_allocation(void) {
    // Use PSRAM for large buffers
    heap_caps_malloc_extmem_enable(4096);
    
    // Audio buffers in DMA-capable memory
    audio_config.dma_buf_count = 2;  // Reduce buffer count
    audio_config.dma_buf_len = 512;  // Smaller buffers
    
    // LVGL buffers in PSRAM
    lv_color_t *buf1 = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
}
```

### Component Communication Issues

#### Issue: Audio Data Not Reaching WebSocket

**Symptoms**:
- Audio capture working
- WebSocket connected
- No data transmitted

**Diagnosis**:
```c
void diagnose_audio_pipeline(void) {
    // Check audio processor state
    bool capturing = is_audio_capturing();
    ESP_LOGI(TAG, "Audio capturing: %s", capturing ? "YES" : "NO");
    
    // Check buffer availability
    uint8_t *buffer;
    size_t length;
    esp_err_t ret = audio_processor_get_buffer(&buffer, &length);
    ESP_LOGI(TAG, "Buffer available: %s (%d bytes)", 
             ret == ESP_OK ? "YES" : "NO", length);
    
    // Check WebSocket state
    ws_client_state_t ws_state = ws_client_get_state();
    ESP_LOGI(TAG, "WebSocket state: %d", ws_state);
    
    // Check callback registration
    bool callback_set = is_audio_callback_registered();
    ESP_LOGI(TAG, "Audio callback registered: %s", callback_set ? "YES" : "NO");
}
```

**Solution**:
```c
// Ensure proper callback chain
static void audio_event_handler(audio_event_t event, void *data, size_t len) {
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            ESP_LOGD(TAG, "Audio data ready: %d bytes", len);
            
            if (ws_client_is_audio_ready()) {
                uint8_t *buffer;
                size_t length;
                if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
                    esp_err_t ret = ws_client_send_binary_audio((int16_t*)buffer, length/2);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to send audio: %s", esp_err_to_name(ret));
                    }
                    audio_processor_release_buffer();
                }
            } else {
                ESP_LOGW(TAG, "WebSocket not ready for audio");
            }
            break;
        default:
            break;
    }
}
```

---

## System Diagnostics and Monitoring

### Comprehensive System Health Check

```c
// system_diagnostics.h
#pragma once

typedef struct {
    // Hardware status
    bool i2c_ok;
    bool display_ok;
    bool touch_ok;
    bool audio_codec_ok;
    bool sdio_ok;
    bool sd_card_ok;
    
    // Software status
    bool audio_processor_ok;
    bool ui_manager_ok;
    bool websocket_client_ok;
    bool service_discovery_ok;
    
    // Network status
    bool wifi_connected;
    int wifi_rssi;
    bool server_discovered;
    bool websocket_connected;
    
    // System resources
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t free_psram;
    float cpu_usage_core0;
    float cpu_usage_core1;
    
    // Performance metrics
    uint32_t audio_capture_rate;
    uint32_t network_throughput;
    uint32_t ui_frame_rate;
    uint32_t error_count;
} system_health_t;

esp_err_t system_health_check(system_health_t *health);
void system_health_print_report(const system_health_t *health);
```

```c
// system_diagnostics.c
#include "system_diagnostics.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"
#include "audio_processor.h"
#include "ui_manager.h"
#include "websocket_client.h"
#include "service_discovery.h"
#include "network_manager.h"

static const char *TAG = "SYS_DIAG";

esp_err_t system_health_check(system_health_t *health) {
    memset(health, 0, sizeof(system_health_t));
    
    ESP_LOGI(TAG, "Starting comprehensive system health check...");
    
    // Hardware diagnostics
    health->i2c_ok = test_i2c_bus();
    health->display_ok = test_display_controller();
    health->touch_ok = test_touch_controller();
    health->audio_codec_ok = test_audio_codec();
    health->sdio_ok = test_sdio_interface();
    health->sd_card_ok = test_sd_card();
    
    // Software component diagnostics
    health->audio_processor_ok = test_audio_processor_health();
    health->ui_manager_ok = test_ui_manager_health();
    health->websocket_client_ok = test_websocket_client_health();
    health->service_discovery_ok = test_service_discovery_health();
    
    // Network diagnostics
    health->wifi_connected = (network_manager_get_state(&network_mgr) == NETWORK_STATE_CONNECTED);
    health->wifi_rssi = network_get_rssi();
    health->server_discovered = check_server_discovery_status();
    health->websocket_connected = (ws_client_get_state() == WS_CLIENT_STATE_CONNECTED);
    
    // System resources
    health->free_heap = esp_get_free_heap_size();
    health->min_free_heap = esp_get_minimum_free_heap_size();
    health->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    // Performance metrics
    get_performance_metrics(health);
    
    ESP_LOGI(TAG, "System health check completed");
    return ESP_OK;
}

void system_health_print_report(const system_health_t *health) {
    ESP_LOGI(TAG, "=== SYSTEM HEALTH REPORT ===");
    
    ESP_LOGI(TAG, "Hardware Status:");
    ESP_LOGI(TAG, "  I2C Bus:        %s", health->i2c_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  Display:        %s", health->display_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  Touch:          %s", health->touch_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  Audio Codec:    %s", health->audio_codec_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  SDIO:           %s", health->sdio_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  SD Card:        %s", health->sd_card_ok ? "✓ OK" : "⚠ WARNING");
    
    ESP_LOGI(TAG, "Software Status:");
    ESP_LOGI(TAG, "  Audio Processor: %s", health->audio_processor_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  UI Manager:      %s", health->ui_manager_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  WebSocket Client: %s", health->websocket_client_ok ? "✓ OK" : "✗ FAIL");
    ESP_LOGI(TAG, "  Service Discovery: %s", health->service_discovery_ok ? "✓ OK" : "✗ FAIL");
    
    ESP_LOGI(TAG, "Network Status:");
    ESP_LOGI(TAG, "  WiFi Connected:  %s", health->wifi_connected ? "✓ YES" : "✗ NO");
    ESP_LOGI(TAG, "  WiFi RSSI:       %d dBm", health->wifi_rssi);
    ESP_LOGI(TAG, "  Server Found:    %s", health->server_discovered ? "✓ YES" : "✗ NO");
    ESP_LOGI(TAG, "  WebSocket:       %s", health->websocket_connected ? "✓ CONNECTED" : "✗ DISCONNECTED");
    
    ESP_LOGI(TAG, "System Resources:");
    ESP_LOGI(TAG, "  Free Heap:       %d bytes", health->free_heap);
    ESP_LOGI(TAG, "  Min Free Heap:   %d bytes", health->min_free_heap);
    ESP_LOGI(TAG, "  Free PSRAM:      %d bytes", health->free_psram);
    ESP_LOGI(TAG, "  CPU Core 0:      %.1f%%", health->cpu_usage_core0);
    ESP_LOGI(TAG, "  CPU Core 1:      %.1f%%", health->cpu_usage_core1);
    
    ESP_LOGI(TAG, "Performance Metrics:");
    ESP_LOGI(TAG, "  Audio Rate:      %d samples/sec", health->audio_capture_rate);
    ESP_LOGI(TAG, "  Network Rate:    %d bytes/sec", health->network_throughput);
    ESP_LOGI(TAG, "  UI Frame Rate:   %d fps", health->ui_frame_rate);
    ESP_LOGI(TAG, "  Error Count:     %d", health->error_count);
    
    // Overall health assessment
    int critical_failures = 0;
    if (!health->i2c_ok) critical_failures++;
    if (!health->display_ok) critical_failures++;
    if (!health->audio_codec_ok) critical_failures++;
    if (!health->audio_processor_ok) critical_failures++;
    
    if (critical_failures == 0) {
        ESP_LOGI(TAG, "Overall Status: ✓ HEALTHY");
    } else if (critical_failures <= 2) {
        ESP_LOGW(TAG, "Overall Status: ⚠ WARNING (%d issues)", critical_failures);
    } else {
        ESP_LOGE(TAG, "Overall Status: ✗ CRITICAL (%d failures)", critical_failures);
    }
    
    ESP_LOGI(TAG, "=============================");
}

// Individual component tests
static bool test_i2c_bus(void) {
    ESP_LOGD(TAG, "Testing I2C bus...");
    
    i2c_master_bus_handle_t bus_handle = bsp_i2c_get_handle();
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus handle is NULL");
        return false;
    }
    
    // Scan for expected devices
    bool es8311_found = false;
    bool cst9217_found = false;
    
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        esp_err_t ret = i2c_master_probe(bus_handle, addr, 100);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "I2C device found at 0x%02X", addr);
            if (addr == 0x18) es8311_found = true;
            if (addr == 0x1A) cst9217_found = true;
        }
    }
    
    if (!es8311_found) {
        ESP_LOGE(TAG, "ES8311 audio codec not found at 0x18");
    }
    if (!cst9217_found) {
        ESP_LOGE(TAG, "CST9217 touch controller not found at 0x1A");
    }
    
    return es8311_found && cst9217_found;
}

static bool test_audio_codec(void) {
    ESP_LOGD(TAG, "Testing audio codec...");
    
    // Try to read codec ID register
    i2c_master_bus_handle_t bus_handle = bsp_i2c_get_handle();
    if (bus_handle == NULL) return false;
    
    uint8_t codec_id = 0;
    uint8_t write_data = 0xFD;  // ES8311 chip ID register
    uint8_t read_data = 0;
    
    esp_err_t ret = i2c_master_transmit_receive(bus_handle, 0x18, &write_data, 1, &read_data, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read codec ID: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGD(TAG, "Codec ID: 0x%02X", read_data);
    return (read_data == 0x83);  // Expected ES8311 ID
}

static bool test_display_controller(void) {
    ESP_LOGD(TAG, "Testing display controller...");
    
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display not initialized");
        return false;
    }
    
    // Test display buffer
    lv_color_t *buf = lv_display_get_buf_active(display);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Display buffer not available");
        return false;
    }
    
    return true;
}

static bool test_touch_controller(void) {
    ESP_LOGD(TAG, "Testing touch controller...");
    
    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch == NULL) {
        ESP_LOGE(TAG, "Touch input device not initialized");
        return false;
    }
    
    // Test touch data reading
    lv_indev_data_t data;
    lv_indev_read(touch, &data);
    
    return true;  // If we get here, basic touch reading works
}
```

### Automated Diagnostic Script

```bash
#!/bin/bash
# tools/system_diagnostics.sh

PROJECT_ROOT=$(dirname $(dirname $(realpath $0)))
LOG_FILE="/tmp/howdyscreen_diagnostics.log"

echo "ESP32-P4 HowdyScreen System Diagnostics" | tee $LOG_FILE
echo "=======================================" | tee -a $LOG_FILE
echo "Date: $(date)" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

# Check development environment
echo "Development Environment:" | tee -a $LOG_FILE
echo "  ESP-IDF Path: $IDF_PATH" | tee -a $LOG_FILE
echo "  ESP-IDF Version: $(cd $IDF_PATH && git describe --tags)" | tee -a $LOG_FILE
echo "  Python Version: $(python --version)" | tee -a $LOG_FILE
echo "  CMake Version: $(cmake --version | head -1)" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

# Check project structure
echo "Project Structure:" | tee -a $LOG_FILE
for component in audio_processor bsp ui_manager websocket_client service_discovery; do
    if [ -d "$PROJECT_ROOT/components/$component" ]; then
        echo "  ✓ $component component found" | tee -a $LOG_FILE
    else
        echo "  ✗ $component component MISSING" | tee -a $LOG_FILE
    fi
done
echo "" | tee -a $LOG_FILE

# Check managed components
echo "Managed Components:" | tee -a $LOG_FILE
if [ -d "$PROJECT_ROOT/managed_components" ]; then
    component_count=$(ls -1 "$PROJECT_ROOT/managed_components" | wc -l)
    echo "  ✓ $component_count managed components installed" | tee -a $LOG_FILE
else
    echo "  ✗ No managed components found" | tee -a $LOG_FILE
fi
echo "" | tee -a $LOG_FILE

# Try to build project
echo "Build Test:" | tee -a $LOG_FILE
cd $PROJECT_ROOT

if idf.py build > build_output.log 2>&1; then
    echo "  ✓ Project builds successfully" | tee -a $LOG_FILE
    
    # Check binary size
    if [ -f "build/HowdyScreen.bin" ]; then
        size=$(stat -f%z "build/HowdyScreen.bin" 2>/dev/null || stat -c%s "build/HowdyScreen.bin")
        echo "  ✓ Binary size: $size bytes" | tee -a $LOG_FILE
    fi
else
    echo "  ✗ Build FAILED - check build_output.log" | tee -a $LOG_FILE
fi
echo "" | tee -a $LOG_FILE

# Check for common issues
echo "Common Issues Check:" | tee -a $LOG_FILE

# Check for WiFi remote component
if grep -q "esp_wifi_remote" main/idf_component.yml; then
    echo "  ✓ WiFi remote component configured" | tee -a $LOG_FILE
else
    echo "  ⚠ WiFi remote component not found in dependencies" | tee -a $LOG_FILE
fi

# Check target configuration
if grep -q "esp32p4" sdkconfig; then
    echo "  ✓ ESP32-P4 target configured" | tee -a $LOG_FILE
else
    echo "  ✗ ESP32-P4 target not set" | tee -a $LOG_FILE
fi

# Check PSRAM configuration
if grep -q "CONFIG_SPIRAM=y" sdkconfig; then
    echo "  ✓ PSRAM enabled" | tee -a $LOG_FILE
else
    echo "  ⚠ PSRAM not enabled" | tee -a $LOG_FILE
fi

echo "" | tee -a $LOG_FILE
echo "Diagnostics complete. Log saved to: $LOG_FILE"
```

---

## Error Code Reference

### ESP32-P4 HowdyScreen Error Codes

#### Audio Processor Errors (0x8000-0x80FF)

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0x8001 | `AUDIO_ERR_NOT_INITIALIZED` | Audio processor not initialized | Call `audio_processor_init()` first |
| 0x8002 | `AUDIO_ERR_INVALID_STATE` | Invalid state for operation | Check current state with status functions |
| 0x8003 | `AUDIO_ERR_DMA_FAILED` | DMA buffer allocation failed | Reduce buffer size or count |
| 0x8004 | `AUDIO_ERR_I2S_FAILED` | I2S interface error | Check I2S pin configuration |
| 0x8005 | `AUDIO_ERR_CODEC_TIMEOUT` | Audio codec timeout | Reset codec, check I2C connection |
| 0x8006 | `AUDIO_ERR_BUFFER_OVERFLOW` | Audio buffer overflow | Increase processing speed or buffer size |
| 0x8007 | `AUDIO_ERR_BUFFER_UNDERRUN` | Audio buffer underrun | Check audio data source |

#### UI Manager Errors (0x8100-0x81FF)

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0x8101 | `UI_ERR_NOT_INITIALIZED` | UI manager not initialized | Call `ui_manager_init()` first |
| 0x8102 | `UI_ERR_DISPLAY_FAILED` | Display initialization failed | Check display connections |
| 0x8103 | `UI_ERR_TOUCH_FAILED` | Touch controller failed | Check touch I2C connection |
| 0x8104 | `UI_ERR_MEMORY_FAILED` | UI memory allocation failed | Free memory or use PSRAM |
| 0x8105 | `UI_ERR_INVALID_STATE` | Invalid UI state transition | Check state machine logic |

#### WebSocket Client Errors (0x8200-0x82FF)

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0x8201 | `WS_ERR_NOT_INITIALIZED` | WebSocket client not initialized | Call `ws_client_init()` first |
| 0x8202 | `WS_ERR_CONNECTION_FAILED` | Connection to server failed | Check server IP and port |
| 0x8203 | `WS_ERR_SEND_FAILED` | Failed to send data | Check connection status |
| 0x8204 | `WS_ERR_RECEIVE_TIMEOUT` | Receive timeout | Check network connectivity |
| 0x8205 | `WS_ERR_INVALID_DATA` | Invalid data format | Check protocol implementation |
| 0x8206 | `WS_ERR_BUFFER_FULL` | Send buffer full | Reduce send rate or increase buffer |

#### Network Errors (0x8300-0x83FF)

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0x8301 | `NET_ERR_WIFI_INIT_FAILED` | WiFi initialization failed | Check ESP32-C6 connection |
| 0x8302 | `NET_ERR_WIFI_CONNECT_FAILED` | WiFi connection failed | Check SSID/password |
| 0x8303 | `NET_ERR_WIFI_TIMEOUT` | WiFi connection timeout | Check signal strength |
| 0x8304 | `NET_ERR_DNS_FAILED` | DNS resolution failed | Check DNS server |
| 0x8305 | `NET_ERR_SERVER_UNREACHABLE` | Server unreachable | Check server status |

#### Service Discovery Errors (0x8400-0x84FF)

| Code | Name | Description | Solution |
|------|------|-------------|----------|
| 0x8401 | `SD_ERR_MDNS_INIT_FAILED` | mDNS initialization failed | Check network connection |
| 0x8402 | `SD_ERR_NO_SERVERS_FOUND` | No HowdyTTS servers found | Check server is running |
| 0x8403 | `SD_ERR_QUERY_TIMEOUT` | mDNS query timeout | Check network connectivity |
| 0x8404 | `SD_ERR_INVALID_RESPONSE` | Invalid mDNS response | Check server configuration |

### Error Handling Framework

```c
// error_handler.h
#pragma once

#include "esp_err.h"
#include "esp_log.h"

// Error categories
typedef enum {
    ERROR_CATEGORY_HARDWARE = 0,
    ERROR_CATEGORY_SOFTWARE,
    ERROR_CATEGORY_NETWORK,
    ERROR_CATEGORY_SYSTEM
} error_category_t;

// Error severity levels
typedef enum {
    ERROR_SEVERITY_INFO = 0,
    ERROR_SEVERITY_WARNING,
    ERROR_SEVERITY_ERROR,
    ERROR_SEVERITY_CRITICAL
} error_severity_t;

// Error record structure
typedef struct {
    esp_err_t error_code;
    error_category_t category;
    error_severity_t severity;
    const char *component;
    const char *function;
    int line;
    uint64_t timestamp;
    char description[128];
} error_record_t;

// Error handler function
void system_error_handler(esp_err_t error_code, 
                         error_category_t category,
                         error_severity_t severity,
                         const char *component,
                         const char *function,
                         int line,
                         const char *description);

// Convenience macros
#define HANDLE_ERROR(code, cat, sev, desc) \
    system_error_handler(code, cat, sev, __FILE__, __func__, __LINE__, desc)

#define HANDLE_CRITICAL_ERROR(code, desc) \
    HANDLE_ERROR(code, ERROR_CATEGORY_SYSTEM, ERROR_SEVERITY_CRITICAL, desc)

#define HANDLE_NETWORK_ERROR(code, desc) \
    HANDLE_ERROR(code, ERROR_CATEGORY_NETWORK, ERROR_SEVERITY_ERROR, desc)
```

```c
// error_handler.c
#include "error_handler.h"
#include "ui_manager.h"

#define MAX_ERROR_RECORDS 50
static error_record_t error_history[MAX_ERROR_RECORDS];
static int error_count = 0;
static int error_index = 0;

void system_error_handler(esp_err_t error_code, 
                         error_category_t category,
                         error_severity_t severity,
                         const char *component,
                         const char *function,
                         int line,
                         const char *description) {
    
    // Log the error
    const char *severity_str[] = {"INFO", "WARN", "ERROR", "CRITICAL"};
    ESP_LOG_LEVEL(severity >= ERROR_SEVERITY_ERROR ? ESP_LOG_ERROR : ESP_LOG_WARN,
                  component, "[%s] %s:%d - %s (0x%x)", 
                  severity_str[severity], function, line, description, error_code);
    
    // Store in error history
    error_record_t *record = &error_history[error_index];
    record->error_code = error_code;
    record->category = category;
    record->severity = severity;
    record->component = component;
    record->function = function;
    record->line = line;
    record->timestamp = esp_timer_get_time();
    strncpy(record->description, description, sizeof(record->description) - 1);
    
    error_index = (error_index + 1) % MAX_ERROR_RECORDS;
    if (error_count < MAX_ERROR_RECORDS) {
        error_count++;
    }
    
    // Update UI with error status
    if (severity >= ERROR_SEVERITY_ERROR) {
        ui_manager_set_state(UI_STATE_ERROR);
        ui_manager_update_status(description);
    }
    
    // Handle critical errors
    if (severity == ERROR_SEVERITY_CRITICAL) {
        ESP_LOGE(component, "CRITICAL ERROR - System restart required");
        vTaskDelay(pdMS_TO_TICKS(5000));  // Allow time for logging
        esp_restart();
    }
}

void print_error_history(void) {
    ESP_LOGI("ERROR_HIST", "Error History (%d total):", error_count);
    
    int start = error_count < MAX_ERROR_RECORDS ? 0 : error_index;
    for (int i = 0; i < error_count; i++) {
        int idx = (start + i) % MAX_ERROR_RECORDS;
        error_record_t *record = &error_history[idx];
        
        ESP_LOGI("ERROR_HIST", "%d. [%lld] %s: %s (0x%x)", 
                 i + 1, record->timestamp, record->component,
                 record->description, record->error_code);
    }
}
```

---

## Performance Optimization

### Memory Optimization

#### Memory Usage Analysis

```c
// memory_analyzer.c
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "MEM_ANALYZER";

typedef struct {
    const char* component;
    size_t allocated_size;
    void* ptr;
} allocation_record_t;

#define MAX_ALLOCATIONS 100
static allocation_record_t allocations[MAX_ALLOCATIONS];
static int allocation_count = 0;

void* tracked_malloc(size_t size, const char* component) {
    void* ptr = malloc(size);
    if (ptr && allocation_count < MAX_ALLOCATIONS) {
        allocations[allocation_count].component = component;
        allocations[allocation_count].allocated_size = size;
        allocations[allocation_count].ptr = ptr;
        allocation_count++;
    }
    return ptr;
}

void tracked_free(void* ptr) {
    for (int i = 0; i < allocation_count; i++) {
        if (allocations[i].ptr == ptr) {
            // Move last element to this position
            allocations[i] = allocations[allocation_count - 1];
            allocation_count--;
            break;
        }
    }
    free(ptr);
}

void print_memory_usage_by_component(void) {
    ESP_LOGI(TAG, "Memory Usage by Component:");
    
    // Group by component
    const char* components[20];
    size_t component_sizes[20];
    int component_count = 0;
    
    for (int i = 0; i < allocation_count; i++) {
        const char* comp = allocations[i].component;
        int found = -1;
        
        for (int j = 0; j < component_count; j++) {
            if (strcmp(components[j], comp) == 0) {
                found = j;
                break;
            }
        }
        
        if (found >= 0) {
            component_sizes[found] += allocations[i].allocated_size;
        } else if (component_count < 20) {
            components[component_count] = comp;
            component_sizes[component_count] = allocations[i].allocated_size;
            component_count++;
        }
    }
    
    // Sort by size (simple bubble sort)
    for (int i = 0; i < component_count - 1; i++) {
        for (int j = 0; j < component_count - i - 1; j++) {
            if (component_sizes[j] < component_sizes[j + 1]) {
                // Swap sizes
                size_t temp_size = component_sizes[j];
                component_sizes[j] = component_sizes[j + 1];  
                component_sizes[j + 1] = temp_size;
                
                // Swap names
                const char* temp_name = components[j];
                components[j] = components[j + 1];
                components[j + 1] = temp_name;
            }
        }
    }
    
    for (int i = 0; i < component_count; i++) {
        ESP_LOGI(TAG, "  %-20s: %6d bytes", components[i], component_sizes[i]);
    }
}

void optimize_memory_usage(void) {
    ESP_LOGI(TAG, "Analyzing memory usage patterns...");
    
    // Check for memory fragmentation
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    size_t total_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    
    float fragmentation = 1.0f - ((float)largest_block / (float)total_free);
    ESP_LOGI(TAG, "Memory fragmentation: %.1f%%", fragmentation * 100);
    
    if (fragmentation > 0.3) {
        ESP_LOGW(TAG, "High memory fragmentation detected");
        ESP_LOGW(TAG, "Consider using memory pools or reducing allocation/deallocation frequency");
    }
    
    // Check PSRAM usage
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    if (psram_total > 0) {
        float psram_usage = 1.0f - ((float)psram_free / (float)psram_total);
        ESP_LOGI(TAG, "PSRAM usage: %.1f%% (%d/%d bytes)", 
                 psram_usage * 100, psram_total - psram_free, psram_total);
        
        if (psram_usage < 0.1) {
            ESP_LOGI(TAG, "PSRAM underutilized - consider moving large buffers to PSRAM");
        }
    }
}
```

### CPU Performance Optimization

#### Task Priority Optimization

```c
// task_optimizer.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "TASK_OPT";

typedef struct {
    const char* name;
    UBaseType_t priority;
    BaseType_t core;
    uint32_t stack_size;
    TaskFunction_t function;
    bool is_critical;
} task_config_t;

// Optimized task configuration
static const task_config_t optimized_tasks[] = {
    // Critical real-time tasks on Core 1
    {"audio_capture",    25, 1, 4096,  audio_capture_task,  true},
    {"audio_playback",   24, 1, 4096,  audio_playback_task, true},
    {"i2s_handler",      23, 1, 2048,  i2s_handler_task,    true},
    
    // High priority network tasks on Core 0  
    {"websocket_tx",     22, 0, 4096,  websocket_tx_task,   false},
    {"websocket_rx",     21, 0, 4096,  websocket_rx_task,   false},
    {"network_monitor",  20, 0, 3072,  network_monitor_task, false},
    
    // Medium priority UI tasks on Core 0
    {"lvgl_task",        19, 0, 4096,  lv_task_handler,     false},
    {"ui_update",        18, 0, 3072,  ui_update_task,      false},
    {"touch_handler",    17, 0, 2048,  touch_handler_task,  false},
    
    // Low priority background tasks
    {"service_discovery", 10, 0, 3072, service_discovery_task, false},
    {"system_monitor",    5,  0, 2048, system_monitor_task,    false},
    {"log_writer",        3,  0, 2048, log_writer_task,        false},
};

void optimize_task_priorities(void) {
    ESP_LOGI(TAG, "Optimizing task priorities and core assignments...");
    
    // Get current task list
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = malloc(task_count * sizeof(TaskStatus_t));
    
    if (task_array) {
        UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, NULL);
        
        // Analyze current task distribution
        int core0_tasks = 0, core1_tasks = 0;
        int high_priority_tasks = 0;
        
        for (UBaseType_t i = 0; i < actual_count; i++) {
            if (task_array[i].xCoreID == 0) core0_tasks++;
            else if (task_array[i].xCoreID == 1) core1_tasks++;
            
            if (task_array[i].uxCurrentPriority > 20) high_priority_tasks++;
        }
        
        ESP_LOGI(TAG, "Current task distribution:");
        ESP_LOGI(TAG, "  Core 0: %d tasks", core0_tasks);
        ESP_LOGI(TAG, "  Core 1: %d tasks", core1_tasks);
        ESP_LOGI(TAG, "  High priority (>20): %d tasks", high_priority_tasks);
        
        // Recommendations
        if (abs(core0_tasks - core1_tasks) > 3) {
            ESP_LOGW(TAG, "Unbalanced task distribution between cores");
        }
        
        if (high_priority_tasks > 5) {
            ESP_LOGW(TAG, "Too many high priority tasks may cause starvation");
        }
        
        free(task_array);
    }
}

void monitor_task_performance(void) {
    TaskStatus_t *task_array;
    UBaseType_t task_count;
    uint32_t total_runtime;
    static uint32_t last_total_runtime = 0;
    
    task_count = uxTaskGetNumberOfTasks();
    task_array = malloc(task_count * sizeof(TaskStatus_t));
    
    if (task_array) {
        UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
        
        if (last_total_runtime > 0) {
            uint32_t runtime_delta = total_runtime - last_total_runtime;
            
            ESP_LOGI(TAG, "Task Performance Analysis:");
            ESP_LOGI(TAG, "%-20s %8s %8s %8s %8s", "Task", "CPU%", "Priority", "Stack", "Core");
            
            for (UBaseType_t i = 0; i < actual_count; i++) {
                uint32_t task_runtime = task_array[i].ulRunTimeCounter;
                uint32_t cpu_percent = (task_runtime * 100) / runtime_delta;
                
                // Flag performance issues
                const char* flag = "";
                if (cpu_percent > 50) flag = " ⚠HIGH";
                else if (task_array[i].usStackHighWaterMark < 512) flag = " ⚠STACK";
                
                ESP_LOGI(TAG, "%-20s %7d%% %8d %8d %8d%s",
                         task_array[i].pcTaskName,
                         cpu_percent,
                         task_array[i].uxCurrentPriority,
                         task_array[i].usStackHighWaterMark * sizeof(StackType_t),
                         task_array[i].xCoreID,
                         flag);
            }
        }
        
        last_total_runtime = total_runtime;
        free(task_array);
    }
}
```

This comprehensive troubleshooting guide provides systematic approaches to diagnosing and resolving issues in the ESP32-P4 HowdyScreen system, from basic integration problems to advanced performance optimization.