# Quick Reference Guide

## First Time Setup

1. **Power on the ESP32**
   - Device starts in AP mode if WiFi not configured
   - Look for network: `GroutPump-Setup`
   - Password: `12345678`

2. **Configure WiFi**
   - Connect to `GroutPump-Setup`
   - Open browser: `http://192.168.4.1`
   - Click "Settings" → Enter your WiFi credentials
   - Click "Save WiFi Settings"
   - Device will restart and connect to your network

3. **Access the Device**
   - Open browser: `http://groutpump.local`
   - Or check serial monitor for IP address

## Daily Operation

### Remote Control
- **Button A (GPIO 12)** - Extend valve (manual)
- **Button B (GPIO 13)** - Retract valve (manual)
- **Button C (GPIO 14)** - Start automatic cycling
- **Button D (GPIO 15)** - Stop automatic cycling
- **Emergency Stop (GPIO 27)** - Immediate system halt (Normally Closed)

**Pin Configuration (Freenove ESP32-WROOM):**

| Component | ESP32 Pin |
|-----------|-----------|
| **Power Output 1 (SSR1)** | GPIO 25 |
| **Power Output 2 (SSR2)** | GPIO 26 |
| **Wireless Input A** | GPIO 12 |
| **Wireless Input B** | GPIO 13 |
| **Wireless Input C** | GPIO 14 |
| **Wireless Input D** | GPIO 15 |
| **End-Stop IN** | GPIO 32 |
| **End-Stop OUT** | GPIO 33 |
| **Emergency Stop (NC)** | GPIO 27 |

### Web Interface (NEW v2.0)
The web interface now features a modern, responsive design with:
- **Auto-Refresh** - Toggle real-time status updates every 2 seconds
- **Animated Indicators** - Visual feedback for GPO states
- **Live Updates** - Status updates without page reload
- **Home** - View current status with live updates
- **Settings** - Configure timing and WiFi
- **Status** - JSON API endpoint

## Web Interface Settings

### Timing Configuration
- **Cycle Timeout** - Maximum time (ms) for valve to reach end-stop
  - Default: 30000ms (30 seconds)
  - Range: 1000ms - 300000ms
  - If timeout occurs, system stops and returns to manual mode
- **Enable Timeout Protection** - Checkbox to enable/disable timeout

### WiFi Configuration
- **SSID** - Your WiFi network name
- **Password** - Your WiFi password
- Note: Device restarts after saving WiFi settings

## OTA Firmware Updates

### Using PlatformIO
```bash
# One-time setup - add to platformio.ini
upload_protocol = espota
upload_port = groutpump.local
upload_flags = --auth=groutpump123

# Upload firmware
pio run --target upload
```

### Using Arduino IDE
1. Tools → Port → Select "groutpump at [IP address]"
2. Click Upload
3. Enter password: `groutpump123`

## System Status

### LED Indicators
- Built-in LED (GPIO 2) may flash during WiFi operations

### Serial Monitor (115200 baud)
- System initialization messages
- WiFi connection status
- IP address
- Mode changes
- Error messages
- End-stop triggers

## Safety Features

### Automatic Shutdowns
System automatically stops and returns to manual mode if:
- **Emergency Stop (E-Stop) activated**
- Both end-stops trigger simultaneously (sensor fault)
- Cycle timeout exceeded (valve stuck or sensor fault)

### During Auto Mode
- 500ms delay between direction changes
- Only one output active at a time
- Outputs turn off during delays

## Troubleshooting

### Can't Connect to Web Interface
1. Check serial monitor for IP address
2. Try `http://groutpump.local` instead of IP
3. Ensure device and computer are on same network
4. If AP mode, ensure connected to `GroutPump-Setup`

### Device Won't Connect to WiFi
1. Check SSID and password in settings
2. Ensure WiFi is 2.4GHz (ESP32 doesn't support 5GHz)
3. Check serial monitor for error messages
4. Reset WiFi credentials via AP mode

### Timeout Errors
1. Check end-stop sensor connections
2. Verify external pull-up resistors on GPIO 34, 35, 36, 39
3. Increase timeout value in web settings
4. Test sensors manually in manual mode

### OTA Update Fails
1. Ensure device is on same network
2. Check OTA password: `groutpump123`
3. Try IP address instead of hostname
4. Ensure no firewall blocking connection

## Default Credentials

| Item | Value |
|------|-------|
| AP SSID | GroutPump-Setup |
| AP Password | 12345678 |
| mDNS Hostname | groutpump.local |
| OTA Hostname | groutpump |
| OTA Password | groutpump123 |
| Serial Baud Rate | 115200 |

## Pin Reference

| Function | GPIO | Notes |
|----------|------|-------|
| SSR 1 (Extend) | 25 | Output |
| SSR 2 (Retract) | 26 | Output |
| Button A (Extend) | 32 | Input, internal pull-up ✅ |
| Button B (Retract) | 33 | Input, internal pull-up ✅ |
| Button C (Auto) | 12 | Input, internal pull-up ✅ |
| Button D (Stop) | 13 | Input, internal pull-up ✅ |
| End-stop IN | 14 | Input, internal pull-up ✅ |
| End-stop OUT | 15 | Input, internal pull-up ✅ |

**✨ All inputs now support internal pull-ups - no external resistors needed!**

## API Reference

### GET /status
Returns JSON with system status:
```json
{
  "mode": "MANUAL",
  "cycleDirection": "STOPPED",
  "gpo1": 0,
  "gpo2": 0,
  "endStopIn": false,
  "endStopOut": false,
  "cycleTimeout": 30000,
  "timeoutEnabled": true,
  "wifiConnected": true,
  "ipAddress": "192.168.1.100"
}
```

### POST /save
Save timing settings:
- `timeout` - Cycle timeout in milliseconds
- `timeoutEnabled` - Checkbox value

### POST /setwifi
Save WiFi credentials (device restarts):
- `ssid` - WiFi network name
- `password` - WiFi password
