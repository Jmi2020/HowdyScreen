| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

# HowdyScreen - ESP32-P4 Wireless Microphone with Display

A wireless microphone system built on ESP32-P4 featuring real-time audio streaming, visual display, and RGB LED feedback. This device captures audio through an integrated microphone, displays audio levels and system status on an 800x800 circular display, and provides visual feedback through a 109-LED ring.

## Important: WiFi Requirements
- **This device only supports 2.4GHz WiFi networks**
- The ESP32-C6 co-processor provides WiFi 6 capabilities on 2.4GHz band
- Ensure your router broadcasts on 2.4GHz before attempting connection
- See [ESP32P4_WIFI_CONFIG.md](ESP32P4_WIFI_CONFIG.md) for detailed WiFi configuration

## Features

- **Real-time Audio Processing**: 16kHz mono audio capture and streaming with 20ms frames
- **Wireless Communication**: UDP-based audio streaming over WiFi to HowdyTTS server
- **Visual Display**: 800x800 circular LVGL-based interface showing audio levels and system status
- **LED Ring Animation**: 109 WS2812B LEDs providing audio-reactive feedback
- **Multi-core Architecture**: Optimized task distribution across ESP32-P4 dual cores
- **Audio Codec Integration**: ES8311 codec for high-quality audio input/output

## Hardware Requirements

- ESP32-P4 development board
- ES8311 audio codec
- 800x800 circular display
- WS2812B LED ring (109 LEDs)
- Microphone and speaker setup
- Power amplifier (controlled via GPIO 53)

## Project Structure

```
├── CMakeLists.txt
├── main/
│   ├── main.c                  Main application entry point
│   ├── howdy_config.h          Hardware pin definitions and configuration
│   ├── audio_pipeline.c/h      Audio capture and playback management
│   ├── network_manager.c/h     WiFi and UDP communication
│   ├── display_manager.c/h     LVGL display and UI management
│   ├── led_controller.c/h      WS2812B LED ring control
│   └── led_strip_encoder.c/h   RMT-based LED driving
├── components/
│   └── lvgl/                   LVGL graphics library
└── README.md
```

## Configuration

Before building, update the following configuration in `main/main.c`:

```c
#define WIFI_SSID     "your_wifi_network"
#define WIFI_PASSWORD "your_wifi_password"
#define SERVER_IP     "192.168.1.100"  // HowdyTTS server IP
```

## Pin Configuration

The hardware pin assignments are defined in `main/howdy_config.h`:

- **Audio (ES8311)**: I2S pins 9-13, I2C control pins 7-8
- **Display**: Connected via SPI (pins defined in display driver)
- **LED Ring**: WS2812B data on GPIO 16
- **Power Amplifier**: Control on GPIO 53

## Building and Flashing

1. Install ESP-IDF development framework
2. Configure the project: `idf.py menuconfig`
3. Build the project: `idf.py build`
4. Flash to device: `idf.py -p PORT flash monitor`

## System Architecture

The application uses a multi-task architecture optimized for dual-core performance:

- **Core 0**: Audio processing task (highest priority)
- **Core 1**: Network, display, and LED tasks
- **Inter-task Communication**: Queues and mutexes for thread-safe data sharing

## Troubleshooting

* **Audio Issues**: Check ES8311 connections and I2S pin configuration
* **Display Problems**: Verify SPI connections and LVGL configuration
* **Network Connectivity**: Ensure WiFi credentials are correct and server is reachable
* **LED Ring**: Confirm WS2812B data pin and LED count configuration

For detailed debugging, monitor serial output: `idf.py monitor`
