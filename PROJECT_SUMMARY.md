# HowdyScreen - ESP32P4 Audio Interface for HowdyTTS

## Project Overview

HowdyScreen is an ESP32P4-based hardware interface that acts as a bidirectional audio conduit for the HowdyTTS (text-to-speech) system. The device features a circular 800x800 display, ES8311 audio codec for high-quality audio I/O, and a 109-LED WS2812B ring for visual feedback.

## Hardware Specifications

- **MCU**: ESP32P4 (dual-core RISC-V)
- **Display**: 800x800 circular LCD with LVGL graphics
- **Audio**: ES8311 codec (16kHz, 16-bit stereo PCM)
  - Bidirectional I/O (stereo microphone input + stereo speaker output)
  - I2S interface for low-latency audio streaming
- **LED Ring**: 109 WS2812B addressable LEDs for audio visualization
- **Connectivity**: WiFi 802.11 b/g/n

## System Architecture

```
┌─────────────────┐    UDP Audio Stream    ┌──────────────────┐
│   Mac Studio    │◄────────────────────────►│   ESP32P4        │
│   (HowdyTTS)    │      Port 8002          │  (HowdyScreen)   │
│                 │                         │                  │
│ • FastwhisperAPI│                         │ • Audio Input    │
│ • TTS Engine    │                         │ • Audio Output   │
│ • Audio Proc.   │                         │ • LED Viz        │
└─────────────────┘                         │ • Display UI     │
                                            └──────────────────┘
```

## Key Features

### Audio Processing
- **Real-time audio capture** via ES8311 microphone input
- **Voice activity detection** with silence threshold
- **Audio analysis** for visualization (RMS, frequency bins)
- **Playback capability** for TTS responses
- **Low-latency streaming** over UDP

### Visual Interface
- **Audio level meters** showing real-time input levels
- **Processing animation** during TTS generation latency
- **Network status display** with WiFi connection info
- **Mute/unmute control** via center button
- **LED ring visualization** synchronized with audio

### Network Integration
- **Automatic server discovery** via mDNS/Bonjour
- **Fallback IP addressing** for different network configurations
- **WiFi provisioning** with captive portal setup
- **UDP audio streaming** protocol compatible with HowdyTTS

## Current Implementation Status

### ✅ Completed (Simplified Build)
- Core audio pipeline with ES8311 codec
- LVGL display manager with UI components
- LED controller for WS2812B ring
- FreeRTOS multi-core task architecture
- Build system configuration
- Port conflict resolution (8002 vs 8000)

### 🚧 Available but Disabled
- WiFi provisioning system
- mDNS server discovery
- Network audio streaming
- Howdy GIF animation support

### 📋 Integration Requirements
- HowdyTTS server modifications for ESP32P4 support
- UDP audio protocol implementation
- Network discovery service advertising

## File Structure

```
HowdyScreen/
├── main/
│   ├── main.c                  # Main application & task coordination
│   ├── audio_pipeline.{c,h}    # ES8311 codec & audio processing
│   ├── display_manager.{c,h}   # LVGL UI and display control
│   ├── led_controller.{c,h}    # WS2812B LED ring management
│   ├── howdy_config.h          # Hardware pin definitions
│   └── assets/                 # Resources (GIF, fonts, etc.)
├── components/lvgl/            # LVGL graphics library
├── HOWDYTTS_INTEGRATION_GUIDE.md
├── howdytts_esp32_server.py    # Server-side integration script
└── PROJECT_SUMMARY.md          # This file
```

## Technical Specifications

- **Audio Format**: 16kHz, 16-bit, Stereo PCM
- **Network Protocol**: UDP on port 8002
- **Display Resolution**: 800x800 pixels
- **LED Count**: 109 WS2812B addressable LEDs
- **Build System**: ESP-IDF v5.4
- **Graphics**: LVGL v8.3.0

## Usage Workflow

1. **Power On**: ESP32P4 initializes all subsystems
2. **Network Setup**: Connect to WiFi (manual config in simplified build)
3. **Server Discovery**: Locate HowdyTTS server on network
4. **Audio Ready**: Display shows "HowdyScreen Ready"
5. **Voice Input**: User speaks → audio captured → sent to server
6. **Processing**: Display shows Howdy animation during TTS generation
7. **Response**: Server sends audio response → played through speaker
8. **Visual Feedback**: LED ring provides real-time audio visualization

## Build Instructions

```bash
# Flash the firmware
idf.py flash

# Monitor serial output
idf.py monitor
```

## Integration Notes

- Designed to replace or augment existing HowdyTTS audio interfaces
- Compatible with FastwhisperAPI (uses different port)
- Provides both local and remote audio processing capabilities
- Scalable architecture for future feature expansion