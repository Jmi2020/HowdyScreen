# HowdyTTS Complete Conversational System Architecture

## ⚠️ DUAL-PROJECT DEVELOPMENT APPROACH

This system requires **coordinated development across two projects**:

### **Project Locations:**
- **ESP32-P4 HowdyScreen**: `/Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen/`
- **HowdyTTS Server**: `/Users/silverlinings/Desktop/Coding/RBP/HowdyTTS/`

### **Development Workflow:**
1. **Always implement solutions in both projects simultaneously**
2. **Ensure protocol compatibility** between ESP32-P4 and HowdyTTS
3. **Update documentation in both repositories**
4. **Test end-to-end functionality** across both systems

### **Example Dual-Project Changes:**
- **WebSocket Protocols**: Update client (ESP32-P4) and server (HowdyTTS) simultaneously
- **Audio Format Changes**: Coordinate PCM encoding/decoding on both sides
- **Network Discovery**: Implement broadcast (ESP32-P4) and listening (HowdyTTS) together
- **Error Handling**: Synchronize timeout values and retry logic

## 🎯 System Overview

**HowdyTTS** is a complete conversational AI agent (STT + LLM + TTS) that provides full voice interaction:
- **Listens** for wake word "Hey Howdy" 
- **Transcribes** user speech to text (STT)
- **Processes** queries with LLM (OpenAI, Ollama, etc.)
- **Responds** with natural voice synthesis (TTS)
- **Continues** conversation until user ends it

**ESP32-P4 HowdyScreen** acts as a wireless audio interface:
- **Dual microphones** capture user speech
- **Speaker** plays HowdyTTS voice responses
- **Round display** shows conversation state
- **Touch button** ends conversations

## 📡 Bidirectional Audio Architecture

### Current Implementation Status

#### ✅ **ESP32-P4 → HowdyTTS (Audio Input)**
- **Dual Microphone Capture**: I2S audio capture from ESP32-P4 microphones
- **UDP Audio Streaming**: 16kHz PCM audio sent to HowdyTTS server (port 8000)
- **Wake Word Detection**: Local "Hey Howdy" detection with server validation  
- **Continuous Streaming**: Always-on audio for instant wake word detection
- **VAD Integration**: Enhanced VAD with ESP32-P4 + Silero fusion

#### ✅ **HowdyTTS → ESP32-P4 (Audio Output)** 
- **WebSocket TTS Server**: Port 8002 for TTS audio streaming to ESP32-P4
- **TTS Audio Playback**: PCM audio received via WebSocket, played through ESP32-P4 speaker
- **Conversation State Sync**: UI shows listening, thinking, speaking states
- **Auto-Discovery**: Network-agnostic discovery (UDP broadcast + mDNS)

### Complete Conversational Flow

```
1. 👂 ESP32-P4 continuously streams audio → HowdyTTS (UDP port 8000)
2. 🎯 HowdyTTS detects "Hey Howdy" wake word → starts listening
3. 🎤 User speaks query → ESP32-P4 captures → HowdyTTS receives  
4. 📝 HowdyTTS transcribes speech → processes with LLM → generates response
5. 🔊 HowdyTTS synthesizes TTS audio → streams to ESP32-P4 (WebSocket port 8001)
6. 📢 ESP32-P4 plays TTS response through speaker
7. 🔄 Conversation continues until touch button pressed or timeout
```

## 🎵 Audio Hardware Integration

### ESP32-P4 Audio System
- **Microphones**: Dual MEMS microphones for stereo/beamforming capture
- **Codec**: ES8311/ES7210 for high-quality audio processing  
- **Speaker**: Built-in speaker or audio jack output
- **I2S Pipeline**: Hardware-accelerated audio capture/playback
- **Format**: 16kHz, 16-bit, mono PCM for optimal streaming

### HowdyTTS Audio Processing
- **STT Engine**: Whisper, Groq, or local speech recognition
- **TTS Engine**: OpenAI, ElevenLabs, Kokoro, or local synthesis
- **VAD System**: Silero neural network for voice activity detection
- **Audio Format**: Automatic conversion to ESP32-P4 compatible PCM

## 🌐 Network Protocol Stack

### Discovery & Connection (Working ✅)
```
ESP32-P4: "HOWDYTTS_DISCOVERY" broadcast → port 8001
HowdyTTS: "HOWDYTTS_SERVER_hostname" response → registers device
Connection: UDP + WebSocket established automatically
```

### Audio Streaming Protocols

#### **Upstream Audio (ESP32-P4 → HowdyTTS)**
- **Protocol**: UDP streaming  
- **Port**: 8000
- **Format**: 16kHz, 16-bit, mono PCM
- **Packet Size**: 20ms chunks (320 samples)
- **Usage**: Wake word detection, speech recognition

#### **Downstream Audio (HowdyTTS → ESP32-P4)**  
- **Protocol**: WebSocket streaming
- **Port**: 8001  
- **Format**: 16kHz, 16-bit, mono PCM (base64 encoded in JSON)
- **Usage**: TTS response playback, conversation feedback

### WebSocket TTS Protocol
```json
{
  "type": "tts_audio",
  "session_id": "conversation_123", 
  "audio_format": "pcm_16bit_mono_16khz",
  "audio_data": "base64_encoded_pcm_data",
  "timestamp": 1673024400000
}
```

## 🔄 Conversation State Management

### ESP32-P4 UI States
- **IDLE**: "Listening for 'Hey Howdy'" (continuous audio streaming)
- **LISTENING**: "Listening..." (post wake word, capturing query)  
- **PROCESSING**: "Processing..." (HowdyTTS thinking)
- **SPEAKING**: "Speaking: [response text]" (TTS playback active)
- **ERROR**: Connection or audio issues

### HowdyTTS Session Management
- **Session Tracking**: Each conversation gets unique session ID
- **Context Preservation**: Multi-turn conversation memory
- **Timeout Handling**: Auto-end conversations after inactivity
- **Device Coordination**: Handle multiple ESP32-P4 devices

## 🎛️ User Interaction Design

### Wake Word Activation
- **Local Detection**: ESP32-P4 "Hey Howdy" processing (instant feedback)
- **Server Validation**: HowdyTTS confirms wake word (reduces false positives)
- **Dual VAD**: ESP32-P4 + Silero fusion for optimal accuracy

### Conversation Control
- **Start**: "Hey Howdy" voice activation (hands-free)
- **Continue**: Natural conversation flow (automatic turn-taking)
- **End**: Touch button press or conversation timeout

### Visual Feedback
- **Howdy Character**: Animated face shows listening/thinking/speaking
- **Audio Levels**: Real-time visualization of speech input/output
- **Connection Status**: Network and audio pipeline health indicators

## 🚀 Implementation Priorities

### Phase 1: Complete Audio Pipeline ✅
- [x] ESP32-P4 microphone capture → UDP streaming
- [x] HowdyTTS WebSocket TTS server
- [x] ESP32-P4 TTS audio playback via WebSocket
- [x] Network-agnostic discovery system

### Phase 2: Enhanced Integration 🔄  
- [ ] **Verify Speaker Output**: Ensure TTS audio plays through ESP32-P4 speaker
- [ ] **Optimize Audio Quality**: Fine-tune I2S settings, codec configuration
- [ ] **Test Conversation Flow**: Full end-to-end conversational testing
- [ ] **Performance Tuning**: Minimize audio latency, improve responsiveness

### Phase 3: Production Polish
- [ ] Multi-device support (multiple ESP32-P4 units)
- [ ] Advanced wake word customization
- [ ] Voice print recognition for personalized responses
- [ ] Over-the-air firmware updates

## 📊 Technical Specifications

### Audio Performance Targets
- **Wake Word Latency**: <500ms from "Hey" to activation
- **Speech Recognition**: <2s from end-of-speech to transcription  
- **TTS Response**: <1s from query to first audio output
- **End-to-End**: <5s total conversation turn-around time

### Network Requirements
- **Bandwidth**: ~64 kbps for bidirectional audio (16kHz PCM)
- **Latency**: <100ms network RTT for real-time feel
- **Reliability**: Auto-reconnection, graceful degradation

### Hardware Resource Usage
- **ESP32-P4 RAM**: ~100KB for audio buffers
- **ESP32-P4 CPU**: <30% utilization during conversation
- **Network**: Efficient packet sizing, compression where possible

---

## 🎯 Current Status: Phase 1 Complete ✅

**Ready for Testing:**
- ESP32-P4 captures audio → streams to HowdyTTS
- HowdyTTS processes conversations → streams TTS back  
- ESP32-P4 plays TTS audio through speaker
- Network discovery works across any WiFi network

**Next Step:** Verify complete audio flow with real conversation testing.