#!/usr/bin/env python3
"""
HowdyTTS mDNS Server Advertisement
Advertises the HowdyTTS server so ESP32P4 can discover it automatically
"""

import socket
import time
import logging
from zeroconf import ServiceInfo, Zeroconf

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class HowdyTTSMDNSAdvertiser:
    """Advertise HowdyTTS server via mDNS/Bonjour"""
    
    def __init__(self, port=8002):
        self.port = port
        self.zeroconf = None
        self.service_info = None
        
    def get_local_ip(self):
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
            
    def start(self):
        """Start mDNS advertisement"""
        self.zeroconf = Zeroconf()
        
        local_ip = self.get_local_ip()
        
        # Create service info
        self.service_info = ServiceInfo(
            "_howdytts._udp.local.",
            "HowdyTTS Server._howdytts._udp.local.",
            addresses=[socket.inet_aton(local_ip)],
            port=self.port,
            properties={
                'version': '1.0',
                'platform': 'python',
                'description': 'HowdyTTS ESP32P4 Audio Server'
            }
        )
        
        # Register service
        self.zeroconf.register_service(self.service_info)
        logger.info(f"mDNS: Advertising HowdyTTS service on {local_ip}:{self.port}")
        
    def stop(self):
        """Stop mDNS advertisement"""
        if self.zeroconf and self.service_info:
            self.zeroconf.unregister_service(self.service_info)
            self.zeroconf.close()
            logger.info("mDNS: Service advertisement stopped")

# Example usage - add this to your HowdyTTS server
def integrate_with_howdytts_server():
    """
    Example of how to integrate mDNS with your existing server
    """
    # Import your existing server
    from howdytts_esp32_server import HowdyTTSESP32Integration
    
    # Create mDNS advertiser
    mdns = HowdyTTSMDNSAdvertiser(port=8002)
    
    try:
        # Start advertising
        mdns.start()
        
        # Run your server
        server = HowdyTTSESP32Integration()
        server.run()
        
    finally:
        # Clean up
        mdns.stop()

if __name__ == "__main__":
    # Standalone test
    mdns = HowdyTTSMDNSAdvertiser(port=8002)
    
    try:
        mdns.start()
        print("\nHowdyTTS mDNS Advertisement Running")
        print("ESP32P4 devices can now discover this server automatically")
        print("Press Ctrl+C to stop\n")
        
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        mdns.stop()