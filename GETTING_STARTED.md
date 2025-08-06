# ESP32-P4 HowdyScreen Getting Started Guide

## 🚀 Welcome to HowdyScreen!

This guide will help you set up your ESP32-P4 HowdyScreen device as a wireless voice assistant that integrates seamlessly with HowdyTTS.

## 📋 What You'll Need

### Hardware Requirements
- **ESP32-P4-WIFI6-Touch-LCD-XC Board** (Waveshare or compatible)
- **USB-C Cable** for programming and power
- **WiFi Network** (2.4GHz or 5GHz supported)
- **Computer** running macOS, Linux, or Windows

### Software Requirements
- **ESP-IDF v5.3+** (Espressif IoT Development Framework)
- **Python 3.8+** with conda (for HowdyTTS)
- **Git** for cloning repositories

## 🛠️ Step 1: Hardware Setup

### ESP32-P4 Board Overview
```
┌─────────────────────────────────────┐
│    ESP32-P4-WIFI6-Touch-LCD-XC      │
├─────────────────────────────────────┤
│                                     │
│  ┌─────────┐  ┌───────────────────┐ │
│  │         │  │                   │ │
│  │ 800x800 │  │  ESP32-P4 MCU     │ │
│  │ Round   │  │  - Dual RISC-V    │ │  
│  │ LCD     │  │  - 768KB SRAM     │ │
│  │ Display │  │  - 32MB PSRAM     │ │
│  │         │  │  - WiFi6 + BT     │ │
│  └─────────┘  └───────────────────┘ │
│                                     │
│  ┌─────────────────────────────────┐ │
│  │     Touch Controller (CST9217)  │ │
│  └─────────────────────────────────┘ │
│                                     │
│  ┌─────────────────────────────────┐ │
│  │   Audio Codec (ES8311/ES7210)   │ │
│  │   - 16kHz Audio Capture         │ │
│  │   - Speaker Output              │ │
│  └─────────────────────────────────┘ │
└─────────────────────────────────────┘
```

### Connection Steps
1. **Connect USB-C cable** to your computer
2. **Verify device detection**:
   ```bash
   # macOS/Linux
   ls /dev/cu.usbserial-* 
   ls /dev/ttyUSB*
   
   # Windows  
   # Check Device Manager for COM ports
   ```
3. **Note the port name** (e.g., `/dev/cu.usbserial-AB123456`)

## 🔧 Step 2: Development Environment Setup

### Install ESP-IDF
```bash
# Clone ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.3

# Install ESP-IDF
./install.sh esp32p4

# Set up environment (add to ~/.bashrc or ~/.zshrc)
source $HOME/esp-idf/export.sh
```

### Verify Installation
```bash
# Check ESP-IDF version
idf.py --version

# Should show: ESP-IDF v5.3 or later
```

### Set Target Chip
```bash
export IDF_TARGET=esp32p4
echo $IDF_TARGET
```

## 📥 Step 3: Get the HowdyScreen Firmware

### Clone Repository
```bash
# Clone the HowdyScreen project
cd ~/Desktop/Coding
git clone [repository_url] ESP32P4/HowdyScreen
cd ESP32P4/HowdyScreen
```

### Project Structure Overview
```
HowdyScreen/
├── main/                           # Main application
│   └── howdy_phase6_howdytts_integration.c
├── components/                     # Custom components
│   ├── audio_processor/            # Audio processing
│   │   ├── src/enhanced_vad.c     # Voice Activity Detection
│   │   ├── src/esp32_p4_wake_word.c # Wake word detection
│   │   └── src/enhanced_udp_audio.c # UDP audio streaming
│   ├── ui_manager/                 # Round display UI
│   ├── websocket_client/           # WebSocket feedback client
│   │   └── src/esp32_p4_vad_feedback.c
│   └── service_discovery/          # mDNS integration
├── build/                          # Build output (generated)
├── sdkconfig                       # ESP-IDF configuration
└── CMakeLists.txt                  # Build configuration
```

## ⚙️ Step 4: Configuration

### WiFi and Server Configuration
Edit the main configuration file:
```bash
nano main/howdy_phase6_howdytts_integration.c
```

**Update these settings:**
```c
// WiFi Configuration
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourWiFiPassword"

// HowdyTTS Server Configuration  
#define SERVER_IP "192.168.1.100"  // Your HowdyTTS server IP
#define SERVER_PORT 8000           // UDP audio port
#define WEBSOCKET_PORT 8001        // WebSocket feedback port

// Device Configuration
#define DEVICE_NAME "HowdyScreen_01"
#define ROOM_NAME "Living Room"    // Optional room assignment
```

### ESP-IDF Menu Configuration
```bash
idf.py menuconfig
```

**Key settings to verify:**
- **Component config → ESP32-P4-Specific Config**
  - Enable PSRAM: ✅
  - PSRAM clock: 200MHz
- **Component config → WiFi**
  - WiFi6 support: ✅ 
  - Power save mode: None (for audio streaming)
- **Component config → Bluetooth** 
  - Disable if not needed (saves memory)

## 🔨 Step 5: Build and Flash Firmware

### Clean and Build
```bash
# Clean previous build
idf.py clean

# Build the firmware
idf.py build
```

**Expected output:**
```
Project build complete. To flash:
 idf.py flash
HowdyScreen.bin binary size 0x165f70 bytes
```

### Flash to Device
```bash
# Flash and start monitoring
idf.py -p /dev/cu.usbserial-AB123456 flash monitor

# Or just flash
idf.py -p /dev/cu.usbserial-AB123456 flash
```

### First Boot Verification
After flashing, you should see:
```
I (1234) HOWDY: ================================
I (1235) HOWDY: ESP32-P4 HowdyScreen Starting
I (1236) HOWDY: Firmware Version: Phase6C-v1.0
I (1237) HOWDY: ================================
I (1500) HOWDY: WiFi connecting to YourWiFiNetwork...
I (3200) HOWDY: WiFi connected! IP: 192.168.1.101
I (3201) HOWDY: Enhanced VAD initialized
I (3202) HOWDY: Wake word detection active - "Hey Howdy"
I (3203) HOWDY: WebSocket feedback client connecting...
I (3300) HOWDY: WebSocket connected to 192.168.1.100:8001
I (3301) HOWDY: UDP audio streaming ready
I (3302) HOWDY: System initialization complete
```

## 🎭 Step 6: HowdyTTS Server Setup

### Install HowdyTTS
```bash
# Clone HowdyTTS repository
cd ~/Desktop/Coding/RBP
git clone [howdytts_repo_url] HowdyTTS
cd HowdyTTS

# Create conda environment
conda create -n howdy310 python=3.10
conda activate howdy310

# Install dependencies
pip install -r requirements.txt
pip install opuslib websocket-client  # For ESP32-P4 support
```

### Start HowdyTTS
```bash
# Method 1: Use the shell launcher (recommended)
python launch_howdy_shell.py

# Method 2: Direct launch with ESP32-P4 support
python run_voice_assistant.py --wireless --room "Living Room"
```

## 🧪 Step 7: Testing Your Setup

### Basic Functionality Test

1. **Check Device Connection**:
   ```bash
   python run_voice_assistant.py --list-devices
   ```
   
   **Expected output:**
   ```
   Available wireless devices:
     0: HowdyScreen_01 (Living Room) - 192.168.1.101
   ```

2. **Test Wake Word Detection**:
   - Say **"Hey Howdy"** near your ESP32-P4 device
   - Watch for LED activity on the round display
   - Check serial monitor for detection logs

3. **Test Voice Interaction**:
   - Say: **"Hey Howdy"**
   - Wait for activation sound
   - Ask: **"What's the weather like?"**
   - Verify TTS response plays through ESP32-P4 speakers

### Diagnostic Commands
```bash
# Monitor ESP32-P4 serial output
idf.py monitor

# View HowdyTTS debug logs
python run_voice_assistant.py --wireless --debug

# Check network connectivity
ping 192.168.1.101  # Your ESP32-P4 IP
```

## 🎨 Step 8: Display and UI Customization

### Round Display Features
The 800x800 circular display shows:
- **Audio Level Meter**: Real-time audio visualization
- **Connection Status**: WiFi strength, server connection
- **Wake Word Feedback**: Visual confirmation of detection
- **System Status**: Battery, temperature, errors

### Customizing the UI
Edit `components/ui_manager/src/ui_manager.c`:
```c
// Customize audio level colors
#define AUDIO_COLOR_LOW    LV_COLOR_GREEN
#define AUDIO_COLOR_MED    LV_COLOR_YELLOW  
#define AUDIO_COLOR_HIGH   LV_COLOR_RED

// Adjust wake word animation
#define WAKE_WORD_ANIMATION_DURATION 2000  // 2 seconds
```

## 🚀 Step 9: Advanced Configuration

### Multi-Room Setup
For multiple devices in different rooms:

1. **Configure each device** with unique names:
   ```c
   #define DEVICE_NAME "HowdyScreen_Kitchen"
   #define ROOM_NAME "Kitchen"
   ```

2. **Start HowdyTTS** with room targeting:
   ```bash
   # Target specific room
   python run_voice_assistant.py --wireless --room "Kitchen"
   
   # Auto-select best device
   python run_voice_assistant.py --auto
   ```

### Performance Tuning

**For Low Latency** (edit `components/audio_processor/src/audio_processor.c`):
```c
#define AUDIO_BUFFER_COUNT 2     // Fewer buffers
#define AUDIO_BUFFER_SIZE 512    // Smaller buffers
#define VAD_PROCESSING_MODE 1    // Optimized mode
```

**For High Accuracy** (edit main config):
```c
#define WAKE_WORD_CONFIDENCE_THRESHOLD 0.8f  // Higher threshold
#define VAD_CONSISTENCY_FRAMES 7             // More consistency checking  
```

## 🔧 Troubleshooting Common Issues

### WiFi Connection Problems
**Symptoms**: Device doesn't connect to WiFi
**Solutions**:
1. Double-check SSID and password
2. Ensure 2.4GHz band is enabled on router
3. Move device closer to router during setup
4. Check serial output for specific error codes

### No Audio Streaming
**Symptoms**: HowdyTTS doesn't detect wireless device
**Solutions**:
1. Verify firewall allows UDP port 8000
2. Check ESP32-P4 and server are on same network
3. Test with `--list-devices` command
4. Restart both ESP32-P4 and HowdyTTS

### Wake Word Not Working
**Symptoms**: "Hey Howdy" doesn't trigger response
**Solutions**:
1. Check microphone positioning (speak from 0.5-2 meters)
2. Lower confidence threshold in firmware
3. Verify Porcupine integration in HowdyTTS
4. Check WebSocket connection on port 8001

### Display Issues
**Symptoms**: Round display not showing content
**Solutions**:
1. Check MIPI-DSI connections
2. Verify LVGL buffer allocation in PSRAM
3. Reset display controller in code
4. Check touch controller I2C address

## 📚 Additional Resources

### Documentation
- [Complete VAD Integration Guide](VAD_INTEGRATION_GUIDE.md)
- [API Reference](API_REFERENCE.md)
- [Hardware Setup Guide](HARDWARE_SETUP.md)
- [Troubleshooting Guide](TROUBLESHOOTING_GUIDE.md)

### Development
- [Development Workflow](DEVELOPMENT_WORKFLOW.md)
- [Architecture Overview](ARCHITECTURE.md)
- [Integration Guide](INTEGRATION_GUIDE.md)

### Community
- **GitHub Issues**: Report bugs and request features
- **Documentation Updates**: Contribute improvements
- **Hardware Mods**: Share your customizations

## 🎉 Success!

Your ESP32-P4 HowdyScreen is now ready! You should have:

✅ **Wireless Voice Assistant**: "Hey Howdy" wake word activation  
✅ **Bidirectional VAD**: Edge + server processing for optimal accuracy  
✅ **Real-time Feedback**: WebSocket-based threshold adaptation  
✅ **Visual Interface**: Round display with audio level visualization  
✅ **Multi-device Support**: Room-based device coordination  

**Next steps:**
- Experiment with different rooms and wake word scenarios
- Try multi-device coordination with multiple ESP32-P4 devices
- Explore the advanced VAD fusion strategies for your environment
- Customize the circular UI for your specific needs

**Happy voice assisting!** 🤠

---

*This guide covers the Phase 6C implementation with complete bidirectional VAD and wake word detection. For technical support, check the troubleshooting guide or open an issue in the project repository.*