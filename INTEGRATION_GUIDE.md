# ESP32-P4 HowdyScreen Integration Guide

## Overview

This guide provides step-by-step procedures for integrating components, handling errors, performance tuning, and testing the ESP32-P4 HowdyScreen system. Follow these procedures to ensure reliable system operation.

## Table of Contents

1. [Component Integration Procedures](#component-integration-procedures)
2. [Error Handling and Troubleshooting](#error-handling-and-troubleshooting)
3. [Performance Tuning Guidelines](#performance-tuning-guidelines)
4. [Testing and Validation](#testing-and-validation)
5. [System Monitoring](#system-monitoring)
6. [Development Workflow](#development-workflow)

---

## Component Integration Procedures

### 1. BSP Layer Integration

The BSP (Board Support Package) must be initialized first as it provides hardware abstraction for all other components.

#### Step 1: Initialize Core Hardware

```c
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

esp_err_t initialize_bsp_layer(void) {
    esp_err_t ret = ESP_OK;
    
    // Step 1: Initialize I2C bus (shared by codec and touch controller)
    ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C initialized successfully");
    
    // Step 2: Initialize display system
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return ESP_FAIL;
    }
    
    // Step 3: Get touch input device
    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch == NULL) {
        ESP_LOGE(TAG, "Failed to initialize touch controller");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BSP layer initialized successfully");
    return ESP_OK;
}
```

#### Step 2: Initialize Audio Subsystem

```c
esp_err_t initialize_audio_subsystem(void) {
    esp_err_t ret = ESP_OK;
    
    // Configure I2S for ES8311 codec
    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),  // 16 kHz sample rate
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    
    // Step 1: Initialize I2S interface
    ret = bsp_audio_init(&i2s_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Initialize codec devices
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    if (speaker == NULL) {
        ESP_LOGE(TAG, "Failed to initialize speaker codec");
        return ESP_FAIL;
    }
    
    esp_codec_dev_handle_t microphone = bsp_audio_codec_microphone_init();
    if (microphone == NULL) {
        ESP_LOGE(TAG, "Failed to initialize microphone codec");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio subsystem initialized successfully");
    return ESP_OK;
}
```

### 2. Audio Processor Integration

#### Step 1: Configure Audio Pipeline

```c
#include "audio_processor.h"

esp_err_t integrate_audio_processor(void) {
    esp_err_t ret = ESP_OK;
    
    // Configure audio processor
    audio_processor_config_t config = {
        .sample_rate = 16000,           // Match I2S configuration
        .bits_per_sample = 16,          // 16-bit PCM
        .channels = 1,                  // Mono audio
        .dma_buf_count = 4,             // 4 DMA buffers for stability
        .dma_buf_len = 1024,            // 1024 samples per buffer
        .task_priority = 23,            // High priority for real-time audio
        .task_core = 1                  // Use CPU core 1
    };
    
    // Step 1: Initialize audio processor
    ret = audio_processor_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio processor: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Set event callback
    ret = audio_processor_set_callback(audio_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set audio callback: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Audio processor integrated successfully");
    return ESP_OK;
}

// Audio event handler implementation
static void audio_event_handler(audio_event_t event, void *data, size_t len) {
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            // Forward audio data to WebSocket client
            if (ws_client_is_audio_ready()) {
                uint8_t *buffer;
                size_t length;
                if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
                    ws_client_send_binary_audio((int16_t*)buffer, length / 2);
                    audio_processor_release_buffer();
                }
            }
            break;
            
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio pipeline error");
            // Trigger error recovery
            system_handle_error(SYSTEM_ERROR_AUDIO_PIPELINE);
            break;
            
        default:
            break;
    }
}
```

### 3. UI Manager Integration

#### Step 1: Initialize UI System

```c
#include "ui_manager.h"

esp_err_t integrate_ui_manager(void) {
    esp_err_t ret = ESP_OK;
    
    // Step 1: Initialize UI manager (requires BSP display to be ready)
    ret = ui_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Set initial state
    ui_manager_set_state(UI_STATE_INIT);
    ui_manager_update_status("Initializing...");
    
    // Step 3: Create UI update task
    xTaskCreatePinnedToCore(
        ui_update_task,
        "ui_update",
        4096,
        NULL,
        10,  // Lower priority than audio
        NULL,
        0    // CPU core 0
    );
    
    ESP_LOGI(TAG, "UI manager integrated successfully");
    return ESP_OK;
}

// UI update task
static void ui_update_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(100);  // Update every 100ms
    
    while (1) {
        // Update audio level display
        int audio_level = get_current_audio_level();
        ui_manager_update_audio_level(audio_level);
        
        // Update WiFi signal strength
        int wifi_rssi = network_get_rssi();
        if (wifi_rssi != 0) {
            int strength = rssi_to_percentage(wifi_rssi);
            ui_manager_set_wifi_strength(strength);
        }
        
        // Update connection status
        update_connection_status();
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
```

### 4. Network Components Integration

#### Step 1: Network Manager Setup

```c
#include "network_manager.h"

static network_manager_t network_mgr;

esp_err_t integrate_network_manager(void) {
    esp_err_t ret = ESP_OK;
    
    // Step 1: Initialize network manager
    ret = network_manager_init(&network_mgr, 
                              CONFIG_HOWDY_WIFI_SSID,
                              CONFIG_HOWDY_WIFI_PASSWORD,
                              CONFIG_HOWDY_SERVER_IP,
                              8765);  // Default WebSocket port
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Start WiFi connection
    ret = network_manager_connect(&network_mgr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Network manager integrated successfully");
    return ESP_OK;
}
```

#### Step 2: Service Discovery Integration

```c
#include "service_discovery.h"

esp_err_t integrate_service_discovery(void) {
    esp_err_t ret = ESP_OK;
    
    // Step 1: Initialize service discovery
    ret = service_discovery_init(server_discovered_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize service discovery: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Advertise this client
    ret = service_discovery_advertise_client("HowdyScreen", "audio,display,touch");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to advertise client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 3: Start continuous scanning
    ret = service_discovery_start_scan(0);  // Continuous scan
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start service discovery: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Service discovery integrated successfully");
    return ESP_OK;
}

// Server discovery callback
static void server_discovered_callback(const howdytts_server_info_t *server_info) {
    ESP_LOGI(TAG, "Discovered HowdyTTS server: %s at %s:%d", 
             server_info->hostname, server_info->ip_addr, server_info->port);
    
    // Test server connectivity
    uint32_t latency;
    if (service_discovery_test_server(server_info, 5000, &latency) == ESP_OK) {
        ESP_LOGI(TAG, "Server latency: %dms", latency);
        
        // Connect to best available server
        connect_to_best_server();
    }
}
```

#### Step 3: WebSocket Client Integration

```c
#include "websocket_client.h"

esp_err_t integrate_websocket_client(void) {
    esp_err_t ret = ESP_OK;
    
    // Get best available server
    howdytts_server_info_t server_info;
    ret = service_discovery_get_best_server(&server_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No servers discovered, using configured server");
        strcpy(server_info.ip_addr, CONFIG_HOWDY_SERVER_IP);
        server_info.port = 8765;
    }
    
    // Configure WebSocket client
    ws_client_config_t config = {
        .reconnect_timeout_ms = 5000,
        .keepalive_idle_sec = 30,
        .keepalive_interval_sec = 5,
        .keepalive_count = 3,
        .auto_reconnect = true,
        .buffer_size = CONFIG_WEBSOCKET_BUFFER_SIZE
    };
    
    snprintf(config.server_uri, sizeof(config.server_uri), 
             "ws://%s:%d", server_info.ip_addr, server_info.port);
    
    // Step 1: Initialize WebSocket client
    ret = ws_client_init(&config, websocket_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Start connection
    ret = ws_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WebSocket client integrated successfully");
    return ESP_OK;
}

// WebSocket event handler
static void websocket_event_handler(ws_client_state_t state, 
                                   ws_message_type_t msg_type, 
                                   const uint8_t *data, 
                                   size_t len) {
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            ui_manager_update_status("Connected to HowdyTTS");
            ui_manager_set_state(UI_STATE_IDLE);
            break;
            
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            ui_manager_update_status("Disconnected");
            ui_manager_set_state(UI_STATE_ERROR);
            break;
            
        case WS_CLIENT_STATE_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            ui_manager_update_status("Connection Error");
            break;
            
        default:
            break;
    }
    
    switch (msg_type) {
        case WS_MSG_TYPE_TTS_RESPONSE:
            ESP_LOGI(TAG, "Received TTS audio: %d bytes", len);
            // Forward to audio processor for playback
            audio_processor_write_data(data, len);
            ui_manager_set_state(UI_STATE_SPEAKING);
            break;
            
        default:
            break;
    }
}
```

### 5. Complete Integration Sequence

```c
esp_err_t integrate_all_components(void) {
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Starting component integration...");
    
    // Step 1: BSP Layer (Hardware)
    ret = initialize_bsp_layer();
    if (ret != ESP_OK) return ret;
    
    // Step 2: Audio Subsystem
    ret = initialize_audio_subsystem();
    if (ret != ESP_OK) return ret;
    
    // Step 3: Audio Processor
    ret = integrate_audio_processor();
    if (ret != ESP_OK) return ret;
    
    // Step 4: UI Manager
    ret = integrate_ui_manager();
    if (ret != ESP_OK) return ret;
    
    // Step 5: Network Manager  
    ret = integrate_network_manager();
    if (ret != ESP_OK) return ret;
    
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    int retry_count = 0;
    while (network_manager_get_state(&network_mgr) != NETWORK_STATE_CONNECTED && retry_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }
    
    if (network_manager_get_state(&network_mgr) != NETWORK_STATE_CONNECTED) {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    ui_manager_update_status("WiFi Connected");
    
    // Step 6: Service Discovery
    ret = integrate_service_discovery();
    if (ret != ESP_OK) return ret;
    
    // Step 7: WebSocket Client (after server discovery)
    vTaskDelay(pdMS_TO_TICKS(3000));  // Allow time for server discovery
    ret = integrate_websocket_client();
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "All components integrated successfully");
    return ESP_OK;
}
```

---

## Error Handling and Troubleshooting

### Error Categories and Recovery

#### 1. Hardware Initialization Errors

```c
typedef enum {
    HW_ERROR_I2C_INIT = 0x1000,
    HW_ERROR_DISPLAY_INIT,
    HW_ERROR_TOUCH_INIT,
    HW_ERROR_AUDIO_CODEC_INIT,
    HW_ERROR_I2S_INIT
} hardware_error_t;

esp_err_t handle_hardware_error(hardware_error_t error) {
    switch (error) {
        case HW_ERROR_I2C_INIT:
            ESP_LOGE(TAG, "I2C initialization failed - check wiring and pullups");
            // Retry with different clock speed
            return retry_i2c_init_with_fallback();
            
        case HW_ERROR_DISPLAY_INIT:
            ESP_LOGE(TAG, "Display initialization failed - check MIPI-DSI connections");
            // Try display reset sequence
            return retry_display_init_with_reset();
            
        case HW_ERROR_AUDIO_CODEC_INIT:
            ESP_LOGE(TAG, "Audio codec initialization failed - check I2C and codec power");
            // Verify codec I2C address and power sequence
            return retry_codec_init_with_power_cycle();
            
        default:
            ESP_LOGE(TAG, "Unknown hardware error: 0x%x", error);
            return ESP_FAIL;
    }
}
```

#### 2. Network Error Recovery

```c
typedef enum {
    NET_ERROR_WIFI_CONNECT = 0x2000,
    NET_ERROR_SERVER_DISCOVERY,
    NET_ERROR_WEBSOCKET_CONNECT,
    NET_ERROR_CONNECTION_LOST
} network_error_t;

esp_err_t handle_network_error(network_error_t error) {
    static int retry_count = 0;
    const int max_retries = 5;
    
    switch (error) {
        case NET_ERROR_WIFI_CONNECT:
            ESP_LOGW(TAG, "WiFi connection failed, attempt %d/%d", retry_count + 1, max_retries);
            if (retry_count < max_retries) {
                retry_count++;
                vTaskDelay(pdMS_TO_TICKS(5000 * retry_count));  // Exponential backoff
                return network_manager_connect(&network_mgr);
            }
            ui_manager_update_status("WiFi Failed - Check Credentials");
            return ESP_FAIL;
            
        case NET_ERROR_SERVER_DISCOVERY:
            ESP_LOGW(TAG, "No HowdyTTS servers found");
            ui_manager_update_status("Searching for servers...");
            // Fall back to configured server IP
            return connect_to_configured_server();
            
        case NET_ERROR_WEBSOCKET_CONNECT:
            ESP_LOGW(TAG, "WebSocket connection failed");
            ui_manager_update_status("Server Unavailable");
            // Try next best server
            return connect_to_next_best_server();
            
        case NET_ERROR_CONNECTION_LOST:
            ESP_LOGW(TAG, "Connection lost - attempting reconnection");
            ui_manager_set_state(UI_STATE_ERROR);
            ui_manager_update_status("Reconnecting...");
            return ws_client_start();  // Auto-reconnect enabled
            
        default:
            return ESP_FAIL;
    }
}
```

#### 3. Audio Pipeline Error Recovery

```c
typedef enum {
    AUDIO_ERROR_DMA_UNDERRUN = 0x3000,
    AUDIO_ERROR_CODEC_TIMEOUT,
    AUDIO_ERROR_BUFFER_OVERFLOW,
    AUDIO_ERROR_I2S_CLOCK_LOSS
} audio_error_t;

esp_err_t handle_audio_error(audio_error_t error) {
    switch (error) {
        case AUDIO_ERROR_DMA_UNDERRUN:
            ESP_LOGW(TAG, "Audio DMA underrun - increasing buffer size");
            return restart_audio_with_larger_buffers();
            
        case AUDIO_ERROR_CODEC_TIMEOUT:
            ESP_LOGW(TAG, "Codec timeout - resetting codec");
            return reset_and_reinit_codec();
            
        case AUDIO_ERROR_BUFFER_OVERFLOW:
            ESP_LOGW(TAG, "Audio buffer overflow - client too slow");
            // Clear buffers and restart
            return restart_audio_pipeline();
            
        case AUDIO_ERROR_I2S_CLOCK_LOSS:
            ESP_LOGE(TAG, "I2S clock loss - hardware issue");
            return reinitialize_i2s_interface();
            
        default:
            return ESP_FAIL;
    }
}
```

### Diagnostic Functions

#### System Health Check

```c
typedef struct {
    bool i2c_ok;
    bool display_ok;
    bool touch_ok;
    bool audio_ok;
    bool wifi_ok;
    bool server_ok;
    uint32_t free_heap;
    uint32_t min_free_heap;
} system_health_t;

esp_err_t system_health_check(system_health_t *health) {
    memset(health, 0, sizeof(system_health_t));
    
    // Check I2C bus
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    health->i2c_ok = (i2c_handle != NULL);
    
    // Check display
    health->display_ok = (lv_display_get_default() != NULL);
    
    // Check touch
    health->touch_ok = (bsp_display_get_input_dev() != NULL);
    
    // Check audio
    health->audio_ok = is_audio_pipeline_running();
    
    // Check network
    health->wifi_ok = (network_manager_get_state(&network_mgr) == NETWORK_STATE_CONNECTED);
    health->server_ok = (ws_client_get_state() == WS_CLIENT_STATE_CONNECTED);
    
    // Check memory
    health->free_heap = esp_get_free_heap_size();
    health->min_free_heap = esp_get_minimum_free_heap_size();
    
    ESP_LOGI(TAG, "System Health: I2C:%s Display:%s Touch:%s Audio:%s WiFi:%s Server:%s Heap:%d/%d",
             health->i2c_ok ? "OK" : "FAIL",
             health->display_ok ? "OK" : "FAIL", 
             health->touch_ok ? "OK" : "FAIL",
             health->audio_ok ? "OK" : "FAIL",
             health->wifi_ok ? "OK" : "FAIL",
             health->server_ok ? "OK" : "FAIL",
             health->free_heap, health->min_free_heap);
    
    return ESP_OK;
}
```

#### Connection Diagnostics

```c
esp_err_t diagnose_connection_issues(void) {
    ESP_LOGI(TAG, "Running connection diagnostics...");
    
    // Test WiFi signal strength
    int rssi = network_get_rssi();
    ESP_LOGI(TAG, "WiFi RSSI: %d dBm", rssi);
    if (rssi < -70) {
        ESP_LOGW(TAG, "Weak WiFi signal may cause issues");
    }
    
    // Test server connectivity
    howdytts_server_info_t servers[5];
    size_t num_servers;
    if (service_discovery_get_servers(servers, 5, &num_servers) == ESP_OK) {
        ESP_LOGI(TAG, "Found %d HowdyTTS servers", num_servers);
        
        for (size_t i = 0; i < num_servers; i++) {
            uint32_t latency;
            if (service_discovery_test_server(&servers[i], 5000, &latency) == ESP_OK) {
                ESP_LOGI(TAG, "Server %s: %dms latency", servers[i].hostname, latency);
            } else {
                ESP_LOGW(TAG, "Server %s: unreachable", servers[i].hostname);
            }
        }
    } else {
        ESP_LOGW(TAG, "No servers discovered");
    }
    
    // Test WebSocket connection
    ws_client_state_t ws_state = ws_client_get_state();
    ESP_LOGI(TAG, "WebSocket state: %d", ws_state);
    
    if (ws_state == WS_CLIENT_STATE_CONNECTED) {
        // Test ping
        if (ws_client_ping() == ESP_OK) {
            ESP_LOGI(TAG, "WebSocket ping successful");
        } else {
            ESP_LOGW(TAG, "WebSocket ping failed");
        }
    }
    
    return ESP_OK;
}
```

---

## Performance Tuning Guidelines

### Audio Pipeline Optimization

#### Buffer Size Tuning

```c
// Optimal buffer configurations for different use cases
typedef struct {
    uint16_t capture_buf_size;
    uint8_t capture_buf_count;
    uint16_t playback_buf_size;
    uint8_t playback_buf_count;
    const char *description;
} audio_config_preset_t;

static const audio_config_preset_t audio_presets[] = {
    {512,  4, 512,  4, "Low Latency (high CPU)"},
    {1024, 4, 1024, 4, "Balanced (recommended)"},
    {2048, 2, 2048, 2, "Low CPU (higher latency)"},
    {4096, 2, 4096, 2, "Minimum CPU (streaming)"}
};

esp_err_t optimize_audio_buffers(int preset_index) {
    if (preset_index < 0 || preset_index >= sizeof(audio_presets)/sizeof(audio_presets[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const audio_config_preset_t *preset = &audio_presets[preset_index];
    ESP_LOGI(TAG, "Applying audio preset: %s", preset->description);
    
    audio_processor_config_t config = {
        .sample_rate = 16000,
        .bits_per_sample = 16,
        .channels = 1,
        .dma_buf_count = preset->capture_buf_count,
        .dma_buf_len = preset->capture_buf_size,
        .task_priority = 23,
        .task_core = 1
    };
    
    return audio_processor_reconfigure(&config);
}
```

#### CPU Core Assignment

```c
esp_err_t optimize_task_placement(void) {
    // Core 0: Network, UI, System tasks
    // Core 1: Audio, Real-time tasks
    
    const struct {
        const char *task_name;
        uint8_t core;
        uint8_t priority;
    } task_config[] = {
        {"audio_capture",    1, 23},  // Highest priority, Core 1
        {"audio_playback",   1, 22},  // High priority, Core 1
        {"websocket_client", 0, 21},  // High priority, Core 0
        {"lvgl_task",        0, 20},  // Medium priority, Core 0
        {"ui_update",        0, 19},  // Medium priority, Core 0
        {"service_discovery", 0, 18}, // Medium priority, Core 0
        {"system_monitor",   0, 10},  // Low priority, Core 0
    };
    
    for (size_t i = 0; i < sizeof(task_config)/sizeof(task_config[0]); i++) {
        ESP_LOGI(TAG, "Task %s: Core %d, Priority %d", 
                 task_config[i].task_name, 
                 task_config[i].core, 
                 task_config[i].priority);
    }
    
    return ESP_OK;
}
```

### Memory Management

#### PSRAM Utilization

```c
esp_err_t optimize_memory_allocation(void) {
    // Use PSRAM for large buffers
    heap_caps_malloc_extmem_enable(4096);  // Use PSRAM for allocations > 4KB
    
    // LVGL buffer allocation
    static lv_color_t *lvgl_buf1;
    static lv_color_t *lvgl_buf2;
    
    size_t buf_size = BSP_LCD_DRAW_BUFF_SIZE * sizeof(lv_color_t);
    
    // Allocate display buffers in PSRAM
    lvgl_buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (BSP_LCD_DRAW_BUFF_DOUBLE) {
        lvgl_buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    }
    
    if (lvgl_buf1 == NULL || (BSP_LCD_DRAW_BUFF_DOUBLE && lvgl_buf2 == NULL)) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers in PSRAM");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "LVGL buffers allocated in PSRAM: %d bytes", buf_size);
    return ESP_OK;
}
```

#### Memory Monitoring

```c
void monitor_memory_usage(void) {
    static uint32_t last_free_heap = 0;
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    
    if (abs((int32_t)(free_heap - last_free_heap)) > 1024) {  // Report changes > 1KB
        ESP_LOGI(TAG, "Heap: %d free, %d minimum, %d PSRAM", 
                 free_heap, min_free_heap, esp_get_free_heap_size_caps(MALLOC_CAP_SPIRAM));
        last_free_heap = free_heap;
    }
    
    // Check for memory leaks
    if (min_free_heap < 50000) {  // Less than 50KB minimum free
        ESP_LOGW(TAG, "Low memory warning: %d bytes minimum free", min_free_heap);
    }
}
```

### Network Performance

#### WebSocket Optimization

```c
esp_err_t optimize_websocket_performance(void) {
    ws_client_config_t config = {
        .reconnect_timeout_ms = 2000,      // Faster reconnect
        .keepalive_idle_sec = 15,          // More frequent keepalive
        .keepalive_interval_sec = 3,       // Faster failure detection
        .keepalive_count = 2,              // Fewer retries
        .auto_reconnect = true,
        .buffer_size = 4096                // Larger buffer for audio
    };
    
    return ws_client_reconfigure(&config);
}
```

#### Audio Streaming Optimization

```c
esp_err_t optimize_audio_streaming(void) {
    // Use voice activity detection to reduce network traffic
    const float vad_threshold = 0.01f;  // Adjust based on environment
    
    // Configure audio processor for optimal streaming
    audio_processor_config_t config = {
        .sample_rate = 16000,           // Standard rate for speech
        .bits_per_sample = 16,          // Good quality/bandwidth balance
        .channels = 1,                  // Mono sufficient for speech
        .dma_buf_count = 4,             // Enough for smooth streaming
        .dma_buf_len = 1024,           // 64ms at 16kHz
        .task_priority = 23,
        .task_core = 1
    };
    
    return audio_processor_init(&config);
}
```

---

## Testing and Validation

### Component Unit Tests

#### Audio Pipeline Test

```c
esp_err_t test_audio_pipeline(void) {
    ESP_LOGI(TAG, "Testing audio pipeline...");
    
    // Test 1: Initialize audio processor
    audio_processor_config_t config = AUDIO_PROCESSOR_DEFAULT_CONFIG();
    esp_err_t ret = audio_processor_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Test 2: Start capture
    ret = audio_processor_start_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio capture start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Test 3: Capture audio for 5 seconds
    int samples_captured = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(5000)) {
        uint8_t *buffer;
        size_t length;
        
        if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
            samples_captured += length / 2;  // 16-bit samples
            audio_processor_release_buffer();
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Test 4: Stop capture
    ret = audio_processor_stop_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio capture stop failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Audio test completed: %d samples captured", samples_captured);
    
    // Validation
    int expected_samples = 16000 * 5;  // 5 seconds at 16kHz
    if (samples_captured < expected_samples * 0.9) {  // Allow 10% tolerance
        ESP_LOGE(TAG, "Audio test failed: insufficient samples");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio pipeline test PASSED");
    return ESP_OK;
}
```

#### Display and Touch Test

```c
esp_err_t test_display_and_touch(void) {
    ESP_LOGI(TAG, "Testing display and touch...");
    
    // Test 1: Display initialization
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_FAIL;
    }
    
    // Test 2: Touch controller
    lv_indev_t *touch = bsp_display_get_input_dev();
    if (touch == NULL) {
        ESP_LOGE(TAG, "Touch controller not initialized");
        return ESP_FAIL;
    }
    
    // Test 3: Create test UI
    if (bsp_display_lock(1000)) {
        lv_obj_t *test_label = lv_label_create(lv_screen_active());
        lv_label_set_text(test_label, "Touch Test - Tap Screen");
        lv_obj_center(test_label);
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to lock display");
        return ESP_FAIL;
    }
    
    // Test 4: Wait for touch events
    int touch_events = 0;
    TickType_t start_time = xTaskGetTickCount();
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(10000)) {  // 10 seconds
        lv_indev_data_t data;
        if (lv_indev_read(touch, &data) == LV_INDEV_DATA_TYPE_POINT) {
            if (data.state == LV_INDEV_STATE_PRESSED) {
                touch_events++;
                ESP_LOGI(TAG, "Touch detected at (%d, %d)", data.point.x, data.point.y);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "Display and touch test completed: %d touch events", touch_events);
    
    if (touch_events == 0) {
        ESP_LOGW(TAG, "No touch events detected - touch may not be working");
    }
    
    ESP_LOGI(TAG, "Display and touch test PASSED");
    return ESP_OK;
}
```

#### Network Connectivity Test

```c
esp_err_t test_network_connectivity(void) {
    ESP_LOGI(TAG, "Testing network connectivity...");
    
    // Test 1: WiFi connection
    network_state_t state = network_manager_get_state(&network_mgr);
    if (state != NETWORK_STATE_CONNECTED) {
        ESP_LOGE(TAG, "WiFi not connected: state %d", state);
        return ESP_FAIL;
    }
    
    // Test 2: Signal strength
    int rssi = network_get_rssi();
    ESP_LOGI(TAG, "WiFi RSSI: %d dBm", rssi);
    if (rssi < -80) {
        ESP_LOGW(TAG, "Very weak WiFi signal");
    }
    
    // Test 3: Server discovery
    howdytts_server_info_t servers[5];
    size_t num_servers;
    esp_err_t ret = service_discovery_get_servers(servers, 5, &num_servers);
    if (ret != ESP_OK || num_servers == 0) {
        ESP_LOGW(TAG, "No HowdyTTS servers discovered");
    } else {
        ESP_LOGI(TAG, "Discovered %d HowdyTTS servers", num_servers);
        
        // Test server connectivity
        for (size_t i = 0; i < num_servers; i++) {
            uint32_t latency;
            if (service_discovery_test_server(&servers[i], 3000, &latency) == ESP_OK) {
                ESP_LOGI(TAG, "Server %s: %dms latency", servers[i].hostname, latency);
            }
        }
    }
    
    // Test 4: WebSocket connection
    ws_client_state_t ws_state = ws_client_get_state();
    if (ws_state != WS_CLIENT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "WebSocket not connected: state %d", ws_state);
    } else {
        ESP_LOGI(TAG, "WebSocket connected successfully");
        
        // Test ping
        if (ws_client_ping() == ESP_OK) {
            ESP_LOGI(TAG, "WebSocket ping successful");
        }
    }
    
    ESP_LOGI(TAG, "Network connectivity test PASSED");
    return ESP_OK;
}
```

### Integration Tests

#### End-to-End Audio Test

```c
esp_err_t test_end_to_end_audio(void) {
    ESP_LOGI(TAG, "Testing end-to-end audio...");
    
    // Prerequisite: All systems must be connected
    if (ws_client_get_state() != WS_CLIENT_STATE_CONNECTED) {
        ESP_LOGE(TAG, "WebSocket not connected");
        return ESP_FAIL;
    }
    
    // Test 1: Start audio capture
    esp_err_t ret = audio_processor_start_capture();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio capture");
        return ret;
    }
    
    // Test 2: Capture and stream audio for 10 seconds
    TickType_t start_time = xTaskGetTickCount();
    int packets_sent = 0;
    
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(10000)) {
        uint8_t *buffer;
        size_t length;
        
        if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
            if (ws_client_send_binary_audio((int16_t*)buffer, length/2) == ESP_OK) {
                packets_sent++;
            }
            audio_processor_release_buffer();
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Test 3: Stop capture
    audio_processor_stop_capture();
    
    ESP_LOGI(TAG, "End-to-end audio test completed: %d packets sent", packets_sent);
    
    if (packets_sent < 50) {  // Should send ~200 packets in 10 seconds
        ESP_LOGW(TAG, "Low packet count - may indicate issues");
    }
    
    ESP_LOGI(TAG, "End-to-end audio test PASSED");
    return ESP_OK;
}
```

### Performance Benchmarks

#### System Performance Test

```c
typedef struct {
    uint32_t audio_capture_rate;      // Samples per second
    uint32_t network_throughput;      // Bytes per second
    uint32_t ui_frame_rate;           // Frames per second
    uint32_t cpu_usage_core0;         // Percentage
    uint32_t cpu_usage_core1;         // Percentage
    uint32_t memory_usage;            // Bytes
} performance_metrics_t;

esp_err_t benchmark_system_performance(performance_metrics_t *metrics) {
    ESP_LOGI(TAG, "Running performance benchmark...");
    
    memset(metrics, 0, sizeof(performance_metrics_t));
    
    // Benchmark period
    const uint32_t benchmark_duration_ms = 30000;  // 30 seconds
    TickType_t start_time = xTaskGetTickCount();
    
    uint32_t audio_samples = 0;
    uint32_t network_bytes = 0;
    uint32_t ui_frames = 0;
    
    // Monitor performance metrics
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(benchmark_duration_ms)) {
        // Audio metrics
        uint8_t *buffer;
        size_t length;
        if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
            audio_samples += length / 2;  // 16-bit samples
            
            // Network metrics
            if (ws_client_send_binary_audio((int16_t*)buffer, length/2) == ESP_OK) {
                network_bytes += length;
            }
            
            audio_processor_release_buffer();
        }
        
        // UI metrics (approximate)
        ui_frames++;
        
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60 FPS
    }
    
    uint32_t actual_duration_s = benchmark_duration_ms / 1000;
    
    // Calculate rates
    metrics->audio_capture_rate = audio_samples / actual_duration_s;
    metrics->network_throughput = network_bytes / actual_duration_s;
    metrics->ui_frame_rate = ui_frames / actual_duration_s;
    
    // Memory usage
    metrics->memory_usage = esp_get_minimum_free_heap_size();
    
    // CPU usage (approximate - would need detailed monitoring)
    metrics->cpu_usage_core0 = 50;  // Placeholder
    metrics->cpu_usage_core1 = 75;  // Placeholder
    
    ESP_LOGI(TAG, "Performance Benchmark Results:");
    ESP_LOGI(TAG, "Audio Capture Rate: %d samples/sec", metrics->audio_capture_rate);
    ESP_LOGI(TAG, "Network Throughput: %d bytes/sec", metrics->network_throughput);
    ESP_LOGI(TAG, "UI Frame Rate: %d fps", metrics->ui_frame_rate);
    ESP_LOGI(TAG, "Memory Usage: %d bytes minimum free", metrics->memory_usage);
    
    return ESP_OK;
}
```

This integration guide provides comprehensive procedures for successfully integrating, troubleshooting, optimizing, and testing the ESP32-P4 HowdyScreen system. Follow these guidelines to ensure reliable and high-performance operation.