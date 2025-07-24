# ESP32-P4 + ESP32-C6 WiFi Configuration Guide

## Hardware Architecture

The Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C board uses a dual-chip architecture:
- **ESP32-P4**: Main processor (no built-in WiFi)
- **ESP32-C6**: WiFi co-processor connected via SDIO interface

## Key Configuration Points

### 1. WiFi Limitations
- **Only 2.4GHz networks are supported** (ESP32-C6 limitation)
- WiFi 6 features are available on 2.4GHz band
- Ensure your router/AP broadcasts on 2.4GHz

### 2. Required Components
The following components have been added to `idf_component.yml`:
```yaml
espressif/esp_wifi_remote: "~0.3.0"
espressif/esp_hosted: "*"
```

### 3. Build Configuration
Enable WiFi remote support in menuconfig:
```bash
idf.py menuconfig
# Navigate to: HowdyScreen Configuration → ESP32-P4 + ESP32-C6 Configuration
# Enable: Use ESP WiFi Remote (for ESP32-C6 co-processor)
# Enable: Enable SDIO Interface for ESP32-C6
```

### 4. Network Configuration
When using WiFi provisioning:
1. Connect to AP: "HowdyScreen-Setup" (password: "configure")
2. Access: http://192.168.4.1
3. Select a 2.4GHz network from the scan results
4. Enter WiFi password

### 5. Troubleshooting

#### WiFi Not Connecting
- Verify network is 2.4GHz (not 5GHz only)
- Check if `CONFIG_ESP_HOST_WIFI_ENABLED=y` in sdkconfig
- Ensure esp_wifi_remote is properly initialized

#### Build Errors
- Run `idf.py reconfigure` after adding components
- Clean build: `idf.py fullclean && idf.py build`

#### Runtime Issues
- Check serial monitor for ESP32-C6 initialization messages
- Look for "ESP WiFi Remote initialized for ESP32-C6 co-processor"

## Implementation Details

The WiFi functionality is implemented with conditional compilation:
```c
#ifdef CONFIG_HOWDY_USE_ESP_WIFI_REMOTE
    wifi_remote_config_t remote_config = WIFI_REMOTE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_wifi_remote_init(&remote_config));
#endif
```

This ensures compatibility with both:
- ESP32-P4 + ESP32-C6 boards (with esp_wifi_remote)
- Other ESP32 variants (standard WiFi stack)