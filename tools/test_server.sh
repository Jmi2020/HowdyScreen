#!/bin/bash
# Test script for HowdyTTS server

echo "Testing HowdyTTS Test Server on port 8888..."
echo ""

# Test health endpoint
echo "1. Testing /health endpoint:"
curl -s http://localhost:8888/health | python3 -m json.tool || echo "Health endpoint not ready"
echo ""

# Test config endpoint  
echo "2. Testing /config endpoint:"
curl -s http://localhost:8888/config | python3 -m json.tool || echo "Config endpoint not ready"
echo ""

# Test device registration
echo "3. Testing device registration:"
curl -s -X POST http://localhost:8888/devices/register \
  -H "Content-Type: application/json" \
  -d '{
    "device_id": "esp32p4-howdy-001",
    "device_name": "ESP32-P4 HowdyScreen Display",
    "capabilities": "display,touch,audio,tts,lvgl,websocket"
  }' | python3 -m json.tool || echo "Registration endpoint not ready"
echo ""

# List devices
echo "4. Listing registered devices:"
curl -s http://localhost:8888/devices | python3 -m json.tool || echo "Devices endpoint not ready"
echo ""

echo "Test complete!"