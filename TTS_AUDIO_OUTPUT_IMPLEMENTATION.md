# ESP32-P4 TTS Audio Output Implementation Plan

## 🎯 Project Goal

Complete the bidirectional audio system by implementing TTS audio output on ESP32-P4 HowdyScreen devices, enabling them to play HowdyTTS server responses through their built-in speakers.

## 📊 Current State Analysis

### ✅ What's Working (Phase 6C)
- **Audio Input**: Enhanced UDP streaming with VAD and wake word detection
- **Wake Word Processing**: "Hey Howdy" edge detection with Porcupine validation
- **WebSocket Feedback**: VAD corrections and threshold adaptation (Port 8001)
- **Voice Processing**: Complete HowdyTTS pipeline (transcription → LLM → TTS generation)

### ❌ Missing Components
- **TTS Audio Reception**: ESP32-P4 doesn't receive TTS audio from server
- **Audio Output Integration**: TTS callback placeholder not connected to speakers
- **Full-Duplex Management**: No microphone/speaker conflict resolution
- **WebSocket Audio Protocol**: No TTS audio transport mechanism

### 🏗️ Available Infrastructure
- ✅ **TTS Audio Handler** (`tts_audio_handler.c`) - Complete playback system
- ✅ **ES8311/ES7210 Codec** - Hardware speaker/microphone support
- ✅ **Audio Processor** - Supports both capture and playback modes
- ✅ **WebSocket Client** - Bidirectional communication framework
- ✅ **PSRAM/Memory** - Sufficient for audio buffering

## 🎯 Implementation Strategy

### Phase 6D: Complete Bidirectional Audio System

```
┌─────────────────────────────────────┐    ┌────────────────────────────────────┐
│        ESP32-P4 HowdyScreen         │    │         HowdyTTS Server            │
├─────────────────────────────────────┤    ├────────────────────────────────────┤
│                                     │    │                                    │
│ ┌─────────────────────────────────┐ │    │ ┌──────────────────────────────┐   │
│ │          INPUT PATH             │ │    │ │       VOICE PROCESSING       │   │
│ │   Microphone → Enhanced VAD     │ │    │ │   Transcription → LLM        │   │
│ │   → Wake Word → Enhanced UDP    │ │──→ │ │   → TTS Generation           │   │
│ │   (Port 8000)                   │ │    │ │                              │   │
│ └─────────────────────────────────┘ │    │ └──────────────────────────────┘   │
│                ↕                    │    │                ↓                   │
│ ┌─────────────────────────────────┐ │    │ ┌──────────────────────────────┐   │
│ │       BIDIRECTIONAL CONTROL     │ │◄──►│ │      WebSocket Feedback      │   │
│ │   WebSocket Client (Port 8001)  │ │    │ │   - VAD corrections          │   │
│ │   - VAD feedback                │ │    │ │   - Wake word validation     │   │
│ │   - Threshold updates           │ │    │ │   - TTS audio streaming      │   │ ← NEW
│ │   - TTS audio reception         │ │    │ │   - Audio session control    │   │ ← NEW
│ └─────────────────────────────────┘ │    │ └──────────────────────────────┘   │
│                ↓                    │    │                                    │
│ ┌─────────────────────────────────┐ │    │                                    │
│ │         OUTPUT PATH             │ │    │                                    │ ← NEW
│ │   TTS Audio → Audio Processor   │ │    │                                    │ ← NEW
│ │   → ES8311 Speaker → Sound      │ │    │                                    │ ← NEW
│ └─────────────────────────────────┘ │    │                                    │
└─────────────────────────────────────┘    └────────────────────────────────────┘
```

## 🛠️ Implementation Tasks

### Task 1: WebSocket TTS Audio Protocol Extension

**Objective**: Extend WebSocket protocol to support TTS audio streaming

**Components to Modify**:
- `WEBSOCKET_PROTOCOL.md` - Add TTS audio message types
- `esp32_p4_vad_feedback.h/.c` - Add TTS audio reception functions
- Server-side WebSocket handler - Add TTS audio transmission

**New Message Types**:
```json
{
  "message_type": "tts_audio_start",
  "session_id": "tts_20230805_123456",
  "audio_format": {
    "sample_rate": 22050,
    "channels": 1,
    "bits_per_sample": 16,
    "total_samples": 44100
  },
  "playback_config": {
    "volume": 0.8,
    "fade_in_ms": 100,
    "interrupt_recording": true
  }
}

{
  "message_type": "tts_audio_chunk",
  "session_id": "tts_20230805_123456",
  "chunk_sequence": 1,
  "audio_data": "[base64-encoded-pcm-data]",
  "chunk_size": 2048,
  "is_final": false
}

{
  "message_type": "tts_audio_end",
  "session_id": "tts_20230805_123456",
  "total_chunks": 22,
  "playback_duration_ms": 2500
}
```

### Task 2: ESP32-P4 TTS Audio Reception System

**Objective**: Implement client-side TTS audio reception and buffering

**New Functions**:
```c
// In esp32_p4_vad_feedback.h
typedef struct {
    char session_id[32];
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    float volume;
    bool fade_in;
    bool interrupt_recording;
} tts_audio_session_t;

esp_err_t vad_feedback_handle_tts_audio_start(vad_feedback_handle_t handle,
                                             const tts_audio_session_t *session);

esp_err_t vad_feedback_handle_tts_audio_chunk(vad_feedback_handle_t handle,
                                             const char *session_id,
                                             uint16_t sequence,
                                             const uint8_t *audio_data,
                                             size_t data_len,
                                             bool is_final);

esp_err_t vad_feedback_handle_tts_audio_end(vad_feedback_handle_t handle,
                                           const char *session_id);
```

**Implementation Details**:
- **Session Management**: Track active TTS sessions
- **Audio Buffering**: Queue audio chunks in order
- **Memory Management**: Use PSRAM for large audio buffers
- **Error Handling**: Handle missing chunks, session timeouts

### Task 3: Full-Duplex Audio Management

**Objective**: Coordinate microphone input and speaker output to prevent conflicts

**New Component**: `components/audio_processor/src/full_duplex_manager.c`

**Key Features**:
```c
typedef enum {
    AUDIO_MODE_IDLE,           // No audio activity
    AUDIO_MODE_LISTENING,      // Microphone active, speaker muted
    AUDIO_MODE_SPEAKING,       // Speaker active, microphone muted
    AUDIO_MODE_WAKE_WORD,      // Brief speaker feedback, mic stays active
    AUDIO_MODE_ERROR          // Error state
} audio_mode_t;

typedef struct {
    audio_mode_t current_mode;
    bool echo_cancellation_enabled;
    uint16_t speaker_to_mic_delay_ms;  // Hardware echo delay
    float mic_attenuation_during_playback;
    bool quick_mode_switching;         // For wake word feedback
} full_duplex_config_t;

// Core functions
esp_err_t full_duplex_manager_init(const full_duplex_config_t *config);
esp_err_t full_duplex_set_mode(audio_mode_t mode);
esp_err_t full_duplex_play_tts_audio(const int16_t *audio_data, size_t samples);
bool full_duplex_can_record_audio(void);
```

**Echo Prevention Strategies**:
1. **Time-Division**: Stop recording during TTS playback
2. **Echo Cancellation**: Use built-in ES8311 echo cancellation
3. **Adaptive Gains**: Reduce microphone sensitivity during playback
4. **Hardware Isolation**: Physical separation of mic/speaker

### Task 4: TTS Callback Integration

**Objective**: Connect existing TTS callback to complete audio playback system

**Current Code** (`howdy_phase6_howdytts_integration.c:261`):
```c
static esp_err_t howdytts_tts_callback(const int16_t *tts_audio, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "TTS callback: received %d samples from HowdyTTS server", (int)samples);
    
    // TODO: Integrate with audio processor for speaker output  ← IMPLEMENT THIS
    
    return ESP_OK;
}
```

**New Implementation**:
```c
static esp_err_t howdytts_tts_callback(const int16_t *tts_audio, size_t samples, void *user_data)
{
    ESP_LOGI(TAG, "🔊 TTS callback: received %zu samples from HowdyTTS server", samples);
    
    // Switch to speaking mode (mutes microphone)
    esp_err_t ret = full_duplex_set_mode(AUDIO_MODE_SPEAKING);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set speaking mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Update UI to show speaking state
    ui_manager_set_state(UI_STATE_SPEAKING);
    ui_manager_update_status("Playing TTS response...");
    
    // Play audio through full-duplex manager
    ret = full_duplex_play_tts_audio(tts_audio, samples);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play TTS audio: %s", esp_err_to_name(ret));
        full_duplex_set_mode(AUDIO_MODE_IDLE);
        return ret;
    }
    
    // Schedule return to listening mode after playback
    // (This will be handled by TTS playback completion callback)
    
    return ESP_OK;
}
```

### Task 5: Audio Session State Management

**Objective**: Coordinate voice assistant states across the complete pipeline

**Enhanced State Machine**:
```c
typedef enum {
    VA_STATE_IDLE,              // Waiting for wake word
    VA_STATE_WAKE_WORD,         // Wake word detected/validating
    VA_STATE_LISTENING,         // Recording user speech
    VA_STATE_PROCESSING,        // Server processing (transcription + LLM)
    VA_STATE_SPEAKING,          // Playing TTS response
    VA_STATE_COOLDOWN,          // Brief pause before returning to idle
    VA_STATE_ERROR              // Error recovery
} enhanced_va_state_t;
```

**State Transitions**:
```
IDLE → WAKE_WORD → LISTENING → PROCESSING → SPEAKING → COOLDOWN → IDLE
  ↓                                           ↑
  ERROR ←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←
```

## 🔧 Technical Implementation Details

### Audio Format Specifications
- **TTS Sample Rate**: 22050 Hz (HowdyTTS default) → Resample to 16000 Hz if needed
- **Bit Depth**: 16-bit PCM (matches existing pipeline)
- **Channels**: Mono (1 channel)
- **Buffer Size**: 2048 samples (128ms @ 16kHz) for low latency
- **Codec Configuration**: ES8311 for playback, ES7210 for capture

### Memory Management
- **TTS Audio Buffers**: Allocate in PSRAM (up to 32MB available)
- **Streaming Buffers**: 4 x 2KB circular buffers for smooth playback
- **Session Tracking**: Maximum 2 concurrent TTS sessions
- **Memory Protection**: Free buffers immediately after playback

### Performance Targets
- **TTS Latency**: < 500ms from server response to audio start
- **Audio Quality**: No dropouts, clicks, or distortion
- **Echo Prevention**: < -40dB echo suppression
- **CPU Usage**: < 40% total (audio processing + UI + networking)
- **Memory Usage**: < 256KB for TTS buffers (excluding PSRAM)

### Error Handling
- **Network Issues**: Graceful degradation, buffer timeouts
- **Audio Codec Errors**: Automatic reset and recovery
- **Memory Exhaustion**: Priority-based buffer management
- **Session Conflicts**: Interrupt lower-priority audio

## 📝 Implementation Sequence

### Phase 6D-1: Protocol Extension (Days 1-2)
1. ✅ Design WebSocket TTS audio message format
2. ✅ Update protocol documentation
3. ✅ Implement server-side TTS audio transmission
4. ✅ Add client-side message parsing

### Phase 6D-2: ESP32-P4 Audio Reception (Days 3-4)
1. ✅ Implement TTS session management
2. ✅ Add audio chunk buffering and ordering
3. ✅ Integrate with existing WebSocket client
4. ✅ Add memory management for audio buffers

### Phase 6D-3: Full-Duplex Audio Manager (Days 5-6)
1. ✅ Create full-duplex manager component
2. ✅ Implement echo prevention strategies  
3. ✅ Add audio mode state machine
4. ✅ Integrate with ES8311/ES7210 codec control

### Phase 6D-4: Integration & Testing (Days 7-8)
1. ✅ Connect TTS callback to playback system
2. ✅ Update UI state management
3. ✅ Add comprehensive error handling
4. ✅ Performance optimization and testing

### Phase 6D-5: Validation & Documentation (Days 9-10)
1. ✅ End-to-end system testing
2. ✅ Performance benchmarking
3. ✅ Update all documentation
4. ✅ Create user guides and troubleshooting

## 🎯 Expected Outcomes

### Complete Bidirectional Voice Assistant
```
User: "Hey Howdy"
ESP32-P4: [Visual wake word confirmation] 🎯
User: "What's the weather like?"
ESP32-P4: [Streams audio to HowdyTTS] → Server processes → [Receives TTS audio]
ESP32-P4: [Plays through speaker] "It's sunny and 75 degrees today."
ESP32-P4: [Returns to listening mode] 👂
```

### System Capabilities
- ✅ **Complete Voice Pipeline**: Wake word → Speech recognition → LLM → TTS playback
- ✅ **High-Quality Audio**: Professional-grade audio processing with echo cancellation
- ✅ **Responsive UI**: Real-time visual feedback throughout voice interaction
- ✅ **Multi-Device Support**: Room-based coordination with shared TTS responses
- ✅ **Adaptive Performance**: Self-optimizing based on network and audio conditions

### Performance Metrics
- **End-to-End Latency**: < 2 seconds (wake word → TTS playback start)
- **Audio Quality**: CD-quality output (16-bit, no compression artifacts)
- **System Reliability**: > 99.5% successful voice interactions
- **Echo Suppression**: < -40dB (professional audio standard)
- **Power Efficiency**: < 2W average power consumption

## 🚀 Getting Started

### Prerequisites
- ✅ Phase 6C implementation (bidirectional VAD + wake word)
- ✅ ESP32-P4 with ES8311/ES7210 audio codec
- ✅ HowdyTTS server with WebSocket support
- ✅ Stable WiFi network (5GHz recommended)

### Implementation Command
```bash
# Ready to begin Phase 6D implementation
cd /Users/silverlinings/Desktop/Coding/ESP32P4/HowdyScreen
git checkout -b phase6d-tts-audio-output
```

This implementation will complete the ESP32-P4 HowdyScreen as a fully autonomous voice assistant with professional-grade bidirectional audio processing.

---

**Next Step**: Begin Task 1 - WebSocket TTS Audio Protocol Extension