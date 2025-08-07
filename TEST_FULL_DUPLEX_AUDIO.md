# Full-Duplex Audio Testing Guide

## Phase 3B Implementation Status

### ✅ Completed Components

1. **WebSocket TTS Audio Reception**
   - Base64 decoding implemented with mbedtls
   - TTS session management with start/chunk/end handling
   - Audio chunk queuing to TTS playback system

2. **TTS Audio Playbook Integration**
   - TTS audio handler connected to Dual I2S Manager
   - Direct I2S speaker output via `dual_i2s_write_speaker()`
   - Fallback to audio processor if dual I2S fails

3. **Full-Duplex I2S Configuration**
   - Simultaneous mode activation during TTS playback
   - Microphone-only mode during wake word detection
   - ESP32-P4 pin mapping: SCLK(12), LCLK(10), DOUT(9), DSIN(11)

4. **Audio Pipeline Integration**
   - WebSocket TTS → TTS Audio Handler → Dual I2S Speaker
   - Microphone → Dual I2S → Audio Processor → HowdyTTS

### 🔧 Testing Protocol

#### 1. Build and Flash Test
```bash
cd /Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen
idf.py build flash monitor
```

#### 2. Expected Boot Sequence
```
🎵 Initializing Dual I2S Manager for full-duplex operation
✅ Dual I2S Manager initialized
🎤 Microphone: ES7210 with echo cancellation
🔊 Speaker: ES8311 for TTS playback
⚡ Configuration: 16kHz, 16-bit, mono, 20ms buffers
🎤 Started in microphone mode - ready for wake word detection

🔊 Initializing TTS Audio Handler
✅ TTS Audio Handler initialized
🔊 Audio Format: 16000Hz, 1ch, 16-bit, 80% volume
🔊 TTS will use Dual I2S Manager for speaker output
```

#### 3. TTS Audio Test Sequence
1. Connect to HowdyTTS server via WiFi discovery
2. Say "Hey Howdy" to trigger wake word detection
3. Speak a query
4. Wait for TTS response from server
5. Monitor log for TTS playback activation:

```
🎵 WebSocket TTS audio callback: session=xxx, samples=xxx
🎶 Started TTS playback session: xxx
🔊 TTS playback started - activating simultaneous I2S mode
Played TTS chunk: xxx bytes (xxx samples)
```

#### 4. Full-Duplex Validation
During TTS playback, verify:
- [ ] Speaker audio output audible
- [ ] Microphone continues capturing
- [ ] No audio feedback loops
- [ ] Clean audio quality
- [ ] <50ms latency from WebSocket to speaker

### 🎯 Key Performance Targets

- **TTS Latency**: <30ms from WebSocket reception to speaker output
- **Full-Duplex Mode**: Seamless microphone+speaker operation
- **Echo Suppression**: Hardware ES7210 + Software conversation-aware
- **Memory Usage**: Minimal buffer allocation in fast memory
- **CPU Load**: <40% per core during simultaneous operation

### 🐛 Troubleshooting

#### Common Issues
1. **Base64 Decode Fails**: Check WebSocket TTS message format
2. **I2S Write Errors**: Verify pin configuration and codec initialization  
3. **Audio Dropouts**: Check DMA buffer sizing (320 samples = 20ms)
4. **Echo Feedback**: Validate ES7210 echo cancellation setup

#### Debug Commands
```bash
# Monitor TTS audio flow
grep "TTS" monitor_output.log

# Check I2S status
grep "I2S\|dual_i2s" monitor_output.log

# Verify WebSocket connection
grep "WebSocket\|tts_audio" monitor_output.log
```

### 📊 Performance Validation

Expected log outputs:
```
📊 Audio Stats - Packets sent: xxx, Loss rate: x.xx%, Latency: xx.xms
🎤 VAD: V:xxx S:xxx C:xx% Sup:xxx NF:xxx  
🔊 TTS chunk from WebSocket queued successfully (xxx bytes)
```

### 🎉 Success Criteria

✅ **WebSocket TTS Reception**: Audio chunks decoded and queued  
✅ **Speaker Output**: Clear TTS audio playback through ES8311  
✅ **Full-Duplex Mode**: Simultaneous mic capture + speaker playback  
✅ **Echo Cancellation**: No feedback loops during conversation  
✅ **Performance**: <50ms end-to-end conversation latency  

### 🚀 Next Steps

After successful testing:
1. Fine-tune ES7210 echo cancellation parameters
2. Optimize buffer sizes for minimal latency
3. Add conversation state management
4. Implement dynamic volume control
5. Production deployment validation