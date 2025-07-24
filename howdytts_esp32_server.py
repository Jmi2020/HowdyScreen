#!/usr/bin/env python3
"""
HowdyTTS ESP32P4 Integration Server
Complete example showing how to integrate ESP32P4 with HowdyTTS
"""

import socket
import struct
import numpy as np
import time
import queue
from threading import Thread, Event
import whisper
import logging

# mDNS support for automatic discovery
try:
    from zeroconf import ServiceInfo, Zeroconf
    MDNS_AVAILABLE = True
except ImportError:
    MDNS_AVAILABLE = False
    print("Warning: zeroconf not installed. Install with: pip install zeroconf")
    print("mDNS discovery will be disabled.")

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class ESP32AudioServer:
    """UDP server for ESP32P4 audio communication"""
    
    def __init__(self, host='0.0.0.0', port=8002):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((self.host, self.port))
        self.sock.settimeout(0.1)  # Non-blocking with timeout
        
        self.client_address = None
        self.audio_queue = queue.Queue()
        self.running = Event()
        self.running.set()
        
        logger.info(f"ESP32 Audio Server listening on {host}:{port}")
        
    def receive_audio(self):
        """Receive audio packets from ESP32P4"""
        while self.running.is_set():
            try:
                data, addr = self.sock.recvfrom(256)  # MAX_PACKET_SIZE
                
                # Save client address for response
                if self.client_address != addr:
                    self.client_address = addr
                    logger.info(f"ESP32P4 connected from {addr}")
                
                # Parse packet header
                if len(data) >= 8:
                    timestamp, sequence, data_size = struct.unpack('<IHH', data[:8])
                    audio_data = data[8:8+data_size]
                    
                    # Convert to numpy array
                    audio_samples = np.frombuffer(audio_data, dtype=np.int16)
                    self.audio_queue.put(audio_samples)
                    
            except socket.timeout:
                continue
            except Exception as e:
                logger.error(f"Error receiving audio: {e}")
                
    def send_audio(self, audio_samples):
        """Send TTS audio back to ESP32P4"""
        if self.client_address is None:
            logger.warning("No ESP32P4 client connected")
            return
            
        # Convert to int16 if needed
        if audio_samples.dtype != np.int16:
            audio_samples = (audio_samples * 32767).astype(np.int16)
        
        # Send in chunks of 320 samples (20ms)
        chunk_size = 320
        sequence = 0
        
        logger.info(f"Sending {len(audio_samples)} samples to ESP32P4")
        
        for i in range(0, len(audio_samples), chunk_size):
            chunk = audio_samples[i:i+chunk_size]
            
            # Pad last chunk if needed
            if len(chunk) < chunk_size:
                chunk = np.pad(chunk, (0, chunk_size - len(chunk)), mode='constant')
            
            # Create packet
            timestamp = int(time.time() * 1000)  # milliseconds
            header = struct.pack('<IHH', timestamp, sequence, len(chunk) * 2)
            packet = header + chunk.tobytes()
            
            # Send to ESP32P4
            self.sock.sendto(packet, self.client_address)
            sequence += 1
            
            # Small delay to avoid overwhelming ESP32
            time.sleep(0.018)  # Slightly less than 20ms
            
    def stop(self):
        """Stop the server"""
        self.running.clear()
        self.sock.close()

class HowdyTTSESP32Integration:
    """Main integration class for HowdyTTS with ESP32P4"""
    
    def __init__(self, whisper_model_name="base", enable_mdns=True):
        self.audio_server = ESP32AudioServer()
        self.whisper_model = whisper.load_model(whisper_model_name)
        
        # Audio processing parameters
        self.sample_rate = 16000
        self.silence_threshold_ms = 500
        self.min_audio_length = 0.5  # seconds
        
        # mDNS advertisement
        self.mdns_enabled = enable_mdns and MDNS_AVAILABLE
        self.zeroconf = None
        self.service_info = None
        
        if self.mdns_enabled:
            self._start_mdns_advertising()
        
        # Start audio receiver thread
        self.receiver_thread = Thread(target=self.audio_server.receive_audio, daemon=True)
        self.receiver_thread.start()
        
        logger.info("HowdyTTS ESP32 Integration initialized")
    
    def _start_mdns_advertising(self):
        """Start mDNS service advertising"""
        try:
            self.zeroconf = Zeroconf()
            
            # Get local IP
            local_ip = self._get_local_ip()
            
            # Create service info
            self.service_info = ServiceInfo(
                "_howdytts._udp.local.",
                "HowdyTTS Server._howdytts._udp.local.",
                addresses=[socket.inet_aton(local_ip)],
                port=self.audio_server.port,
                properties={
                    'version': '1.0',
                    'platform': 'python',
                    'description': 'HowdyTTS ESP32P4 Audio Server',
                    'audio_format': '16kHz_16bit_mono'
                }
            )
            
            # Register service
            self.zeroconf.register_service(self.service_info)
            logger.info(f"mDNS: Advertising HowdyTTS service on {local_ip}:{self.audio_server.port}")
            
        except Exception as e:
            logger.error(f"Failed to start mDNS advertising: {e}")
            self.mdns_enabled = False
    
    def _get_local_ip(self):
        """Get local IP address"""
        try:
            # Create a dummy socket to get local IP
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            local_ip = s.getsockname()[0]
            s.close()
            return local_ip
        except Exception:
            return "127.0.0.1"
    
    def _stop_mdns_advertising(self):
        """Stop mDNS service advertising"""
        if self.zeroconf and self.service_info:
            try:
                self.zeroconf.unregister_service(self.service_info)
                self.zeroconf.close()
                logger.info("mDNS: Service advertisement stopped")
            except Exception as e:
                logger.error(f"Error stopping mDNS: {e}")
        
    def process_query(self, text):
        """
        Process the transcribed text with your HowdyTTS logic
        Replace this with your actual HowdyTTS processing
        """
        # Example: Simple echo response
        return f"You said: {text}"
        
    def text_to_speech(self, text):
        """
        Convert text to speech audio
        Replace this with your actual TTS implementation
        """
        # Example: Generate a beep for now
        # In production, use your TTS engine here
        frequency = 440  # Hz
        duration = 1.0   # seconds
        
        t = np.linspace(0, duration, int(self.sample_rate * duration))
        audio = np.sin(2 * np.pi * frequency * t) * 0.3
        
        return audio
        
    def process_audio_stream(self):
        """Main processing loop"""
        audio_buffer = []
        last_audio_time = time.time()
        processing = False
        
        logger.info("Starting audio processing loop")
        
        while True:
            try:
                # Get audio from queue (non-blocking)
                audio_chunk = self.audio_server.audio_queue.get(timeout=0.02)
                audio_buffer.extend(audio_chunk)
                last_audio_time = time.time()
                processing = False
                
            except queue.Empty:
                # Check if we have enough silence after speech
                silence_duration = (time.time() - last_audio_time) * 1000  # ms
                
                if (audio_buffer and 
                    silence_duration > self.silence_threshold_ms and 
                    not processing):
                    
                    # Check minimum audio length
                    audio_duration = len(audio_buffer) / self.sample_rate
                    if audio_duration < self.min_audio_length:
                        audio_buffer = []
                        continue
                    
                    processing = True
                    logger.info(f"Processing {audio_duration:.1f}s of audio")
                    
                    # Convert to float32 for Whisper
                    audio_array = np.array(audio_buffer, dtype=np.float32) / 32768.0
                    
                    try:
                        # 1. Speech to text
                        result = self.whisper_model.transcribe(
                            audio_array, 
                            fp16=False,
                            language='en'
                        )
                        text = result["text"].strip()
                        
                        if text:
                            logger.info(f"Transcribed: {text}")
                            
                            # 2. Process with HowdyTTS logic
                            response_text = self.process_query(text)
                            logger.info(f"Response: {response_text}")
                            
                            # 3. Text to speech
                            audio_response = self.text_to_speech(response_text)
                            
                            # 4. Send response back to ESP32P4
                            self.audio_server.send_audio(audio_response)
                        else:
                            logger.info("No speech detected in audio")
                            
                    except Exception as e:
                        logger.error(f"Error processing audio: {e}")
                    
                    # Clear buffer for next query
                    audio_buffer = []
                    processing = False
                    
            except KeyboardInterrupt:
                logger.info("Shutting down...")
                break
            except Exception as e:
                logger.error(f"Unexpected error: {e}")
                
    def run(self):
        """Start the server"""
        try:
            self.process_audio_stream()
        finally:
            self._stop_mdns_advertising()
            self.audio_server.stop()

def main():
    """Main entry point"""
    # Create and run the integration server
    server = HowdyTTSESP32Integration(whisper_model_name="base", enable_mdns=True)
    
    print("\n" + "="*50)
    print("HowdyTTS ESP32P4 Server Running")
    print("="*50)
    print(f"Listening on UDP port 8002")
    if server.mdns_enabled:
        print(f"mDNS: Advertising as 'HowdyTTS Server' for automatic discovery")
    else:
        print(f"mDNS: Disabled (install zeroconf library to enable)")
    print(f"Waiting for ESP32P4 connection...")
    
    # Display setup instructions
    print("\n" + "-"*50)
    print("ESP32P4 Setup Instructions:")
    print("1. Connect to 'HowdyScreen-Setup' WiFi (password: configure)")
    print("2. Go to http://192.168.4.1 to configure your WiFi")
    print("3. ESP32P4 will automatically discover this server")
    print("-"*50)
    
    print("\nPress Ctrl+C to stop")
    print("="*50 + "\n")
    
    server.run()

if __name__ == "__main__":
    main()