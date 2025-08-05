# ESP32-P4 HowdyScreen API Reference

## Overview

This document provides detailed API documentation for all components in the ESP32-P4 HowdyScreen project. Each component exposes a clean C API with consistent error handling and configuration patterns.

## Error Handling Convention

All APIs follow ESP-IDF error handling conventions:
- `ESP_OK` (0) indicates success
- Negative values indicate specific error conditions
- `esp_err_t` return type for all functions that can fail
- Error codes defined in component-specific headers

---

## Audio Processor Component

### Header: `audio_processor.h`

The audio processor handles real-time audio capture, processing, and playback using the ESP32-P4's I2S interface.

#### Configuration

```c
typedef struct {
    uint32_t sample_rate;       // Sample rate (16000 Hz recommended)
    uint8_t bits_per_sample;    // Bits per sample (16 recommended)
    uint8_t channels;           // Number of channels (1 for mono)
    uint8_t dma_buf_count;      // DMA buffer count (2-8)
    uint16_t dma_buf_len;       // DMA buffer length (512-4096)
    uint8_t task_priority;      // Audio task priority (20-25)
    uint8_t task_core;          // CPU core for audio task (0-1)
} audio_processor_config_t;

// Default configuration
#define AUDIO_PROCESSOR_DEFAULT_CONFIG() { \
    .sample_rate = 16000, \
    .bits_per_sample = 16, \
    .channels = 1, \
    .dma_buf_count = 4, \
    .dma_buf_len = 1024, \
    .task_priority = 23, \
    .task_core = 1 \
}
```

#### Events and Callbacks

```c
typedef enum {
    AUDIO_EVENT_STARTED,        // Audio capture/playback started
    AUDIO_EVENT_STOPPED,        // Audio capture/playback stopped
    AUDIO_EVENT_DATA_READY,     // New audio data available
    AUDIO_EVENT_ERROR           // Audio pipeline error
} audio_event_t;

typedef void (*audio_event_callback_t)(audio_event_t event, void *data, size_t len);
```

#### Core Functions

##### Initialization

```c
esp_err_t audio_processor_init(const audio_processor_config_t *config);
```
**Description**: Initialize the audio processor with specified configuration.
**Parameters**: 
- `config`: Pointer to configuration structure
**Returns**: `ESP_OK` on success, error code on failure
**Example**:
```c
audio_processor_config_t config = AUDIO_PROCESSOR_DEFAULT_CONFIG();
config.sample_rate = 22050;
esp_err_t ret = audio_processor_init(&config);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize audio processor: %s", esp_err_to_name(ret));
}
```

##### Capture Control

```c
esp_err_t audio_processor_start_capture(void);
esp_err_t audio_processor_stop_capture(void);
```
**Description**: Start/stop audio capture from microphone
**Returns**: `ESP_OK` on success
**Notes**: 
- Must call `audio_processor_init()` first
- Capture runs in dedicated FreeRTOS task
- Audio data available via callback or `audio_processor_get_buffer()`

##### Playback Control

```c
esp_err_t audio_processor_start_playback(void);
esp_err_t audio_processor_stop_playback(void);
```
**Description**: Start/stop audio playback to speaker
**Returns**: `ESP_OK` on success
**Notes**: Playback data provided via `audio_processor_write_data()`

##### Data Access

```c
esp_err_t audio_processor_get_buffer(uint8_t **buffer, size_t *length);
esp_err_t audio_processor_release_buffer(void);
```
**Description**: Get captured audio buffer for processing
**Parameters**:
- `buffer`: Output pointer to audio data
- `length`: Output buffer length in bytes
**Returns**: `ESP_OK` if data available, `ESP_ERR_NOT_FOUND` if no data
**Notes**: Must call `audio_processor_release_buffer()` after processing

```c
esp_err_t audio_processor_write_data(const uint8_t *data, size_t length);
```
**Description**: Write audio data for playback
**Parameters**:
- `data`: Audio data buffer (16-bit PCM samples)
- `length`: Data length in bytes
**Returns**: `ESP_OK` on success
**Notes**: Data queued for playback, may block if buffer full

##### Event Management

```c
esp_err_t audio_processor_set_callback(audio_event_callback_t callback);
```
**Description**: Set callback function for audio events
**Parameters**: `callback`: Function called on audio events
**Returns**: `ESP_OK` on success

#### Usage Example

```c
#include "audio_processor.h"

static void audio_event_handler(audio_event_t event, void *data, size_t len) {
    switch (event) {
        case AUDIO_EVENT_DATA_READY:
            ESP_LOGI(TAG, "Audio data ready: %d bytes", len);
            // Process audio data
            break;
        case AUDIO_EVENT_ERROR:
            ESP_LOGE(TAG, "Audio error occurred");
            break;
        default:
            break;
    }
}

void app_main() {
    // Initialize audio processor
    audio_processor_config_t config = AUDIO_PROCESSOR_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(audio_processor_init(&config));
    
    // Set event callback
    ESP_ERROR_CHECK(audio_processor_set_callback(audio_event_handler));
    
    // Start capture
    ESP_ERROR_CHECK(audio_processor_start_capture());
    
    // Process captured audio
    uint8_t *buffer;
    size_t length;
    while (1) {
        if (audio_processor_get_buffer(&buffer, &length) == ESP_OK) {
            // Process audio data
            process_audio_data(buffer, length);
            audio_processor_release_buffer();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## UI Manager Component

### Header: `ui_manager.h`

The UI manager provides LVGL-based interface for the circular display with touch support.

#### State Management

```c
typedef enum {
    UI_STATE_INIT,          // Initializing
    UI_STATE_IDLE,          // Ready, waiting for input
    UI_STATE_LISTENING,     // Capturing audio
    UI_STATE_PROCESSING,    // Processing on server
    UI_STATE_SPEAKING,      // Playing back response
    UI_STATE_ERROR          // Error state
} ui_state_t;
```

#### UI Elements Structure

```c
typedef struct {
    lv_obj_t *screen;           // Main screen object
    lv_obj_t *audio_meter;      // Real-time audio level display
    lv_obj_t *status_label;     // Connection/processing status
    lv_obj_t *wifi_label;       // Network status indicator
    lv_obj_t *level_arc;        // Circular audio level meter
    lv_obj_t *center_button;    // Mute/unmute control
    lv_obj_t *howdy_gif;        // Processing animation
    lv_obj_t *mic_icon;         // Microphone status icon
    ui_state_t current_state;   // Current UI state
    bool muted;                 // Mute status
    int wifi_signal_strength;   // WiFi signal (0-100)
} ui_manager_t;
```

#### Core Functions

##### Initialization

```c
esp_err_t ui_manager_init(void);
```
**Description**: Initialize LVGL UI and create all UI elements
**Returns**: `ESP_OK` on success
**Notes**: Must call after BSP display initialization

##### State Management

```c
esp_err_t ui_manager_set_state(ui_state_t state);
ui_state_t ui_manager_get_state(void);
```
**Description**: Set/get current UI state
**Parameters**: `state`: New UI state to set
**Returns**: Current or new state
**Notes**: State changes trigger UI animations and color updates

##### Audio Visualization

```c
esp_err_t ui_manager_update_audio_level(int level);
```
**Description**: Update circular audio level meter
**Parameters**: `level`: Audio level (0-100)
**Returns**: `ESP_OK` on success
**Notes**: 
- Updates in real-time during capture/playback
- Smoothed animation for better visual experience

##### Network Status

```c
esp_err_t ui_manager_set_wifi_strength(int strength);
```
**Description**: Update WiFi signal strength indicator
**Parameters**: `strength`: Signal strength (0-100)
**Returns**: `ESP_OK` on success
**Notes**: Updates WiFi icon and color based on signal quality

##### User Controls

```c
esp_err_t ui_manager_set_mute(bool muted);
bool ui_manager_is_muted(void);
```
**Description**: Set/get mute state
**Parameters**: `muted`: True to mute, false to unmute
**Returns**: Current mute state
**Notes**: Updates center button appearance and blocks audio

##### Status Display

```c
esp_err_t ui_manager_update_status(const char *status);
```
**Description**: Update status text display
**Parameters**: `status`: Status message string
**Returns**: `ESP_OK` on success
**Notes**: Displayed at bottom of screen, auto-scrolling for long text

#### Usage Example

```c
#include "ui_manager.h"

void app_main() {
    // Initialize display first (BSP)
    lv_display_t *display = bsp_display_start();
    
    // Initialize UI manager
    ESP_ERROR_CHECK(ui_manager_init());
    
    // Set initial state
    ui_manager_set_state(UI_STATE_IDLE);
    ui_manager_update_status("Ready");
    
    // Update in main loop
    while (1) {
        // Update audio level (example)
        int audio_level = get_current_audio_level();
        ui_manager_update_audio_level(audio_level);
        
        // Update WiFi status
        int wifi_rssi = network_get_rssi();
        int wifi_strength = rssi_to_percentage(wifi_rssi);
        ui_manager_set_wifi_strength(wifi_strength);
        
        // State transitions based on system events
        if (voice_detected) {
            ui_manager_set_state(UI_STATE_LISTENING);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## WebSocket Client Component

### Header: `websocket_client.h`

The WebSocket client handles HowdyTTS protocol communication including audio streaming and control messages.

#### Configuration

```c
typedef struct {
    char server_uri[256];           // WebSocket server URI
    uint16_t reconnect_timeout_ms;  // Reconnect timeout
    uint16_t keepalive_idle_sec;    // Keepalive idle time
    uint16_t keepalive_interval_sec;// Keepalive interval
    uint16_t keepalive_count;       // Keepalive probe count
    bool auto_reconnect;            // Enable auto-reconnect
    uint32_t buffer_size;           // Send/receive buffer size
} ws_client_config_t;

#define WS_CLIENT_DEFAULT_CONFIG(uri) { \
    .server_uri = uri, \
    .reconnect_timeout_ms = 5000, \
    .keepalive_idle_sec = 30, \
    .keepalive_interval_sec = 5, \
    .keepalive_count = 3, \
    .auto_reconnect = true, \
    .buffer_size = 2048 \
}
```

#### State and Message Types

```c
typedef enum {
    WS_CLIENT_STATE_DISCONNECTED = 0,
    WS_CLIENT_STATE_CONNECTING,
    WS_CLIENT_STATE_CONNECTED,
    WS_CLIENT_STATE_ERROR
} ws_client_state_t;

typedef enum {
    WS_MSG_TYPE_AUDIO_STREAM = 0,   // Binary audio data
    WS_MSG_TYPE_TTS_RESPONSE,       // TTS audio response
    WS_MSG_TYPE_CONTROL,            // Control messages
    WS_MSG_TYPE_STATUS              // Status updates
} ws_message_type_t;
```

#### Event Handling

```c
typedef void (*ws_event_callback_t)(ws_client_state_t state, 
                                   ws_message_type_t msg_type, 
                                   const uint8_t *data, 
                                   size_t len);
```

#### Core Functions

##### Initialization and Connection

```c
esp_err_t ws_client_init(const ws_client_config_t *config, 
                        ws_event_callback_t event_callback);
esp_err_t ws_client_start(void);
esp_err_t ws_client_stop(void);
```
**Description**: Initialize, start, and stop WebSocket client
**Parameters**: 
- `config`: Client configuration
- `event_callback`: Event handler function
**Returns**: `ESP_OK` on success

##### Audio Streaming

```c
esp_err_t ws_client_send_audio(const int16_t *audio_data, 
                              size_t samples, 
                              uint32_t sample_rate);
```
**Description**: Send audio data to HowdyTTS server
**Parameters**:
- `audio_data`: PCM audio samples (16-bit)
- `samples`: Number of samples
- `sample_rate`: Sample rate in Hz
**Returns**: `ESP_OK` on success
**Notes**: Audio data sent as binary WebSocket message

```c
esp_err_t ws_client_send_binary_audio(const int16_t *buffer, size_t samples);
```
**Description**: Send raw binary audio data
**Parameters**:
- `buffer`: Audio buffer
- `samples`: Sample count
**Returns**: `ESP_OK` on success
**Notes**: Direct binary transmission for minimal overhead

```c
esp_err_t ws_client_handle_binary_response(const uint8_t *data, size_t length);
```
**Description**: Handle binary audio response from server
**Parameters**:
- `data`: Binary audio data
- `length`: Data length in bytes
**Returns**: `ESP_OK` on success
**Notes**: Typically contains TTS audio for playback

##### Text Communication

```c
esp_err_t ws_client_send_text(const char *text);
```
**Description**: Send text message to server
**Parameters**: `text`: Text message string
**Returns**: `ESP_OK` on success
**Notes**: Used for control messages and configuration

##### Status and Diagnostics

```c
ws_client_state_t ws_client_get_state(void);
bool ws_client_is_audio_ready(void);
esp_err_t ws_client_ping(void);
```
**Description**: Get connection state, check audio readiness, send keepalive
**Returns**: Current state, readiness status, ping result

```c
esp_err_t ws_client_get_stats(uint32_t *bytes_sent, 
                             uint32_t *bytes_received, 
                             uint32_t *reconnect_count);
```
**Description**: Get connection statistics
**Parameters**: Output pointers for statistics
**Returns**: `ESP_OK` on success

#### Usage Example

```c
#include "websocket_client.h"

static void ws_event_handler(ws_client_state_t state, 
                           ws_message_type_t msg_type, 
                           const uint8_t *data, 
                           size_t len) {
    switch (state) {
        case WS_CLIENT_STATE_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            break;
        case WS_CLIENT_STATE_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket disconnected");
            break;
        default:
            break;
    }
    
    switch (msg_type) {
        case WS_MSG_TYPE_TTS_RESPONSE:
            ESP_LOGI(TAG, "Received TTS audio: %d bytes", len);
            audio_processor_write_data(data, len);
            break;
        default:
            break;
    }
}

void websocket_task(void *pvParameters) {
    ws_client_config_t config = WS_CLIENT_DEFAULT_CONFIG("ws://192.168.1.100:8765");
    
    ESP_ERROR_CHECK(ws_client_init(&config, ws_event_handler));
    ESP_ERROR_CHECK(ws_client_start());
    
    // Send audio data periodically
    int16_t audio_buffer[1024];
    while (1) {
        if (ws_client_is_audio_ready()) {
            // Get audio from processor
            if (get_audio_data(audio_buffer, 1024) > 0) {
                ws_client_send_binary_audio(audio_buffer, 1024);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

---

## Service Discovery Component

### Header: `service_discovery.h`

The service discovery component uses mDNS to automatically locate HowdyTTS servers on the network.

#### Server Information

```c
typedef struct {
    char ip_addr[16];        // IPv4 address string
    uint16_t port;           // WebSocket port
    char hostname[64];       // Server hostname
    char version[16];        // Server version
    bool secure;             // Whether to use WSS
    uint32_t last_seen;      // Last seen timestamp (ms)
} howdytts_server_info_t;

typedef void (*service_discovered_callback_t)(const howdytts_server_info_t *server_info);
```

#### Core Functions

##### Initialization

```c
esp_err_t service_discovery_init(service_discovered_callback_t callback);
```
**Description**: Initialize mDNS service discovery
**Parameters**: `callback`: Function called when servers discovered
**Returns**: `ESP_OK` on success
**Notes**: Must initialize mDNS service first

##### Scanning

```c
esp_err_t service_discovery_start_scan(uint32_t scan_duration_ms);
esp_err_t service_discovery_stop_scan(void);
```
**Description**: Start/stop scanning for HowdyTTS servers
**Parameters**: `scan_duration_ms`: Scan duration (0 = continuous)
**Returns**: `ESP_OK` on success
**Notes**: Callback invoked for each discovered server

##### Server Management

```c
esp_err_t service_discovery_get_servers(howdytts_server_info_t *servers, 
                                       size_t max_servers, 
                                       size_t *num_servers);
```
**Description**: Get list of discovered servers
**Parameters**:
- `servers`: Output array for server info
- `max_servers`: Maximum servers to return
- `num_servers`: Actual number of servers found
**Returns**: `ESP_OK` on success

```c
esp_err_t service_discovery_get_best_server(howdytts_server_info_t *server_info);
```
**Description**: Get best server (lowest latency, most recent)
**Parameters**: `server_info`: Output server information
**Returns**: `ESP_OK` on success, `ESP_ERR_NOT_FOUND` if no servers
**Notes**: Uses latency testing and recency to determine best option

##### Server Testing

```c
esp_err_t service_discovery_test_server(const howdytts_server_info_t *server_info, 
                                       uint32_t timeout_ms, 
                                       uint32_t *latency_ms);
```
**Description**: Test connectivity to specific server
**Parameters**:
- `server_info`: Server to test
- `timeout_ms`: Test timeout
- `latency_ms`: Output measured latency
**Returns**: `ESP_OK` if reachable
**Notes**: Performs actual network connectivity test

##### Client Advertisement

```c
esp_err_t service_discovery_advertise_client(const char *device_name, 
                                           const char *capabilities);
esp_err_t service_discovery_stop_advertising(void);
```
**Description**: Advertise this ESP32 as HowdyTTS client
**Parameters**:
- `device_name`: Name to advertise
- `capabilities`: Client capabilities string
**Returns**: `ESP_OK` on success
**Notes**: Allows servers to discover this client

#### Usage Example

```c
#include "service_discovery.h"

static void server_discovered(const howdytts_server_info_t *server_info) {
    ESP_LOGI(TAG, "Discovered server: %s at %s:%d", 
             server_info->hostname, 
             server_info->ip_addr, 
             server_info->port);
    
    // Test server connectivity
    uint32_t latency;
    if (service_discovery_test_server(server_info, 5000, &latency) == ESP_OK) {
        ESP_LOGI(TAG, "Server latency: %d ms", latency);
        
        // Use this server if it's the best option
        howdytts_server_info_t best_server;
        if (service_discovery_get_best_server(&best_server) == ESP_OK) {
            if (strcmp(best_server.ip_addr, server_info->ip_addr) == 0) {
                connect_to_server(&best_server);
            }
        }
    }
}

void discovery_task(void *pvParameters) {
    ESP_ERROR_CHECK(service_discovery_init(server_discovered));
    
    // Advertise this client
    ESP_ERROR_CHECK(service_discovery_advertise_client("HowdyScreen", "audio,display"));
    
    // Start continuous scanning
    ESP_ERROR_CHECK(service_discovery_start_scan(0));
    
    // Periodic server list update
    while (1) {
        howdytts_server_info_t servers[10];
        size_t num_servers;
        
        if (service_discovery_get_servers(servers, 10, &num_servers) == ESP_OK) {
            ESP_LOGI(TAG, "Found %d servers", num_servers);
            for (size_t i = 0; i < num_servers; i++) {
                ESP_LOGI(TAG, "Server %d: %s", i, servers[i].hostname);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
    }
}
```

---

## BSP (Board Support Package) Component

### Header: `bsp/esp32_p4_wifi6_touch_lcd_xc.h`

The BSP provides hardware abstraction for the ESP32-P4-WiFi6-Touch-LCD-XC development board.

#### Capabilities

```c
#define BSP_CAPS_DISPLAY        1
#define BSP_CAPS_TOUCH          1
#define BSP_CAPS_BUTTONS        0
#define BSP_CAPS_AUDIO          1
#define BSP_CAPS_AUDIO_SPEAKER  1
#define BSP_CAPS_AUDIO_MIC      1
#define BSP_CAPS_SDCARD         1
#define BSP_CAPS_IMU            0
```

#### I2C Interface

```c
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_deinit(void);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
```
**Description**: Initialize/deinitialize I2C bus, get handle
**Returns**: `ESP_OK` on success, I2C handle
**Notes**: Shared bus for audio codec and touch controller

#### Audio Interface

```c
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
```
**Description**: Initialize I2S audio and codec devices
**Parameters**: `i2s_config`: I2S configuration (NULL for defaults)
**Returns**: `ESP_OK` on success, codec device handles
**Notes**: 
- Default: Mono, duplex, 16-bit, 22050 Hz
- ES8311 codec for both capture and playback

#### Display Interface

```c
lv_display_t *bsp_display_start(void);
lv_display_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);
lv_indev_t *bsp_display_get_input_dev(void);
```
**Description**: Initialize display and get LVGL objects
**Parameters**: `cfg`: Optional display configuration
**Returns**: LVGL display and input device handles
**Notes**: 800x800 round LCD with CST9217 touch controller

```c
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation);
```
**Description**: LVGL mutex management and display rotation
**Parameters**: 
- `timeout_ms`: Lock timeout (0 = block indefinitely)
- `rotation`: Display rotation angle
**Notes**: Required for thread-safe LVGL operations

#### Storage Interface

```c
esp_err_t bsp_spiffs_mount(void);
esp_err_t bsp_spiffs_unmount(void);
esp_err_t bsp_sdcard_mount(void);
esp_err_t bsp_sdcard_unmount(void);
```
**Description**: Mount/unmount SPIFFS and SD card filesystems
**Returns**: `ESP_OK` on success
**Notes**: 
- SPIFFS mount point: `/spiffs`
- SD card mount point: `/sdcard`

#### Usage Example

```c
#include "bsp/esp32_p4_wifi6_touch_lcd_xc.h"

void app_main() {
    // Initialize I2C bus
    ESP_ERROR_CHECK(bsp_i2c_init());
    
    // Initialize audio
    i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    ESP_ERROR_CHECK(bsp_audio_init(&i2s_config));
    
    // Initialize codec devices
    esp_codec_dev_handle_t speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_handle_t microphone = bsp_audio_codec_microphone_init();
    
    // Initialize display
    lv_display_t *display = bsp_display_start();
    lv_indev_t *touch = bsp_display_get_input_dev();
    
    // Mount storage
    ESP_ERROR_CHECK(bsp_sdcard_mount());
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    
    // Use LVGL with proper locking
    if (bsp_display_lock(1000)) {
        lv_obj_t *label = lv_label_create(lv_screen_active());
        lv_label_set_text(label, "Hello HowdyScreen!");
        bsp_display_unlock();
    }
}
```

---

## Integration Patterns

### Component Interaction

Components are designed to work together through well-defined interfaces:

1. **Audio → WebSocket**: Audio processor provides PCM data to WebSocket client
2. **WebSocket → Audio**: WebSocket client sends TTS audio to audio processor
3. **Service Discovery → WebSocket**: Discovery provides server information to WebSocket client
4. **All Components → UI**: All components update UI manager with status information

### Event-Driven Architecture

```c
typedef enum {
    SYSTEM_EVENT_WIFI_CONNECTED,
    SYSTEM_EVENT_SERVER_DISCOVERED,
    SYSTEM_EVENT_WEBSOCKET_CONNECTED,
    SYSTEM_EVENT_AUDIO_READY,
    SYSTEM_EVENT_ERROR
} system_event_t;

typedef struct {
    system_event_t event;
    void *data;
    size_t data_len;
} system_event_data_t;

// Global event handler
void system_event_handler(system_event_data_t *event_data);
```

### Error Propagation

All components use consistent error handling:

```c
// Component-specific error codes
#define AUDIO_ERR_BASE                  0x8000
#define AUDIO_ERR_NOT_INITIALIZED      (AUDIO_ERR_BASE + 1)
#define AUDIO_ERR_INVALID_STATE        (AUDIO_ERR_BASE + 2)
#define AUDIO_ERR_DMA_FAILED           (AUDIO_ERR_BASE + 3)

// Error reporting function
typedef void (*error_callback_t)(esp_err_t error, const char *component, const char *description);
```

This API reference provides comprehensive documentation for integrating and using all components of the ESP32-P4 HowdyScreen system.