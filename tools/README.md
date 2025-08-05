# HowdyTTS Test Server

A comprehensive test server that simulates a HowdyTTS server for ESP32-P4 HowdyScreen development and testing.

## Features

- **mDNS Service Advertisement**: Advertises as `_howdytts._tcp` service for automatic discovery
- **HTTP REST API**: Complete endpoints for health checks, configuration, and device registration
- **WebSocket Communication**: Real-time voice communication endpoint at `/howdytts`
- **Mock Voice Assistant**: Simulates TTS responses and voice recognition
- **System Monitoring**: Real-time CPU, memory, and connection statistics
- **Development Tools**: Echo tests, configurable responses, and detailed logging

## Quick Start

### 1. Install Dependencies

```bash
cd tools/
pip3 install -r requirements.txt
```

### 2. Start the Test Server

```bash
python3 howdytts_test_server.py --port 8080 --name "ESP32-Test-Server"
```

### 3. Verify Operation

The server will display startup information:
```
🎉 HowdyTTS Test Server ready!
   📍 HTTP Server: http://0.0.0.0:8080
   🔍 mDNS Service: ESP32-Test-Server._howdytts._tcp.local.
   🔌 WebSocket: ws://localhost:8080/howdytts
   🏥 Health Check: http://localhost:8080/health
   ⚙️  Configuration: http://localhost:8080/config

Ready for ESP32-P4 HowdyScreen connections!
```

## API Endpoints

### HTTP REST API

#### `GET /health`
Returns server health status and system metrics:
```json
{
  "status": "healthy",
  "version": "1.0.0-test",
  "uptime_seconds": 123.45,
  "cpu_usage": 0.15,
  "memory_usage": 0.45,
  "active_sessions": 2,
  "registered_devices": 1,
  "capabilities": ["tts", "voice", "websocket", "test_mode"]
}
```

#### `GET /config`
Returns server configuration:
```json
{
  "server_name": "ESP32-Test-Server",
  "features": {
    "text_to_speech": true,
    "voice_recognition": true,
    "websocket_streaming": true
  },
  "audio_config": {
    "sample_rate": 16000,
    "channels": 1,
    "format": "pcm_s16le"
  }
}
```

#### `POST /devices/register`
Register a device with the server:
```json
{
  "device_id": "esp32p4-howdy-001",
  "device_name": "ESP32-P4 HowdyScreen Display",
  "capabilities": "display,touch,audio,tts,lvgl,websocket"
}
```

### WebSocket Protocol

#### Connection
Connect to `ws://server:port/howdytts` with protocol `howdytts-v1`

#### Message Types

**Ping/Pong**:
```json
{"type": "ping"}
{"type": "pong", "timestamp": "2025-08-05T12:34:56.789Z"}
```

**Text-to-Speech**:
```json
{
  "type": "text_to_speech",
  "text": "Hello world",
  "voice": "default"
}
```

**Voice Recognition**:
```json
{"type": "voice_start"}
{"type": "voice_end", "mock_result": "Optional mock text"}
```

**Echo Test**:
```json
{
  "type": "echo_test",
  "message": "test data"
}
```

## Testing with ESP32-P4

### 1. Start Test Server
```bash
python3 howdytts_test_server.py
```

### 2. Flash and Monitor ESP32-P4
```bash
idf.py flash monitor
```

### 3. Expected Behavior

The ESP32-P4 should:
1. Connect to WiFi
2. Discover the test server via mDNS
3. Perform HTTP health check
4. Establish WebSocket connection
5. Show "connected" status in system monitor

### 4. Verify Connection

Check the test server logs for:
```
Device registered: esp32p4-howdy-001 (ESP32-P4 HowdyScreen Display)
WebSocket client connected: 192.168.86.37 (Session: ws_session_xxx)
```

## Development Features

### Mock Responses
- **Health Status**: Configurable server health and performance metrics
- **TTS Simulation**: Mock audio generation with realistic delays
- **Voice Recognition**: Configurable mock text responses
- **Error Simulation**: Test error handling by modifying server responses

### Logging
- **Detailed Logs**: All HTTP requests, WebSocket connections, and message exchanges
- **Performance Metrics**: Real-time CPU, memory, and connection statistics
- **Debug Mode**: Use `--verbose` flag for detailed debugging information

### Customization
- **Server Name**: Customize mDNS service name with `--name` parameter
- **Port Configuration**: Change server port with `--port` parameter
- **Response Delays**: Modify processing delays to test timeout handling
- **Mock Data**: Customize mock responses for different testing scenarios

## Troubleshooting

### mDNS Not Working
- Ensure firewall allows multicast traffic
- Check network supports mDNS (some corporate networks block it)
- Verify ESP32-P4 and test server are on same network segment

### WebSocket Connection Fails
- Check firewall settings for the server port
- Verify ESP32-P4 WebSocket client configuration
- Use browser developer tools to test WebSocket connection manually

### HTTP Endpoints Not Responding
- Confirm server started successfully on specified port
- Test with curl: `curl http://localhost:8080/health`
- Check for port conflicts with other services

## Advanced Usage

### Custom Test Scenarios

Modify the server to test specific scenarios:

1. **Server Failures**: Add artificial delays or errors
2. **Load Testing**: Simulate multiple device connections
3. **Protocol Testing**: Test different WebSocket message formats
4. **Recovery Testing**: Simulate network interruptions

### Integration Testing

Use the test server for automated testing:

```python
import requests
import websockets

# Test HTTP endpoints
response = requests.get('http://localhost:8080/health')
assert response.json()['status'] == 'healthy'

# Test WebSocket connection
async with websockets.connect('ws://localhost:8080/howdytts') as ws:
    await ws.send('{"type": "ping"}')
    response = await ws.recv()
    assert 'pong' in response
```

## Contributing

To add new features or test scenarios:

1. Modify `howdytts_test_server.py`
2. Add new HTTP endpoints or WebSocket message types
3. Update this documentation
4. Test with ESP32-P4 HowdyScreen system

---

*HowdyTTS Test Server - ESP32-P4 HowdyScreen Development Tools*