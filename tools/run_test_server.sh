#!/bin/bash
# Run HowdyTTS Test Server for ESP32-P4 Development

PORT=8888
NAME="ESP32-Test-Server"

echo "Starting HowdyTTS Test Server..."
echo "Port: $PORT"
echo "Name: $NAME"
echo ""
echo "Server will be available at:"
echo "  HTTP: http://localhost:$PORT"
echo "  WebSocket: ws://localhost:$PORT/howdytts"
echo ""
echo "Press Ctrl+C to stop the server"
echo ""

# Run with ESP-IDF Python environment if available
if [ -f "/Users/silverlinings/.espressif/python_env/idf5.5_py3.12_env/bin/python3" ]; then
    /Users/silverlinings/.espressif/python_env/idf5.5_py3.12_env/bin/python3 howdytts_test_server.py --port $PORT --name "$NAME"
else
    python3 howdytts_test_server.py --port $PORT --name "$NAME"
fi