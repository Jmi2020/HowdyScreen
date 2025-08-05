#!/usr/bin/env python3
"""
HowdyTTS Test Server Simulator
=====================================

A development server that simulates a HowdyTTS server for testing ESP32-P4 HowdyScreen integration.

Features:
- mDNS service advertisement (_howdytts._tcp)
- HTTP REST API endpoints (/health, /config, /devices/register)
- WebSocket endpoint (/howdytts) for real-time communication
- Mock voice assistant responses
- Configurable server behavior for testing different scenarios

Usage:
    python3 howdytts_test_server.py [--port 8080] [--name "Test-HowdyTTS"]
"""

import asyncio
import json
import logging
import argparse
import signal
import sys
import time
from datetime import datetime
from typing import Dict, List, Optional

# Third-party imports
try:
    import websockets
    from websockets.server import WebSocketServerProtocol
    from aiohttp import web, WSMsgType
    from aiohttp.web_ws import WebSocketResponse
    import aiohttp_cors
    from zeroconf import ServiceInfo, Zeroconf
    import psutil
except ImportError as e:
    print(f"Missing required dependency: {e}")
    print("Install with: pip3 install websockets aiohttp aiohttp-cors zeroconf psutil")
    sys.exit(1)

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('HowdyTTS-TestServer')

class HowdyTTSTestServer:
    """HowdyTTS Test Server for ESP32-P4 development and testing"""
    
    def __init__(self, port: int = 8080, name: str = "Test-HowdyTTS"):
        self.port = port
        self.name = name
        self.start_time = time.time()
        
        # Server state
        self.registered_devices: Dict[str, dict] = {}
        self.active_sessions: List[dict] = []
        self.websocket_clients: List[WebSocketResponse] = []
        
        # mDNS service
        self.zeroconf = None
        self.service_info = None
        
        # HTTP server
        self.app = None
        self.runner = None
        self.site = None
        
    async def setup_http_server(self):
        """Setup HTTP server with API endpoints"""
        self.app = web.Application()
        
        # Setup CORS
        cors = aiohttp_cors.setup(self.app, defaults={
            "*": aiohttp_cors.ResourceOptions(
                allow_credentials=True,
                expose_headers="*",
                allow_headers="*",
                allow_methods="*"
            )
        })
        
        # API Routes
        self.app.router.add_get('/health', self.health_endpoint)
        self.app.router.add_get('/config', self.config_endpoint)
        self.app.router.add_post('/devices/register', self.register_device_endpoint)
        self.app.router.add_get('/devices', self.list_devices_endpoint)
        self.app.router.add_get('/sessions', self.list_sessions_endpoint)
        self.app.router.add_get('/howdytts', self.websocket_endpoint)
        
        # Add CORS to all routes
        for route in list(self.app.router.routes()):
            cors.add(route)
        
        # Setup runner
        self.runner = web.AppRunner(self.app)
        await self.runner.setup()
        self.site = web.TCPSite(self.runner, '0.0.0.0', self.port)
        await self.site.start()
        
        logger.info(f"HTTP server started on port {self.port}")
    
    def setup_mdns(self):
        """Setup mDNS service advertisement"""
        try:
            self.zeroconf = Zeroconf()
            
            # Service properties
            properties = {
                b'version': b'1.0.0',
                b'capabilities': b'tts,voice,websocket',
                b'max_sessions': b'10',
                b'audio_formats': b'pcm,opus',
                b'description': b'HowdyTTS Test Server for ESP32-P4 Development'
            }
            
            # Create service info
            self.service_info = ServiceInfo(
                "_howdytts._tcp.local.",
                f"{self.name}._howdytts._tcp.local.",
                addresses=[b"\x00\x00\x00\x00"],  # Listen on all interfaces
                port=self.port,
                properties=properties,
                server=f"{self.name.lower()}.local."
            )
            
            # Register service
            self.zeroconf.register_service(self.service_info)
            logger.info(f"mDNS service registered: {self.name}._howdytts._tcp.local. on port {self.port}")
            
        except Exception as e:
            logger.error(f"Failed to setup mDNS: {e}")
    
    async def health_endpoint(self, request):
        """HTTP health check endpoint"""
        uptime = time.time() - self.start_time
        
        # Get system stats
        cpu_percent = psutil.cpu_percent(interval=0.1)
        memory = psutil.virtual_memory()
        
        health_data = {
            "status": "healthy",
            "version": "1.0.0-test",
            "uptime_seconds": round(uptime, 2),
            "timestamp": datetime.now().isoformat(),
            "cpu_usage": cpu_percent / 100.0,  # Convert to 0.0-1.0 range
            "memory_usage": memory.percent / 100.0,  # Convert to 0.0-1.0 range
            "active_sessions": len(self.active_sessions),
            "registered_devices": len(self.registered_devices),
            "websocket_clients": len(self.websocket_clients),
            "capabilities": ["tts", "voice", "websocket", "test_mode"],
            "audio_formats": ["pcm", "opus"],
            "max_concurrent_sessions": 10
        }
        
        logger.info(f"Health check requested - CPU: {cpu_percent:.1f}%, Memory: {memory.percent:.1f}%")
        return web.json_response(health_data)
    
    async def config_endpoint(self, request):
        """HTTP configuration endpoint"""
        config_data = {
            "server_name": self.name,
            "version": "1.0.0-test",
            "features": {
                "text_to_speech": True,
                "voice_recognition": True,
                "websocket_streaming": True,
                "multi_session": True,
                "device_registration": True
            },
            "audio_config": {
                "sample_rate": 16000,
                "channels": 1,
                "format": "pcm_s16le",
                "frame_size": 320
            },
            "websocket_config": {
                "endpoint": "/howdytts",
                "protocol": "howdytts-v1",
                "keepalive_interval": 30,
                "max_message_size": 65536
            },
            "limits": {
                "max_sessions": 10,
                "max_audio_duration": 30,
                "max_text_length": 1000
            }
        }
        
        logger.info("Configuration requested")
        return web.json_response(config_data)
    
    async def register_device_endpoint(self, request):
        """HTTP device registration endpoint"""
        try:
            data = await request.json()
            device_id = data.get('device_id')
            device_name = data.get('device_name', 'Unknown Device')
            capabilities = data.get('capabilities', '')
            device_type = data.get('device_type', 'unknown')
            
            if not device_id:
                return web.json_response(
                    {"error": "device_id is required"}, 
                    status=400
                )
            
            # Register device
            self.registered_devices[device_id] = {
                "device_id": device_id,
                "device_name": device_name,
                "device_type": device_type,
                "capabilities": capabilities.split(',') if capabilities else [],
                "registered_at": datetime.now().isoformat(),
                "last_seen": datetime.now().isoformat(),
                "status": "online"
            }
            
            response_data = {
                "status": "registered",
                "device_id": device_id,
                "session_id": f"session_{int(time.time())}_{len(self.registered_devices)}",
                "server_capabilities": ["tts", "voice", "websocket"],
                "websocket_url": f"ws://localhost:{self.port}/howdytts",
                "keepalive_interval": 30
            }
            
            logger.info(f"Device registered: {device_id} ({device_name}) - Capabilities: {capabilities}")
            return web.json_response(response_data)
            
        except Exception as e:
            logger.error(f"Device registration failed: {e}")
            return web.json_response(
                {"error": "Registration failed", "details": str(e)}, 
                status=500
            )
    
    async def list_devices_endpoint(self, request):
        """List registered devices"""
        return web.json_response({
            "devices": list(self.registered_devices.values()),
            "total_count": len(self.registered_devices)
        })
    
    async def list_sessions_endpoint(self, request):
        """List active sessions"""
        return web.json_response({
            "sessions": self.active_sessions,
            "total_count": len(self.active_sessions)
        })
    
    async def websocket_endpoint(self, request):
        """WebSocket endpoint for real-time communication"""
        ws = web.WebSocketResponse(protocols=['howdytts-v1'])
        await ws.prepare(request)
        
        client_info = {
            "remote": request.remote,
            "connected_at": datetime.now().isoformat(),
            "session_id": f"ws_session_{int(time.time())}_{len(self.websocket_clients)}"
        }
        
        self.websocket_clients.append(ws)
        logger.info(f"WebSocket client connected: {client_info['remote']} (Session: {client_info['session_id']})")
        
        # Send welcome message
        welcome_msg = {
            "type": "welcome",
            "session_id": client_info['session_id'],
            "server": self.name,
            "version": "1.0.0-test",
            "capabilities": ["tts", "voice", "echo_test"],
            "timestamp": datetime.now().isoformat()
        }
        await ws.send_str(json.dumps(welcome_msg))
        
        try:
            async for msg in ws:
                if msg.type == WSMsgType.TEXT:
                    try:
                        data = json.loads(msg.data)
                        await self.handle_websocket_message(ws, data, client_info)
                    except json.JSONDecodeError:
                        error_msg = {
                            "type": "error",
                            "error": "Invalid JSON format",
                            "timestamp": datetime.now().isoformat()
                        }
                        await ws.send_str(json.dumps(error_msg))
                elif msg.type == WSMsgType.BINARY:
                    # Handle binary audio data
                    await self.handle_audio_data(ws, msg.data, client_info)
                elif msg.type == WSMsgType.ERROR:
                    logger.error(f'WebSocket error: {ws.exception()}')
                    break
        
        except Exception as e:
            logger.error(f"WebSocket error: {e}")
        
        finally:
            if ws in self.websocket_clients:
                self.websocket_clients.remove(ws)
            logger.info(f"WebSocket client disconnected: {client_info['remote']}")
        
        return ws
    
    async def handle_websocket_message(self, ws: WebSocketResponse, data: dict, client_info: dict):
        """Handle WebSocket JSON messages"""
        msg_type = data.get('type')
        
        if msg_type == 'ping':
            # Respond to ping
            pong_msg = {
                "type": "pong",
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(pong_msg))
            
        elif msg_type == 'text_to_speech':
            # Handle TTS request
            text = data.get('text', '')
            voice = data.get('voice', 'default')
            
            logger.info(f"TTS request: '{text}' (voice: {voice})")
            
            # Simulate TTS processing
            await asyncio.sleep(0.5)  # Simulate processing delay
            
            tts_response = {
                "type": "tts_response",
                "text": text,
                "voice": voice,
                "audio_format": "pcm_s16le",
                "sample_rate": 16000,
                "duration_ms": len(text) * 50,  # Rough estimate
                "status": "completed",
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(tts_response))
            
            # Send mock audio data (silence)
            mock_audio = b'\x00' * 1024  # 1KB of silence
            await ws.send_bytes(mock_audio)
            
        elif msg_type == 'voice_start':
            # Voice recognition session started
            logger.info("Voice recognition session started")
            response = {
                "type": "voice_ready",
                "session_id": client_info['session_id'],
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(response))
            
        elif msg_type == 'voice_end':
            # Voice recognition session ended
            logger.info("Voice recognition session ended")
            
            # Simulate voice recognition result
            mock_text = data.get('mock_result', "Hello, this is a test response from the HowdyTTS test server.")
            
            response = {
                "type": "voice_result",
                "text": mock_text,
                "confidence": 0.95,
                "session_id": client_info['session_id'],
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(response))
            
        elif msg_type == 'echo_test':
            # Echo test for debugging
            echo_response = {
                "type": "echo_response",
                "original_message": data,
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(echo_response))
            
        else:
            # Unknown message type
            error_msg = {
                "type": "error",
                "error": f"Unknown message type: {msg_type}",
                "timestamp": datetime.now().isoformat()
            }
            await ws.send_str(json.dumps(error_msg))
    
    async def handle_audio_data(self, ws: WebSocketResponse, audio_data: bytes, client_info: dict):
        """Handle binary audio data"""
        logger.info(f"Received {len(audio_data)} bytes of audio data from {client_info['remote']}")
        
        # Simulate audio processing
        await asyncio.sleep(0.01)  # Simulate processing delay
        
        # Send acknowledgment
        ack_msg = {
            "type": "audio_received",
            "bytes_received": len(audio_data),
            "session_id": client_info['session_id'],
            "timestamp": datetime.now().isoformat()
        }
        await ws.send_str(json.dumps(ack_msg))
    
    async def start(self):
        """Start the test server"""
        logger.info(f"Starting HowdyTTS Test Server: {self.name}")
        
        # Setup mDNS first
        self.setup_mdns()
        
        # Start HTTP server
        await self.setup_http_server()
        
        logger.info(f"🎉 HowdyTTS Test Server ready!")
        logger.info(f"   📍 HTTP Server: http://0.0.0.0:{self.port}")
        logger.info(f"   🔍 mDNS Service: {self.name}._howdytts._tcp.local.")
        logger.info(f"   🔌 WebSocket: ws://localhost:{self.port}/howdytts")
        logger.info(f"   🏥 Health Check: http://localhost:{self.port}/health")
        logger.info(f"   ⚙️  Configuration: http://localhost:{self.port}/config")
        logger.info(f"")
        logger.info(f"Ready for ESP32-P4 HowdyScreen connections!")
    
    async def stop(self):
        """Stop the test server"""
        logger.info("Stopping HowdyTTS Test Server...")
        
        # Close WebSocket connections
        for ws in self.websocket_clients[:]:
            await ws.close()
        
        # Stop HTTP server
        if self.site:
            await self.site.stop()
        if self.runner:
            await self.runner.cleanup()
        
        # Unregister mDNS service
        if self.zeroconf and self.service_info:
            self.zeroconf.unregister_service(self.service_info)
            self.zeroconf.close()
        
        logger.info("HowdyTTS Test Server stopped")

async def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='HowdyTTS Test Server for ESP32-P4 Development')
    parser.add_argument('-p', '--port', type=int, default=8080, help='Server port (default: 8080)')
    parser.add_argument('-n', '--name', default='Test-HowdyTTS', help='Server name for mDNS (default: Test-HowdyTTS)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose logging')
    
    args = parser.parse_args()
    
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Create and start server
    server = HowdyTTSTestServer(port=args.port, name=args.name)
    
    # Setup signal handlers
    def signal_handler(signum, frame):
        logger.info("Received interrupt signal, shutting down...")
        asyncio.create_task(server.stop())
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    try:
        await server.start()
        
        # Keep server running
        while True:
            await asyncio.sleep(1)
            
    except KeyboardInterrupt:
        await server.stop()
    except Exception as e:
        logger.error(f"Server error: {e}")
        await server.stop()

if __name__ == '__main__':
    asyncio.run(main())