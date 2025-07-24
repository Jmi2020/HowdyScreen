# WiFi Provisioning Guide for ESP32P4 HowdyScreen

This guide explains how to configure WiFi credentials on your ESP32P4 HowdyScreen device without hardcoding them.

## How It Works

1. **First Boot**: ESP32P4 creates a WiFi access point called "HowdyScreen-Setup"
2. **Connect**: Your Mac connects to this AP and visits a web interface
3. **Configure**: You select your WiFi network and enter credentials
4. **Save**: Configuration is saved to ESP32P4's flash memory
5. **Connect**: ESP32P4 connects to your WiFi and discovers HowdyTTS server

## Setup Process

### Step 1: Power On ESP32P4
When the ESP32P4 boots for the first time (or after a reset), it will:
- Display "WiFi Setup Mode" on the screen
- Create WiFi access point: `HowdyScreen-Setup`
- Password: `configure`

### Step 2: Connect Your Mac
1. Open WiFi settings on your Mac
2. Connect to network: `HowdyScreen-Setup`
3. Enter password: `configure`

### Step 3: Configure WiFi
1. Open browser and go to: `http://192.168.4.1`
2. You'll see the HowdyScreen setup page
3. Select your home WiFi network from the dropdown
4. Enter your WiFi password
5. (Optional) Enter your Mac Studio's MAC address for device verification
6. Click "Connect"

### Step 4: Automatic Connection
The ESP32P4 will:
- Save your WiFi credentials
- Connect to your home network
- Search for HowdyTTS server via mDNS
- Fall back to trying known IP addresses
- Display connection status on screen

## Finding Your Mac Studio's MAC Address

If you want the ESP32P4 to verify it's on the same network as your Mac Studio:

### Method 1: System Information
```bash
ifconfig en0 | grep ether
```

### Method 2: System Preferences
1. Apple Menu → About This Mac → System Report
2. Network → WiFi → Hardware MAC Address

### Method 3: Network Utility
1. Applications → Utilities → Network Utility
2. Info tab → Select WiFi interface

The MAC address looks like: `aa:bb:cc:dd:ee:ff`

## Resetting WiFi Configuration

If you need to reconfigure WiFi:

### Method 1: Hardware Reset
- Hold the center button on ESP32P4 for 10 seconds during boot

### Method 2: Web Interface (if connected)
- Visit `http://[esp32-ip]/reset` in browser

### Method 3: Serial Console
```bash
# Connect via USB-C and send command
AT+RESET_WIFI
```

## Troubleshooting

### ESP32P4 doesn't create setup AP
- Check power supply (needs 5V, 2A minimum)
- Ensure it's first boot or configuration was reset
- Look for "Setup Mode" message on display

### Can't connect to HowdyScreen-Setup
- Verify password: `configure`
- Try forgetting and reconnecting to the network
- Check if your Mac has internet access requirements (disable)

### Configuration page won't load
- Ensure you're connected to `HowdyScreen-Setup`
- Try `http://192.168.4.1` in different browser
- Clear browser cache and cookies

### ESP32P4 won't connect to home WiFi
- Verify WiFi password is correct
- Check if network uses 5GHz only (ESP32 needs 2.4GHz)
- Ensure network doesn't have MAC filtering enabled
- Try moving ESP32P4 closer to router

### Can't find HowdyTTS server
- Ensure server is running with mDNS enabled
- Check firewall settings on Mac Studio
- Verify both devices are on same network subnet
- Try running server discovery test

## Advanced Configuration

### Custom Fallback IPs
Edit `main.c` to add your specific IP addresses:
```c
static const char *FALLBACK_SERVERS[] = {
    "192.168.1.50",   // Your Mac Studio IP
    "10.0.1.50",      // Alternative network
    "192.168.0.50",   // Guest network
};
```

### Captive Portal Detection
Some networks require web authentication. The ESP32P4 will:
- Detect captive portals
- Display instructions on screen
- Provide fallback connection options

### Enterprise WiFi
For WPA2-Enterprise networks, you'll need to modify the provisioning code to support:
- Username/password authentication
- Certificate-based authentication
- PEAP/EAP protocols

## Security Notes

- Credentials are stored encrypted in ESP32P4's secure flash
- Setup AP is only active during initial configuration
- Web interface uses HTTPS when possible
- MAC address verification adds extra security layer

## Integration with HowdyTTS

Once WiFi is configured, ESP32P4 automatically:
1. Searches for `_howdytts._udp.local` via mDNS
2. Tests connection to discovered servers
3. Falls back to hardcoded IP list if needed
4. Displays connection status on screen
5. Begins audio streaming when server found

Your HowdyTTS server should advertise itself:
```python
from howdytts_mdns_server import HowdyTTSMDNSAdvertiser
mdns = HowdyTTSMDNSAdvertiser(port=8002)
mdns.start()
```