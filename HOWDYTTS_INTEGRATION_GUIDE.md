# HowdyTTS ESP32P4 Integration Guide

This guide explains how to integrate your HowdyTTS server with the ESP32P4 HowdyScreen device.

## Overview

The ESP32P4 acts as a wireless audio interface that:
- Captures voice input via microphone
- Streams audio to HowdyTTS server
- Receives TTS audio responses
- Provides visual feedback on 800x800 display and LED ring

## Network Protocol

### Communication Details
- **Protocol**: UDP (not WebSocket)
- **Audio Port**: 8002 (changed from 8000 to avoid FastwhisperAPI conflict)
- **Control Port**: 8001 (if needed for future control messages)
- **Audio Format**: 16-bit PCM, 16kHz, Mono
- **Frame Size**: 320 samples (20ms)

### Packet Structure
```c
typedef struct {
    uint32_t timestamp;
    uint16_t sequence;
    uint16_t data_size;
    uint8_t data[];  // PCM audio data
} audio_packet_t;
```

## HowdyTTS Server Requirements

### 1. UDP Audio Server Setup

Create a UDP server that listens on port 8002:

```python
import socket
import struct
import numpy as np
from threading import Thread
import queue

class ESP32AudioServer:
    def __init__(self, host='0.0.0.0', port=8002):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((self.host, self.port))
        self.client_address = None
        self.audio_queue = queue.Queue()
        
    def receive_audio(self):
        """Receive audio packets from ESP32P4"""
        while True:
            data, addr = self.sock.recvfrom(256)  # MAX_PACKET_SIZE
            
            # Save client address for response
            self.client_address = addr
            
            # Parse packet header
            if len(data) >= 8:
                timestamp, sequence, data_size = struct.unpack('<IHH', data[:8])
                audio_data = data[8:8+data_size]
                
                # Convert to numpy array
                audio_samples = np.frombuffer(audio_data, dtype=np.int16)
                self.audio_queue.put(audio_samples)
                
    def send_audio(self, audio_samples):
        """Send TTS audio back to ESP32P4"""
        if self.client_address is None:
            return
            
        # Convert to int16 if needed
        if audio_samples.dtype != np.int16:
            audio_samples = (audio_samples * 32767).astype(np.int16)
        
        # Send in chunks of 320 samples (20ms)
        chunk_size = 320
        sequence = 0
        
        for i in range(0, len(audio_samples), chunk_size):
            chunk = audio_samples[i:i+chunk_size]
            
            # Create packet
            timestamp = int(time.time() * 1000)  # milliseconds
            header = struct.pack('<IHH', timestamp, sequence, len(chunk) * 2)
            packet = header + chunk.tobytes()
            
            # Send to ESP32P4
            self.sock.sendto(packet, self.client_address)
            sequence += 1
            
            # Small delay to avoid overwhelming ESP32
            time.sleep(0.018)  # Slightly less than 20ms
```

### 2. Integration with HowdyTTS Pipeline

Integrate the UDP server with your existing HowdyTTS workflow:

```python
import whisper
from your_tts_module import TextToSpeech

class HowdyTTSServer:
    def __init__(self):
        self.audio_server = ESP32AudioServer()
        self.whisper_model = whisper.load_model("base")
        self.tts = TextToSpeech()
        
        # Start audio receiver thread
        Thread(target=self.audio_server.receive_audio, daemon=True).start()
        
    def process_audio_stream(self):
        """Main processing loop"""
        audio_buffer = []
        silence_threshold = 500  # milliseconds
        last_audio_time = time.time()
        
        while True:
            try:
                # Get audio from queue (non-blocking)
                audio_chunk = self.audio_server.audio_queue.get(timeout=0.02)
                audio_buffer.extend(audio_chunk)
                last_audio_time = time.time()
                
            except queue.Empty:
                # Check if we have enough silence after speech
                if audio_buffer and (time.time() - last_audio_time) > silence_threshold/1000:
                    # Process accumulated audio
                    audio_array = np.array(audio_buffer, dtype=np.float32) / 32768.0
                    
                    # 1. Speech to text
                    result = self.whisper_model.transcribe(audio_array, fp16=False)
                    text = result["text"]
                    
                    print(f"Transcribed: {text}")
                    
                    # 2. Process with your HowdyTTS logic
                    response_text = self.process_query(text)
                    
                    # 3. Text to speech
                    audio_response = self.tts.synthesize(response_text, sample_rate=16000)
                    
                    # 4. Send response back to ESP32P4
                    self.audio_server.send_audio(audio_response)
                    
                    # Clear buffer for next query
                    audio_buffer = []
```

### 3. Network Configuration

#### Automatic Server Discovery (Recommended)

The ESP32P4 now supports automatic server discovery via mDNS/Bonjour. Simply advertise your HowdyTTS server:

```python
# Install zeroconf library
pip install zeroconf

# Use the provided mDNS advertiser
from howdytts_mdns_server import HowdyTTSMDNSAdvertiser

mdns = HowdyTTSMDNSAdvertiser(port=8002)
mdns.start()
# Your server code here
mdns.stop()
```

#### Fallback Server IPs

The ESP32P4 will automatically try these IPs if mDNS discovery fails:
1. `192.168.1.100:8002` (Home network)
2. `192.168.0.100:8002` (Alternative home)
3. `10.0.0.100:8002` (Some routers)
4. `172.16.0.100:8002` (Corporate networks)

To modify fallback IPs, edit `main.c`:
```c
static const char *FALLBACK_SERVERS[] = {
    "YOUR_IP_1",
    "YOUR_IP_2",
    // Add more as needed
};
```

#### WiFi Configuration

**New: Automatic WiFi Provisioning (Recommended)**

The ESP32P4 now supports easy WiFi setup without hardcoding credentials:

1. **First Boot**: ESP32P4 creates access point "HowdyScreen-Setup" (password: "configure")
2. **Connect**: Your Mac connects to this AP
3. **Configure**: Visit `http://192.168.4.1` to select your WiFi network and enter password
4. **Save**: Configuration is stored permanently in ESP32P4 flash

See `WIFI_PROVISIONING_GUIDE.md` for detailed setup instructions.

**Legacy Method**: You can still hardcode WiFi in `main.c` if needed:
```c
#define WIFI_SSID     "YourNetworkName"
#define WIFI_PASSWORD "YourPassword"
```

The ESP32P4 will:
1. Connect to configured WiFi (via provisioning or hardcoded)
2. Search for HowdyTTS via mDNS
3. Try each fallback IP if mDNS fails
4. Automatically retry failed servers
5. Display connection status on screen

### 4. Visual Feedback Coordination

The ESP32P4 automatically shows the Howdy animation when:
1. Voice activity is detected
2. 500ms of silence passes (end of speech)
3. Animation stays visible until TTS audio is received

No server-side coordination needed - just send audio response when ready.

### 5. Testing the Integration

Simple test server:
```python
# test_server.py
import socket
import time
import numpy as np

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 8002))

print("Listening for ESP32P4 audio on port 8002...")

while True:
    data, addr = sock.recvfrom(256)
    print(f"Received {len(data)} bytes from {addr}")
    
    # Echo back a simple beep as response
    frequency = 440  # Hz
    duration = 0.5   # seconds
    sample_rate = 16000
    
    t = np.linspace(0, duration, int(sample_rate * duration))
    beep = (np.sin(2 * np.pi * frequency * t) * 32767).astype(np.int16)
    
    # Send beep in chunks
    for i in range(0, len(beep), 320):
        chunk = beep[i:i+320]
        sock.sendto(chunk.tobytes(), addr)
        time.sleep(0.02)
```

### 6. Important Notes

1. **Audio Format**: ESP32P4 expects 16kHz, 16-bit, mono PCM audio
2. **Packet Size**: Keep UDP packets under 256 bytes (including header)
3. **Latency**: ESP32P4 shows animation after 500ms silence, so process quickly
4. **Network**: Ensure server firewall allows UDP port 8002
5. **Buffering**: ESP32 has limited buffer space, send audio in real-time
6. **WiFi Setup**: Use WiFi provisioning for easy network configuration
7. **mDNS**: Enable mDNS advertising for automatic server discovery

## Troubleshooting

### Network Issues
- **ESP32P4 won't connect**: Use WiFi provisioning mode (see `WIFI_PROVISIONING_GUIDE.md`)
- **No audio received**: Check firewall, ensure both devices on same network
- **Server not found**: Verify mDNS is working or check fallback IP list

### Audio Issues
- **Choppy audio**: Reduce chunk size or add small delays between packets
- **Animation not showing**: Verify voice detection threshold in ESP32 code
- **No response audio**: Ensure audio format is 16kHz, 16-bit PCM

### Server Discovery
- **mDNS not working**: Install `zeroconf` library and use `HowdyTTSMDNSAdvertiser`
- **Wrong IP address**: Update fallback server list in ESP32P4 `main.c`
- **Multiple networks**: ESP32P4 will try all configured servers automatically

## Quick Start Checklist

1. ✅ Flash ESP32P4 with updated firmware
2. ✅ Connect to "HowdyScreen-Setup" AP and configure WiFi
3. ✅ Install `pip install zeroconf` on Mac Studio
4. ✅ Run HowdyTTS server with mDNS advertising
5. ✅ Verify ESP32P4 shows "Connected" status
6. ✅ Test voice input → ESP32P4 should show Howdy animation
7. ✅ Confirm TTS audio plays back through ESP32P4 speaker

## Example Full Integration

See the updated `howdytts_esp32_server.py` for a complete implementation with mDNS support that integrates with your existing HowdyTTS pipeline.