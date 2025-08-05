# 🎉 HowdyTTS ESP32-P4 Voice Assistant Display

| Status | ✅ **PHASE 1 COMPLETE - HARDWARE OPERATIONAL** |
| ------ | -------------------------------------------- |
| Supported Targets | ESP32-P4 |
| Hardware Verified | ✅ ESP32-P4-WIFI6-Touch-LCD-3.4C (Waveshare) |

## 🚀 **SYSTEM OPERATIONAL** - August 5, 2025

**Major Milestone**: The ESP32-P4 HowdyScreen is now fully operational with working display, touch interaction, audio processing, and stable memory management!

## Project Overview

HowdyScreen is an ESP32-P4 based voice assistant display device that provides a touchscreen interface for interacting with HowdyTTS (Text-to-Speech) services. The device features a 3.4-inch round 800x800 IPS display, capacitive touch input, dual microphones with echo cancellation, and WiFi 6 connectivity via an integrated ESP32-C6 co-processor.

### Hardware Platform
- **Board**: Waveshare ESP32-P4-WIFI6-Touch-LCD-3.4C
- **Main Processor**: ESP32-P4 (Dual-core RISC-V @ 400MHz)
- **WiFi/Bluetooth**: ESP32-C6 co-processor (WiFi 6, Bluetooth 5 LE)
- **Display**: 3.4" Round IPS LCD (800×800, MIPI-DSI)
- **Touch**: CST9217 capacitive touch controller (10-point multi-touch)
- **Audio**: ES8311 low-power codec + ES7210 echo cancellation
- **Memory**: 32MB PSRAM, 32MB NOR Flash

## ✅ **VERIFIED WORKING Features** 

### 🖥️ **Display & Touch** - ✅ **OPERATIONAL**
- 800×800 round LCD with MIPI-DSI interface working
- GT911 capacitive touch controller responding
- Interactive mute button functional
- LVGL UI framework rendering smoothly

### 🎙️ **Audio Processing** - ✅ **OPERATIONAL**
- Real-time I2S audio capture active on dual cores
- 16kHz, 16-bit mono audio pipeline working
- Thread-safe cross-core audio processing
- **Ready for**: Voice activity detection, TTS playback

### 💾 **Memory Management** - ✅ **OPTIMIZED**
- 32MB SPIRAM detected and integrated (32.6MB free heap)
- Display framebuffer in SPIRAM (1.28MB)
- LVGL buffer in internal memory (40KB, DMA-capable)
- No memory crashes, stable operation

### 🌐 **Network & Connectivity** - 🔄 **PHASE 2 READY** 
- ESP32-C6 WiFi 6 co-processor hardware ready
- UDP audio streaming code implemented
- WebSocket control channel code ready
- HTTP state synchronization endpoints coded
- mDNS service discovery implemented
- **Next**: WiFi provisioning and server connectivity testing

### 🔄 **Conversation States**
- **IDLE**: Ready state with microphone icon
- **LISTENING**: Active audio capture with level meter
- **PROCESSING**: Server communication with loading animation
- **SPEAKING**: TTS playback with visual feedback
- **ERROR**: Error handling with recovery options

## Project Structure

```
HowdyScreen/
├── main/                           # Main application
│   ├── howdy_integrated.c          # Core application logic
│   ├── idf_component.yml           # Component dependencies
│   └── Kconfig.projbuild           # Project configuration
├── components/                     # Custom components
│   ├── audio_processor/            # Audio capture & processing
│   ├── ui_manager/                 # LVGL UI management
│   └── esp_lcd_touch_cst9217/      # Touch controller driver
├── managed_components/             # ESP-IDF managed components
│   ├── espressif__esp_wifi_remote/ # ESP32-C6 WiFi interface
│   ├── espressif__esp_lvgl_port/   # LVGL integration
│   ├── lvgl__lvgl/                 # LVGL graphics library
│   └── waveshare__esp_lcd_touch_cst9217/ # CST9217 touch driver
├── backup/                         # Previous implementations
└── Research/                       # Documentation & specs
```

## Development Status

### ✅ **Completed**
- [x] ESP32-P4 build system and component integration
- [x] LVGL UI framework with HowdyTTS theme
- [x] Touch controller (CST9217) integration
- [x] Conversation state machine implementation
- [x] Project structure and dependencies

### 🔄 **In Progress**
- [ ] Audio pipeline integration (ES8311 codec)
- [ ] ESP32-C6 WiFi remote configuration
- [ ] Display hardware initialization
- [ ] Touch input handling

### 📋 **Planned**
- [ ] WebSocket client for HowdyTTS communication
- [ ] Opus audio encoding implementation
- [ ] mDNS server discovery
- [ ] Error handling and recovery
- [ ] Power management and optimization
- [ ] End-to-end testing with HowdyTTS server

## Quick Start

### Prerequisites
- ESP-IDF v5.3+ with ESP32-P4 support
- VS Code with ESP-IDF extension (recommended)
- HowdyTTS server running on local network

### Build and Flash
```bash
# Set target to ESP32-P4
idf.py set-target esp32p4

# Build the project
idf.py build

# Flash to device
idf.py flash monitor
```

### Configuration

Use `idf.py menuconfig` to configure the following settings:

**WiFi Settings** (HowdyScreen Configuration menu):
- `HOWDY_WIFI_SSID`: Your WiFi network name (2.4GHz only)
- `HOWDY_WIFI_PASSWORD`: Your WiFi password
- `HOWDY_SERVER_IP`: HowdyTTS server IP address (default: 192.168.1.100)

**Additional Settings**:
- `HOWDY_AUDIO_TASK_PRIORITY`: Audio task priority (default: 23)
- `HOWDY_USE_ESP_WIFI_REMOTE`: Enable ESP32-C6 WiFi co-processor (default: enabled)
- `MDNS_ENABLED`: Enable mDNS service discovery (default: enabled)
- `MDNS_HOSTNAME`: Device hostname for mDNS (default: howdyscreen)

**Note about menuconfig warnings**: You may see informational warnings about duplicate WiFi symbols when running menuconfig. These are expected when using the ESP32-C6 WiFi co-processor and can be safely ignored. The esp_wifi_remote component needs to override some default WiFi settings to route through the co-processor.
- Audio settings
- Display configuration

## Hardware Connections

### ESP32-P4 to ESP32-C6 (SDIO)
| ESP32-P4 GPIO | ESP32-C6 Function | Description |
|---------------|-------------------|-------------|
| GPIO36        | SDIO_CLK         | Clock line  |
| GPIO37        | SDIO_CMD         | Command line |
| GPIO35        | SDIO_D0          | Data line 0 |
| GPIO34        | SDIO_D1          | Data line 1 |
| GPIO33        | SDIO_D2          | Data line 2 |
| GPIO48        | SDIO_D3          | Data line 3 |
| GPIO47        | RESET            | Reset line  |

### Audio Interface (I2S)
| ESP32-P4 GPIO | Function    | Description |
|---------------|-------------|-------------|
| GPIO13        | I2S_MCLK    | Master clock |
| GPIO12        | I2S_BCLK    | Bit clock |
| GPIO10        | I2S_WS      | Word select |
| GPIO11        | I2S_DO      | Data out (speaker) |
| GPIO9         | I2S_DI      | Data in (microphone) |

## Communication Protocol

### HowdyTTS Integration
- **Protocol**: WebSocket with JSON messaging
- **Audio Format**: 16kHz, 16-bit PCM mono
- **Encoding**: Opus (adaptive bitrate 6-64 kbps)
- **Latency Target**: <100ms end-to-end
- **Frame Size**: 20ms chunks (320 samples)

### Message Format
```json
{
  "event": "audio_stream",
  "streamId": "esp32_001",
  "media": {
    "track": "inbound",
    "timestamp": 1640995200000,
    "payload": "base64_encoded_opus_audio"
  }
}
```

## Contributing

### Development Workflow
1. Create feature branch from `main`
2. Implement changes with proper component separation
3. Test on actual ESP32-P4 hardware
4. Update documentation and commit messages
5. Create pull request with detailed description

### Code Style
- Follow ESP-IDF coding conventions
- Use meaningful variable and function names
- Add comprehensive logging with appropriate levels
- Document all public APIs
- Maintain component separation

## Architecture Notes

### Dual-Chip Design
The ESP32-P4 lacks built-in WiFi, so this project uses the ESP32-C6 co-processor for wireless connectivity. Communication between chips occurs via SDIO interface, with the ESP32-P4 as host and ESP32-C6 as device.

### Task Priorities
- **Audio Processing**: Priority 24 (above WiFi)
- **Display Updates**: Priority 22 (below audio, above network)
- **Network Operations**: Priority 20 (standard priority)
- **Background Tasks**: Priority 5-10

### Memory Management
- **L2 Memory**: High-performance operations
- **PSRAM**: Display framebuffers and large data
- **DMA Memory**: Audio buffers and LCD transfers

## Troubleshooting

### Common Issues
1. **Build Failures**: Ensure ESP-IDF v5.3+ and ESP32-P4 target
2. **Component Conflicts**: Check `dependencies.lock` for version mismatches
3. **Display Issues**: Verify MIPI-DSI connections and power supply
4. **Audio Problems**: Check I2S wiring and codec initialization
5. **WiFi Connectivity**: Confirm ESP32-C6 SDIO interface setup

### Debug Tools
- `idf.py monitor` - Serial output monitoring
- `idf.py gdb` - Interactive debugging
- LVGL debugging - Visual UI element inspection
- Audio pipeline logging - Real-time audio metrics

## License

This project is licensed under the terms specified in the ESP-IDF framework. See individual component licenses for third-party dependencies.

## Acknowledgments

- **Espressif Systems** - ESP-IDF framework and ESP32-P4 platform
- **Waveshare Electronics** - Hardware platform and BSP components
- **LVGL Team** - Graphics library and UI framework  
- **HowdyTTS Project** - Voice assistant integration target

---

**Board**: ESP32-P4-WIFI6-Touch-LCD-3.4C | **Framework**: ESP-IDF v5.5 | **Target**: Voice Assistant Display