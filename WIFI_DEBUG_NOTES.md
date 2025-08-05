# WiFi Connectivity Debug Notes

## Issue Summary
The ESP32-P4 is establishing an EPPP (Ethernet over PPP) connection with the ESP32-C6 co-processor via UART, but actual WiFi connectivity to the network (J&Awifi) is not being established.

## Current Status
- ✅ EPPP link established between ESP32-P4 and ESP32-C6 (via UART pins 10/11)
- ✅ PPP interface created (192.168.11.2 ↔ 192.168.11.1)
- ❌ WiFi commands not being executed on ESP32-C6
- ❌ No actual connection to WiFi network "J&Awifi"

## Root Cause
The ESP32-C6 co-processor needs to be running specific firmware that:
1. Acts as an EPPP server
2. Handles esp_wifi_remote commands from ESP32-P4
3. Actually connects to the WiFi network

## Configuration Mismatch
- Documentation mentions SDIO interface (GPIO 36, 37, 35, 34, 33, 48)
- Actual implementation uses UART/EPPP (GPIO 10, 11)
- This is configured in sdkconfig:
  - CONFIG_ESP_WIFI_REMOTE_EPPP_UART_TX_PIN=10
  - CONFIG_ESP_WIFI_REMOTE_EPPP_UART_RX_PIN=11

## Solution Steps

### 1. Flash ESP32-C6 with EPPP Server Firmware
The ESP32-C6 needs to be running the esp_wifi_remote EPPP server firmware. Here's how:

```bash
# Clone the esp_wifi_remote repository
git clone https://github.com/espressif/esp_wifi_remote.git
cd esp_wifi_remote/examples/wifi_remote_eppp

# Configure for ESP32-C6
idf.py set-target esp32c6
idf.py menuconfig

# In menuconfig, configure:
# - EPPP Configuration -> Transport -> UART
# - EPPP Configuration -> UART TX Pin -> 16 (or match your hardware)
# - EPPP Configuration -> UART RX Pin -> 17 (or match your hardware)
# - EPPP Configuration -> UART Baud Rate -> 921600

# Build and flash to ESP32-C6
idf.py -p /dev/cu.usbserial-XXXXX build flash monitor
```

**IMPORTANT**: The ESP32-C6 firmware from this example will:
- Start an EPPP server on UART
- Handle WiFi commands from ESP32-P4
- Connect to actual WiFi networks
- Bridge network traffic between WiFi and EPPP

### 2. Hardware Connections
Verify UART connections between ESP32-P4 and ESP32-C6:
- ESP32-P4 GPIO10 (TX) → ESP32-C6 RX
- ESP32-P4 GPIO11 (RX) → ESP32-C6 TX
- Common ground between boards

### 3. Alternative: Use SDIO Interface
If UART/EPPP continues to have issues, switch to SDIO as originally designed:
- Update sdkconfig to use SDIO transport instead of UART
- Connect SDIO pins as documented
- Flash ESP32-C6 with SDIO server firmware

### 4. Debug Commands
To verify ESP32-C6 is responding to WiFi commands:
```c
// Add to simple_wifi_manager.c after esp_wifi_start()
wifi_ap_record_t ap_list[10];
uint16_t ap_count = 10;
esp_err_t scan_ret = esp_wifi_scan_start(NULL, true);
ESP_LOGI(TAG, "WiFi scan result: %s", esp_err_to_name(scan_ret));
if (scan_ret == ESP_OK) {
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    ESP_LOGI(TAG, "Found %d access points", ap_count);
    for (int i = 0; i < ap_count; i++) {
        ESP_LOGI(TAG, "  SSID: %s, RSSI: %d", ap_list[i].ssid, ap_list[i].rssi);
    }
}
```

## Current WiFi Credentials
- SSID: J&Awifi
- Password: Jojoba21
- Configured in: sdkconfig (via menuconfig)

## Next Steps
1. Check if ESP32-C6 has the correct server firmware
2. Monitor ESP32-C6 serial output during connection attempts
3. Consider switching from UART/EPPP to SDIO transport
4. Verify physical connections between boards