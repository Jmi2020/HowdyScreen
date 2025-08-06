# ESP32-P4 WebSocket Feedback Protocol Specification

## Overview

The WebSocket Feedback Protocol enables real-time bidirectional communication between ESP32-P4 HowdyScreen devices and the HowdyTTS server for VAD corrections, wake word validation, and threshold adaptation.

## Connection Details

- **Server Port**: 8001
- **Protocol**: WebSocket over TCP
- **Message Format**: JSON
- **Connection Type**: Persistent with automatic reconnection
- **Keep-alive**: 30-second ping/pong intervals

## Connection Establishment

### ESP32-P4 Client Connection
```c
// WebSocket URI format
ws://server_ip:8001/vad_feedback

// Connection headers
GET /vad_feedback HTTP/1.1
Host: 192.168.1.100:8001
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: [base64-encoded-key]
Sec-WebSocket-Version: 13
Sec-WebSocket-Protocol: howdy-vad-v1
```

### Server Response
```http
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: [calculated-accept-key]
Sec-WebSocket-Protocol: howdy-vad-v1
```

## Message Types

### 1. Device Registration

**Direction**: ESP32-P4 → Server  
**Purpose**: Register device and provide capabilities

**ESP32-P4 Request:**
```json
{
  "message_type": "device_registration",
  "timestamp": 1691234567890,
  "device_id": "esp32_p4_device_01",
  "device_info": {
    "device_name": "HowdyScreen Living Room",
    "room": "Living Room",
    "firmware_version": "Phase6C-v1.0",
    "capabilities": [
      "enhanced_vad",
      "wake_word_detection",
      "audio_visualization",
      "threshold_adaptation"
    ],
    "hardware_info": {
      "chip_model": "ESP32-P4",
      "psram_size": 33554432,
      "flash_size": 16777216,
      "audio_codec": "ES8311/ES7210",
      "display": "800x800_round_lcd"
    },
    "network_info": {
      "ip_address": "192.168.1.101",
      "mac_address": "AA:BB:CC:DD:EE:FF",
      "wifi_rssi": -45,
      "connection_quality": "excellent"
    }
  }
}
```

**Server Response:**
```json
{
  "message_type": "registration_ack",
  "timestamp": 1691234567950,
  "device_id": "esp32_p4_device_01",
  "status": "registered",
  "assigned_room": "Living Room",
  "server_info": {
    "server_version": "HowdyTTS-v2.1",
    "supported_protocols": ["enhanced_udp_v3", "websocket_v1"],
    "vad_fusion_strategy": "adaptive",
    "wake_word_models": ["hey_howdy", "hey_google", "alexa"]
  },
  "initial_config": {
    "vad_sensitivity": 0.7,
    "wake_word_threshold": 0.65,
    "audio_level_smoothing": 0.8,
    "feedback_interval_ms": 100
  }
}
```

### 2. Wake Word Detection Event

**Direction**: ESP32-P4 → Server  
**Purpose**: Report wake word detection for validation

**ESP32-P4 Message:**
```json
{
  "message_type": "wake_word_detection",
  "timestamp": 1691234568123,
  "device_id": "esp32_p4_device_01",
  "detection_id": "ww_20230805_123456_789",
  "wake_word_data": {
    "keyword": "hey_howdy",
    "keyword_id": 1,
    "confidence_score": 0.72,
    "confidence_level": "high",
    "detection_start_ms": 150,
    "detection_duration_ms": 680,
    "syllable_count": 3,
    "pattern_match_score": 0.78,
    "energy_level": 4500,
    "noise_floor": 1200
  },
  "audio_context": {
    "sample_rate": 16000,
    "vad_confidence": 0.85,
    "snr_db": 18.5,
    "max_amplitude": 8750,
    "zero_crossing_rate": 125
  },
  "device_state": {
    "battery_level": 85,
    "cpu_usage": 25,
    "memory_free": 245760,
    "temperature": 42.5
  }
}
```

### 3. Wake Word Validation Response

**Direction**: Server → ESP32-P4  
**Purpose**: Provide wake word validation results and feedback

**Server Response:**
```json
{
  "message_type": "wake_word_validation",
  "timestamp": 1691234568200,
  "device_id": "esp32_p4_device_01",
  "detection_id": "ww_20230805_123456_789",
  "validation_result": {
    "validated": true,
    "server_confidence": 0.89,
    "porcupine_confidence": 0.92,
    "processing_time_ms": 75,
    "validation_method": "porcupine_hybrid",
    "consensus_score": 0.805
  },
  "feedback": {
    "feedback_text": "Excellent detection quality",
    "detection_quality_score": 92,
    "improvement_suggestions": [
      "Good syllable separation",
      "Clear audio quality",
      "Optimal distance from microphone"
    ]
  },
  "threshold_recommendations": {
    "suggest_threshold_adjustment": false,
    "current_threshold_performance": "optimal",
    "edge_vs_server_accuracy": {
      "edge_accuracy": 0.78,
      "server_accuracy": 0.94,
      "fusion_accuracy": 0.89
    }
  }
}
```

### 4. Threshold Update Command

**Direction**: Server → ESP32-P4  
**Purpose**: Update VAD and wake word detection thresholds

**Server Message:**
```json
{
  "message_type": "threshold_update",
  "timestamp": 1691234568500,
  "device_id": "esp32_p4_device_01",
  "update_reason": "adaptive_learning",
  "threshold_updates": {
    "vad_energy_threshold": 3200,
    "vad_confidence_threshold": 0.75,
    "wake_word_confidence_threshold": 0.68,
    "wake_word_energy_threshold": 3800,
    "noise_floor_adjustment": 1150
  },
  "adaptation_info": {
    "learning_cycles": 15,
    "accuracy_improvement": 0.12,
    "false_positive_rate": 0.03,
    "false_negative_rate": 0.01,
    "recommended_duration": "permanent"
  },
  "urgency": "medium",
  "expires_at": 1691320967890
}
```

**ESP32-P4 Response:**
```json
{
  "message_type": "threshold_update_ack",
  "timestamp": 1691234568520,
  "device_id": "esp32_p4_device_01",
  "status": "applied",
  "applied_thresholds": {
    "vad_energy_threshold": 3200,
    "vad_confidence_threshold": 0.75,
    "wake_word_confidence_threshold": 0.68,
    "wake_word_energy_threshold": 3800,
    "noise_floor_adjustment": 1150
  },
  "application_time_ms": 45,
  "restart_required": false
}
```

### 5. VAD Feedback Event

**Direction**: Server → ESP32-P4  
**Purpose**: Provide VAD decision corrections for learning

**Server Message:**
```json
{
  "message_type": "vad_feedback",
  "timestamp": 1691234569000,
  "device_id": "esp32_p4_device_01",
  "feedback_data": {
    "vad_decision_id": "vad_20230805_123459_123",
    "edge_decision": "voice_active",
    "edge_confidence": 0.68,
    "server_decision": "voice_active",  
    "server_confidence": 0.91,
    "final_decision": "voice_active",
    "decision_correct": true,
    "fusion_strategy_used": "confidence_weighted"
  },
  "learning_feedback": {
    "edge_performance_rating": "good",
    "areas_for_improvement": [
      "spectral_analysis_tuning",
      "noise_floor_adaptation"
    ],
    "recommended_adjustments": {
      "consistency_frames": 5,
      "spectral_weight": 0.6,
      "energy_weight": 0.4
    }
  }
}
```

### 6. Statistics Request/Response

**Direction**: Server → ESP32-P4 (Request), ESP32-P4 → Server (Response)  
**Purpose**: Collect device performance statistics

**Server Request:**
```json
{
  "message_type": "statistics_request",
  "timestamp": 1691234570000,
  "device_id": "esp32_p4_device_01",
  "requested_stats": [
    "wake_word_performance",
    "vad_performance", 
    "system_performance",
    "network_performance"
  ],
  "time_range": {
    "start_time": 1691148167890,
    "end_time": 1691234567890
  }
}
```

**ESP32-P4 Response:**
```json
{
  "message_type": "statistics_response",
  "timestamp": 1691234570100,
  "device_id": "esp32_p4_device_01",
  "statistics": {
    "wake_word_performance": {
      "total_detections": 47,
      "true_positives": 42,
      "false_positives": 5,
      "false_negatives": 2,
      "average_confidence": 0.74,
      "detection_latency_ms": 68,
      "accuracy_rate": 0.894
    },
    "vad_performance": {
      "total_vad_decisions": 18420,
      "correct_decisions": 17156,
      "accuracy_rate": 0.931,
      "average_processing_time_us": 2150,
      "false_positive_rate": 0.045,
      "false_negative_rate": 0.024
    },
    "system_performance": {
      "uptime_hours": 72.5,
      "cpu_usage_avg": 28,
      "memory_usage_avg": 68,
      "temperature_avg": 41.2,
      "restart_count": 0,
      "error_count": 3
    },
    "network_performance": {
      "packets_sent": 8420,
      "packets_received": 156,
      "bytes_transmitted": 67532800,
      "bytes_received": 23456,
      "average_latency_ms": 12,
      "connection_drops": 1,
      "reconnection_count": 1
    }
  }
}
```

### 7. Training Data Request

**Direction**: Server → ESP32-P4  
**Purpose**: Request training audio samples

**Server Message:**
```json
{
  "message_type": "training_request",
  "timestamp": 1691234571000,
  "device_id": "esp32_p4_device_01",
  "training_info": {
    "collect_positive_samples": true,
    "collect_negative_samples": true,
    "sample_duration_ms": 2000,
    "samples_requested": 10,
    "wake_word_target": "hey_howdy",
    "quality_requirements": {
      "min_snr_db": 10,
      "max_background_noise": 2000,
      "required_distance_cm": "50-200"
    }
  },
  "collection_instructions": {
    "user_prompt": "Please say 'Hey Howdy' clearly",
    "collection_timeout_s": 300,
    "display_instructions": true,
    "audio_feedback": true
  }
}
```

### 8. Keep-alive Messages

**Direction**: Bidirectional  
**Purpose**: Maintain connection and check latency

**Ping (ESP32-P4 → Server):**
```json
{
  "message_type": "ping",
  "timestamp": 1691234572000,
  "device_id": "esp32_p4_device_01",
  "sequence": 1234
}
```

**Pong (Server → ESP32-P4):**
```json
{
  "message_type": "pong", 
  "timestamp": 1691234572015,
  "device_id": "esp32_p4_device_01",
  "sequence": 1234,
  "server_load": {
    "cpu_usage": 45,
    "memory_usage": 62,
    "active_devices": 3,
    "processing_queue_size": 2
  }
}
```

### 9. TTS Audio Streaming Messages

**Direction**: Server → ESP32-P4  
**Purpose**: Stream TTS audio responses to ESP32-P4 speakers

#### TTS Audio Session Start

**Server Message:**
```json
{
  "message_type": "tts_audio_start",
  "timestamp": 1691234574000,
  "device_id": "esp32_p4_device_01",
  "session_info": {
    "session_id": "tts_20230805_123456_789",
    "response_text": "It's sunny and 75 degrees today.",
    "estimated_duration_ms": 2500,
    "total_chunks_expected": 22
  },
  "audio_format": {
    "sample_rate": 16000,
    "channels": 1,
    "bits_per_sample": 16,
    "encoding": "pcm",
    "total_samples": 40000
  },
  "playback_config": {
    "volume": 0.8,
    "fade_in_ms": 100,
    "fade_out_ms": 100,
    "interrupt_recording": true,
    "echo_cancellation": true
  }
}
```

#### TTS Audio Chunk

**Server Message:**
```json
{
  "message_type": "tts_audio_chunk",
  "timestamp": 1691234574050,
  "device_id": "esp32_p4_device_01",
  "chunk_info": {
    "session_id": "tts_20230805_123456_789",
    "chunk_sequence": 1,
    "chunk_size": 2048,
    "audio_data": "[base64-encoded-pcm-data]",
    "is_final": false,
    "checksum": "a1b2c3d4"
  },
  "timing": {
    "chunk_start_time_ms": 0,
    "chunk_duration_ms": 128,
    "playback_priority": "normal"
  }
}
```

#### TTS Audio Session End

**Server Message:**
```json
{
  "message_type": "tts_audio_end",
  "timestamp": 1691234576550,
  "device_id": "esp32_p4_device_01",
  "session_summary": {
    "session_id": "tts_20230805_123456_789",
    "total_chunks_sent": 22,
    "total_audio_bytes": 44800,
    "actual_duration_ms": 2500,
    "transmission_time_ms": 450
  },
  "playback_actions": {
    "fade_out_ms": 100,
    "return_to_listening": true,
    "cooldown_period_ms": 500
  }
}
```

#### TTS Playback Status (ESP32-P4 Response)

**ESP32-P4 Message:**
```json
{
  "message_type": "tts_playback_status",
  "timestamp": 1691234574100,
  "device_id": "esp32_p4_device_01",
  "status_info": {
    "session_id": "tts_20230805_123456_789",
    "playback_state": "playing",
    "chunks_received": 5,
    "chunks_played": 3,
    "buffer_level_ms": 256,
    "audio_quality": "excellent"
  },
  "performance": {
    "playback_latency_ms": 85,
    "buffer_underruns": 0,
    "audio_dropouts": 0,
    "echo_suppression_db": -42.5
  }
}
```

### 10. Error Messages

**Direction**: Bidirectional  
**Purpose**: Report errors and exceptions

**Error Message Format:**
```json
{
  "message_type": "error",
  "timestamp": 1691234573000,
  "device_id": "esp32_p4_device_01",
  "error_info": {
    "error_code": "WW_PROCESSING_TIMEOUT",
    "error_category": "wake_word_detection",
    "severity": "warning",
    "description": "Wake word processing took longer than expected",
    "details": {
      "processing_time_ms": 1250,
      "expected_time_ms": 500,
      "memory_pressure": false,
      "cpu_overload": false
    }
  },
  "recovery_action": "increased_buffer_size",
  "user_impact": "minor_delay"
}
```

## Protocol State Machine

### ESP32-P4 Client States
```
DISCONNECTED → CONNECTING → CONNECTED → REGISTERED → ACTIVE
     ↑                                                    ↓
     ←←←←←←←← ERROR/TIMEOUT ←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←
```

### Connection Recovery
- **Automatic reconnection** with exponential backoff (1s, 2s, 4s, 8s, max 30s)
- **State preservation** during reconnection
- **Message queuing** for offline periods
- **Resume registration** after reconnection

## Security Considerations

### Authentication
- **Device certificates**: Optional TLS client certificates
- **API keys**: Include in registration message
- **IP whitelisting**: Server-side IP filtering

### Data Protection  
- **TLS encryption**: Optional WSS:// support
- **Message validation**: JSON schema validation
- **Rate limiting**: Per-device message rate limits

## Performance Characteristics

### Latency Targets
- **Wake word validation**: < 200ms
- **Threshold updates**: < 100ms
- **Statistics requests**: < 500ms
- **Keep-alive**: < 50ms

### Throughput Limits
- **Message rate**: Up to 10 messages/second per device
- **Concurrent devices**: Up to 50 devices per server
- **Message size**: Maximum 32KB per message

## Implementation Examples

### ESP32-P4 WebSocket Client (C)
```c
#include "esp_websocket_client.h"

// WebSocket configuration
esp_websocket_client_config_t websocket_cfg = {
    .uri = "ws://192.168.1.100:8001/vad_feedback",
    .keepalive_idle_sec = 30,
    .keepalive_interval_sec = 10,
    .keepalive_count = 3,
    .reconnect_timeout_ms = 10000,
    .network_timeout_ms = 5000,
    .buffer_size = 8192
};

// Event handler
void websocket_event_handler(void *handler_args, esp_event_base_t base,
                           int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            send_device_registration();
            break;
            
        case WEBSOCKET_EVENT_DATA:
            process_websocket_message(data->data_ptr, data->data_len);
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            schedule_reconnection();
            break;
    }
}

// Send wake word detection
void send_wake_word_detection(const wake_word_result_t *result) {
    cJSON *json = cJSON_CreateObject();
    cJSON *wake_word_data = cJSON_CreateObject();
    
    cJSON_AddStringToObject(json, "message_type", "wake_word_detection");
    cJSON_AddNumberToObject(json, "timestamp", esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(json, "device_id", device_id);
    
    cJSON_AddNumberToObject(wake_word_data, "confidence_score", result->confidence);
    cJSON_AddNumberToObject(wake_word_data, "detection_duration_ms", result->duration_ms);
    cJSON_AddItemToObject(json, "wake_word_data", wake_word_data);
    
    char *json_string = cJSON_Print(json);
    esp_websocket_client_send_text(client, json_string, strlen(json_string), portMAX_DELAY);
    
    free(json_string);
    cJSON_Delete(json);
}
```

### HowdyTTS Server (Python)
```python
import asyncio
import websockets
import json
from datetime import datetime

class ESP32P4WebSocketServer:
    def __init__(self, host="0.0.0.0", port=8001):
        self.host = host
        self.port = port
        self.connected_devices = {}
        
    async def handle_client(self, websocket, path):
        device_id = None
        try:
            async for message in websocket:
                data = json.loads(message)
                device_id = data.get("device_id")
                
                if data["message_type"] == "device_registration":
                    await self.handle_registration(websocket, data)
                elif data["message_type"] == "wake_word_detection":
                    await self.handle_wake_word_detection(websocket, data)
                elif data["message_type"] == "ping":
                    await self.handle_ping(websocket, data)
                    
        except websockets.exceptions.ConnectionClosed:
            if device_id and device_id in self.connected_devices:
                del self.connected_devices[device_id]
                
    async def handle_wake_word_detection(self, websocket, data):
        # Validate with Porcupine
        validation_result = await self.validate_with_porcupine(data["wake_word_data"])
        
        # Send validation response
        response = {
            "message_type": "wake_word_validation",
            "timestamp": int(datetime.now().timestamp() * 1000),
            "device_id": data["device_id"],
            "detection_id": data["detection_id"],
            "validation_result": validation_result
        }
        
        await websocket.send(json.dumps(response))
        
        # If validated, trigger HowdyTTS wake word callback
        if validation_result["validated"]:
            self.trigger_wake_word_callback()
```

## Testing and Debugging

### Message Logging
Enable debug logging to trace message flow:
```c
// ESP32-P4 side
#define WEBSOCKET_DEBUG_LEVEL ESP_LOG_DEBUG
ESP_LOGD("WEBSOCKET", "Sending: %s", json_string);
```

```python
# Server side
import logging
logging.getLogger("websockets").setLevel(logging.DEBUG)
logging.getLogger("howdy.esp32p4").setLevel(logging.DEBUG)
```

### Protocol Validation
Use JSON schema validation to ensure message correctness:
```python
import jsonschema

wake_word_schema = {
    "type": "object",
    "required": ["message_type", "timestamp", "device_id", "detection_id"],
    "properties": {
        "message_type": {"const": "wake_word_detection"},
        "confidence_score": {"type": "number", "minimum": 0, "maximum": 1}
    }
}

jsonschema.validate(message_data, wake_word_schema)
```

This WebSocket protocol enables sophisticated bidirectional communication between ESP32-P4 devices and HowdyTTS, supporting real-time VAD corrections, wake word validation, and adaptive learning for optimal voice assistant performance.