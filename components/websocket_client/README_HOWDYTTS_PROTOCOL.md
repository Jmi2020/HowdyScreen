# HowdyTTS Network Protocol Implementation for ESP32-P4

This document provides comprehensive details on the HowdyTTS network protocol implementation designed specifically for the ESP32-P4 HowdyScreen device.

## Overview

The HowdyTTS protocol suite consists of three main communication channels:

1. **UDP Discovery Protocol (Port 8001)** - Service discovery and device registration
2. **UDP Audio Streaming (Port 8000)** - Real-time OPUS-compressed audio streaming
3. **HTTP State Server (Port 8080+)** - Bidirectional state synchronization

## 1. UDP Discovery Protocol (Port 8001)

### Message Format

All discovery packets use the following header structure:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;                         // 0x484F5744 ("HOWD")
    uint8_t version;                        // Protocol version (1)
    uint8_t packet_type;                    // Message type (see below)
    uint16_t payload_length;                // Length of payload after header
    uint32_t sequence;                      // Sequence number for tracking
    uint32_t timestamp;                     // Timestamp (ms since boot)
    uint8_t reserved[4];                    // Reserved for future use
} howdytts_discovery_header_t;
```

### Discovery Flow

#### 1. Server Discovery Request (ESP32-P4 → Broadcast)

**Packet Type:** `HOWDYTTS_DISCOVERY_REQUEST` (0x01)
**Payload:** None
**Example:**

```
Magic: 0x484F5744 ("HOWD")
Version: 1
Type: 0x01 (DISCOVERY_REQUEST)
Payload Length: 0
Sequence: 12345678
Timestamp: 987654321
Reserved: [0,0,0,0]
```

#### 2. Server Discovery Response (HowdyTTS Server → ESP32-P4)

**Packet Type:** `HOWDYTTS_DISCOVERY_RESPONSE` (0x02)
**Payload:** `howdytts_server_info_payload_t`

```c
typedef struct __attribute__((packed)) {
    char server_name[64];                   // "HowdyTTS-MacStudio"
    char server_version[16];                // "1.2.3"
    uint16_t http_port;                     // HTTP API port
    uint16_t websocket_port;                // WebSocket port (if available)
    uint8_t rooms_count;                    // Number of supported rooms
    char rooms[5][16];                      // Room names array
    uint32_t server_uptime;                 // Server uptime in seconds
    uint8_t max_devices;                    // Maximum concurrent devices
    uint8_t current_devices;                // Currently connected devices
    uint8_t supported_codecs;               // Bitmask: 0x01=PCM, 0x02=OPUS
    uint8_t server_load;                    // Server load percentage (0-100)
} howdytts_server_info_payload_t;
```

**Example Response:**
```json
{
    "server_name": "HowdyTTS-MacStudio",
    "server_version": "1.2.3",
    "http_port": 3000,
    "websocket_port": 3001,
    "rooms_count": 3,
    "rooms": ["office", "kitchen", "living_room"],
    "server_uptime": 86400,
    "max_devices": 10,
    "current_devices": 2,
    "supported_codecs": 0x03,
    "server_load": 15
}
```

#### 3. Device Registration (ESP32-P4 → HowdyTTS Server)

**Packet Type:** `HOWDYTTS_REGISTER_DEVICE` (0x03)
**Payload:** `howdytts_device_registration_t`

```c
typedef struct __attribute__((packed)) {
    char device_id[32];                     // "esp32p4-a1b2c3"
    char device_name[32];                   // "HowdyScreen"
    char capabilities[64];                  // Device capabilities
    char room[16];                          // "office"
    uint16_t http_port;                     // 8080
    uint16_t udp_audio_port;               // 8000
    uint8_t audio_sample_rate_khz;         // 16 (for 16kHz)
    uint8_t audio_channels;                // 1 (mono)
    uint8_t audio_bits_per_sample;         // 16
    uint8_t opus_supported;                // 1 (yes)
    uint32_t device_uptime;                // Device uptime
    uint8_t battery_level;                 // 255 (unknown/powered)
    int8_t wifi_rssi;                      // WiFi signal strength
} howdytts_device_registration_t;
```

**Example Registration:**
```json
{
    "device_id": "esp32p4-a1b2c3",
    "device_name": "HowdyScreen",
    "capabilities": "audio_streaming,state_sync,opus_encoding,touch_display",
    "room": "office",
    "http_port": 8080,
    "udp_audio_port": 8000,
    "audio_config": {
        "sample_rate": 16000,
        "channels": 1,
        "bits_per_sample": 16,
        "opus_supported": true
    },
    "device_uptime": 3600,
    "battery_level": null,
    "wifi_rssi": -45
}
```

#### 4. Registration Acknowledgment (HowdyTTS Server → ESP32-P4)

**Packet Type:** `HOWDYTTS_REGISTER_ACK` (0x04)
**Payload:** Registration status and assigned configuration

## 2. UDP Audio Streaming Protocol (Port 8000)

### Audio Packet Format

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;                         // 0x41554449 ("AUDI")
    uint8_t version;                        // Protocol version
    uint8_t packet_type;                    // Audio packet type
    uint16_t payload_length;                // Audio payload length
    uint32_t sequence_number;               // Packet sequence number
    uint32_t timestamp_ms;                  // Audio timestamp (ms)
    uint16_t sample_rate;                   // Sample rate (Hz)
    uint8_t channels;                       // Number of channels
    uint8_t bits_per_sample;                // Bits per sample
    uint16_t samples_count;                 // Number of samples in packet
    uint8_t codec;                          // 0=PCM, 1=OPUS
    uint8_t compression_level;              // OPUS compression level (0-10)
    // Audio payload follows immediately
} howdytts_audio_header_t;
```

### Audio Streaming Flow

#### 1. Audio Stream Start

**Packet Type:** `HOWDYTTS_AUDIO_START` (0x11)

ESP32-P4 signals start of audio streaming session.

#### 2. Audio Data Packets

**Packet Type:** `HOWDYTTS_AUDIO_DATA` (0x10)

Continuous stream of 20ms audio frames:

```c
// Example 20ms frame at 16kHz = 320 samples
howdytts_audio_header_t header = {
    .magic = 0x41554449,
    .version = 1,
    .packet_type = HOWDYTTS_AUDIO_DATA,
    .payload_length = 640,  // 320 samples * 2 bytes
    .sequence_number = 12345,
    .timestamp_ms = 987654321,
    .sample_rate = 16000,
    .channels = 1,
    .bits_per_sample = 16,
    .samples_count = 320,
    .codec = 0,  // PCM
    .compression_level = 0
};
// Followed by 640 bytes of PCM audio data
```

#### 3. OPUS Compressed Audio

For bandwidth optimization, OPUS compression can be enabled:

```c
howdytts_audio_header_t header = {
    .magic = 0x41554449,
    .version = 1,
    .packet_type = HOWDYTTS_AUDIO_DATA,
    .payload_length = 160,  // Compressed size
    .sequence_number = 12346,
    .timestamp_ms = 987654341,
    .sample_rate = 16000,
    .channels = 1,
    .bits_per_sample = 16,
    .samples_count = 320,
    .codec = 1,  // OPUS
    .compression_level = 5
};
// Followed by 160 bytes of OPUS compressed audio
```

## 3. HTTP State Server Protocol (Port 8080+)

### Endpoints

The ESP32-P4 runs an HTTP server with the following endpoints:

#### POST /state - Receive State Changes

HowdyTTS server sends conversation state updates to the device.

**Request Body:**
```json
{
    "state": "listening",
    "text": "What can I help you with?",
    "intent": "general_inquiry",
    "confidence": 0.95,
    "session_id": 123456,
    "timestamp_ms": 1640995200000,
    "expected_duration_ms": 5000,
    "audio_required": false
}
```

**Response:**
```json
{
    "status": "ok",
    "device_state": "ready",
    "timestamp": 1640995200001
}
```

#### POST /speak - Receive TTS Audio

HowdyTTS server sends TTS audio for playback.

**Request Headers:**
```
Content-Type: multipart/form-data; boundary=----WebKitFormBoundary
```

**Request Body:**
```
------WebKitFormBoundary
Content-Disposition: form-data; name="text"

Hello! How can I help you today?
------WebKitFormBoundary
Content-Disposition: form-data; name="audio"; filename="response.opus"
Content-Type: audio/opus

[OPUS encoded audio data]
------WebKitFormBoundary--
```

**Response:**
```json
{
    "status": "ok",
    "message": "TTS audio received",
    "playback_started": true
}
```

#### GET /status - Device Status

Returns current device status for monitoring.

**Response:**
```json
{
    "device_id": "esp32p4-a1b2c3",
    "status": "connected",
    "state": "listening",
    "audio_level": 0.25,
    "wifi_rssi": -45,
    "uptime_seconds": 86400,
    "memory_free_kb": 234,
    "audio_latency_ms": 45,
    "firmware_version": "1.0.0",
    "server_connected": "HowdyTTS-MacStudio",
    "audio_stats": {
        "packets_sent": 12345,
        "packets_lost": 23,
        "average_latency_ms": 45.2,
        "compression_ratio": 2.8
    }
}
```

#### GET /health - Health Check

Simple health check endpoint.

**Response:**
```json
{
    "status": "healthy",
    "timestamp": 1640995200000
}
```

## State Synchronization

### Conversation States

The system maintains synchronized states between HowdyTTS server and ESP32-P4:

```c
typedef enum {
    HOWDYTTS_STATE_IDLE = 0,                // Device idle, waiting for wake word
    HOWDYTTS_STATE_WAKE_WORD_DETECTED,      // Wake word detected, starting session
    HOWDYTTS_STATE_LISTENING,               // Actively listening for speech
    HOWDYTTS_STATE_SPEECH_DETECTED,         // Speech detected, processing
    HOWDYTTS_STATE_THINKING,                // Server processing request
    HOWDYTTS_STATE_RESPONDING,              // Server generating response
    HOWDYTTS_STATE_SPEAKING,                // Playing TTS response
    HOWDYTTS_STATE_SESSION_ENDING,          // Ending conversation session
    HOWDYTTS_STATE_ERROR                    // Error state
} howdytts_conversation_state_t;
```

### State Transitions

```
idle → wake_word_detected → listening → speech_detected → 
thinking → responding → speaking → session_ending → idle
```

## Error Handling and Recovery

### Network Error Types

1. **Discovery Timeout** - No HowdyTTS servers found
2. **Registration Failure** - Server rejected device registration
3. **Audio Stream Errors** - UDP packet loss, network congestion
4. **HTTP Connection Errors** - State server unreachable
5. **Protocol Errors** - Invalid packet format, version mismatch

### Recovery Strategies

1. **Exponential Backoff** - Increasing retry intervals
2. **Automatic Reconnection** - Transparent reconnection attempts
3. **Graceful Degradation** - Continue operation with reduced functionality
4. **Error Reporting** - Detailed error information to application layer

### Implementation Example

```c
esp_err_t howdytts_handle_network_error(const char* error_type) {
    if (strcmp(error_type, "discovery_timeout") == 0) {
        // Retry discovery with longer timeout
        return howdytts_discovery_start(20000);  // 20 seconds
    }
    
    if (strcmp(error_type, "audio_stream_error") == 0) {
        // Restart audio streaming
        howdytts_audio_streaming_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second delay
        return howdytts_audio_streaming_start();
    }
    
    if (strcmp(error_type, "registration_failed") == 0) {
        // Try different server or retry with modified config
        return howdytts_discovery_start(10000);
    }
    
    return ESP_FAIL;
}
```

## Performance Optimizations

### ESP32-P4 Specific Optimizations

1. **Dual-Core Utilization**
   - Audio processing on one core
   - Network communication on another core

2. **Memory Management**
   - Pre-allocated audio buffers
   - Circular buffer for streaming
   - PSRAM utilization for large buffers

3. **Real-Time Performance**
   - High-priority FreeRTOS tasks for audio
   - Non-blocking socket operations
   - Hardware-accelerated OPUS encoding

4. **Power Efficiency**
   - Dynamic frequency scaling
   - WiFi power save mode during idle
   - Selective peripheral shutdown

### Network Optimizations

1. **OPUS Compression**
   - Adaptive compression based on audio content
   - Typical 2.5x-4x compression ratio
   - Sub-50ms encoding latency

2. **Adaptive Quality**
   - Monitor network conditions
   - Adjust compression level dynamically
   - Packet loss detection and mitigation

3. **Buffer Management**
   - Optimal buffer sizes for 20ms frames
   - Jitter buffer for network variations
   - Underrun/overrun protection

## Integration Example

```c
// Initialize HowdyTTS integration
howdytts_integration_config_t config = {
    .device_id = "esp32p4-howdyscreen",
    .device_name = "Office HowdyScreen",
    .room = "office",
    .http_state_port = 8080,
    .opus_compression_level = 5,
    .audio_buffer_frames = 10,
    .enable_audio_stats = true,
    .max_packet_loss_percent = 5
};

howdytts_integration_callbacks_t callbacks = {
    .on_server_discovered = handle_server_discovered,
    .on_state_change = handle_state_change,
    .on_speak_request = handle_tts_audio,
    .on_connection_lost = handle_connection_lost
};

// Start integration
howdytts_integration_init(&config, &callbacks);
howdytts_discovery_start(10000);

// Stream audio in callback
void audio_input_callback(const int16_t* samples, size_t count) {
    howdytts_send_audio_frame(samples, count, true);  // Enable OPUS
}
```

This protocol implementation provides a robust, efficient, and scalable solution for ESP32-P4 integration with the HowdyTTS ecosystem while maintaining backward compatibility and room for future enhancements.

## Troubleshooting

### Common Issues

1. **Discovery Timeout**
   ```bash
   # Check UDP broadcast capability
   netstat -i  # Verify network interface
   # Increase discovery timeout
   howdytts_discovery_start(30000);  // 30 seconds
   ```

2. **Audio Latency**
   ```c
   // Reduce buffer size for lower latency
   config.audio_buffer_frames = 5;  // 100ms buffer
   
   // Enable real-time priority
   vTaskPrioritySet(audio_task_handle, configMAX_PRIORITIES - 1);
   ```

3. **Memory Issues**
   ```c
   // Monitor heap usage
   ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
   
   // Use static buffers for audio
   static uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
   ```

4. **Network Connectivity**
   ```c
   // Test server reachability
   uint32_t latency;
   esp_err_t ret = howdytts_test_server_connectivity(server_ip, &latency);
   ```