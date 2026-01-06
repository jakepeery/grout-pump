# ESP32 Grout Pump Control System

## Overview
This Arduino sketch for the **Freenove ESP32-WROOM development board** controls a hydraulic valve system with two Solid State Relays (SSRs). It supports both manual control and automatic cycling mode with end-stop detection.

**NEW Features:**
- 🌐 **Web Interface** - Configure settings via browser
- 📡 **OTA Updates** - Upload firmware wirelessly
- 💾 **WiFi Credential Storage** - Stored in ESP32 flash memory
- ⏱️ **Safety Timeouts** - Configurable cycle timeout protection

## Hardware Requirements
- **Freenove ESP32-WROOM-32 Development Board**
- 2 SSRs (Solid State Relays) for controlling the hydraulic valve
- Wireless remote control with 4 momentary buttons
- 2 end-stop sensors (limit switches)
- WiFi network (for web interface and OTA updates)

## Pin Configuration

### Outputs (GPO)
| Pin | Function | Description |
|-----|----------|-------------|
| GPIO 25 | GPO1 | SSR #1 control output |
| GPIO 26 | GPO2 | SSR #2 control output |

### Inputs (GPI) - Remote Control
| Pin | Function | Description |
|-----|----------|-------------|
| GPIO 32 | Input A | Manual control for GPO1 (extend valve) |
| GPIO 33 | Input B | Manual control for GPO2 (retract valve) |
| GPIO 34 | Input C | Start automatic loop mode |
| GPIO 35 | Input D | Stop automatic loop mode |

### Inputs (GPI) - End Stops
| Pin | Function | Description |
|-----|----------|-------------|
| GPIO 36 | End Stop IN | Detects fully retracted position |
| GPIO 39 | End Stop OUT | Detects fully extended position |

**Note:** All inputs use internal pull-up resistors and expect active-low signals (pressed = LOW, released = HIGH).

## Freenove ESP32-WROOM Board Notes

The Freenove ESP32-WROOM-32 board features:
- ESP32-WROOM-32 module (dual-core processor)
- USB-to-Serial converter for programming
- Built-in LED on GPIO 2
- 3.3V and 5V power pins available
- All GPIOs are 3.3V logic level

### Important Pin Information
- **GPIO 34-39** are input-only pins (no internal pull-up/down, ADC capable)
- **GPIO 6-11** are connected to internal flash - DO NOT USE
- **GPIO 0** is used for boot mode selection - avoid unless necessary
- **GPIO 2** has built-in LED - can be used but LED will flash
- Safe output pins: 4, 5, 12-19, 21-23, 25-27, 32-33
- Safe input pins (with pull-up): 4, 5, 12-15, 18-19, 21-23, 25-27, 32-33
- Input-only pins (external pull-up required): 34-39

### Pin Selection Rationale
The pins chosen in this project follow these guidelines:
- **GPIO 25, 26**: Output pins for SSRs (safe, no special functions)
- **GPIO 32, 33**: Remote inputs A & B (support internal pull-ups)
- **GPIO 34, 35**: Remote inputs C & D (input-only, need external pull-ups if not provided by remote)
- **GPIO 36, 39**: End-stop sensors (input-only, VP and VN ADC pins)

## Operation Modes

### Manual Mode (Default)
In manual mode, the remote control buttons directly control the outputs:
- **Button A pressed:** GPO1 turns ON (SSR #1 activates - extend)
- **Button A released:** GPO1 turns OFF
- **Button B pressed:** GPO2 turns ON (SSR #2 activates - retract)
- **Button B released:** GPO2 turns OFF
- **Both buttons pressed:** Both outputs turn OFF (safety feature to prevent simultaneous activation)

**Safety Note:** If both buttons A and B are pressed simultaneously, the system turns off both outputs to prevent potential damage to the hydraulic valve from bidirectional force.

### Automatic Loop Mode
Press **Button C** to enter automatic loop mode. The system will:
1. Start by extending (GPO2 ON)
2. When the OUT end-stop is reached, reverse direction (GPO1 ON)
3. When the IN end-stop is reached, reverse direction again (GPO2 ON)
4. Continue cycling between IN and OUT positions

Press **Button D** to exit automatic loop mode and return to manual control.

## Web Interface

### Initial Setup
On first boot (or when WiFi is not configured):
1. The device starts in **Access Point (AP) mode**
2. SSID: `GroutPump-Setup`
3. Password: `12345678`
4. Connect to this network and navigate to: `http://192.168.4.1`
5. Configure your WiFi credentials in the settings page
6. Device will restart and connect to your WiFi network

### Accessing the Web Interface
Once connected to WiFi, access the device via:
- **mDNS:** `http://groutpump.local` (recommended)
- **IP Address:** Check serial output or your router's DHCP list

### Web Interface Features
- **Home Page** - View current status, mode, and output states
- **Settings Page** - Configure:
  - WiFi credentials (SSID and password)
  - Cycle timeout (in milliseconds)
  - Enable/disable timeout protection
- **Status API** - JSON endpoint at `/status` for integration
- **Real-time Monitoring** - Refresh page to see live status

## OTA (Over-The-Air) Updates

### Configuration
- **Hostname:** `groutpump`
- **Password:** `groutpump123` (change in code for security)

### Using Arduino IDE
1. Connect to the same WiFi network as the ESP32
2. Tools → Port → Select "groutpump at [IP address]"
3. Upload sketch normally

### Using PlatformIO
```bash
# Upload via OTA
pio run --target upload --upload-port groutpump.local
```

Or add to `platformio.ini`:
```ini
upload_protocol = espota
upload_port = groutpump.local
upload_flags = 
    --auth=groutpump123
```

## Safety Features
- **Debouncing:** All inputs are debounced (50ms) to prevent false triggers
- **Cycle Delay:** 500ms delay between direction changes to prevent rapid switching and ensure outputs are never active simultaneously
- **Cycle Timeout:** Configurable timeout (default 30 seconds) stops system if end-stop not reached
- **End-stop Detection:** Automatic reversal when end-stops are triggered
- **Dual End-stop Protection:** If both end-stops trigger simultaneously (sensor malfunction), system immediately stops all outputs and returns to manual mode
- **Sequential Output Control:** When switching directions, one output is turned OFF before the other is turned ON to prevent simultaneous activation
- **Clean Shutdown:** All outputs turn OFF when exiting auto mode
- **OTA Safety:** All outputs turn OFF during firmware updates

## Configuration Storage
All settings are stored in ESP32 flash memory using the Preferences library:
- WiFi SSID and password
- Cycle timeout value
- Timeout enable/disable state

Settings persist across power cycles and firmware updates.

## Customization

You can modify these constants in the code to adjust behavior:
- `DEBOUNCE_DELAY`: Input debounce time (default: 50ms)
- `CYCLE_DELAY`: Delay between cycle direction changes (default: 500ms)

To change pin assignments, modify the pin definitions at the top of the sketch.

## Serial Monitor
The sketch outputs status information via Serial at 115200 baud:
- System initialization messages
- Pin configuration details
- Mode changes
- End-stop triggers
- Direction changes in auto mode

## Building and Uploading

### Using PlatformIO (Recommended)
This project is configured for PlatformIO. See the main [README.md](README.md) for build instructions.

### Using Arduino IDE (Alternative)
1. Copy `src/main.cpp` to a new folder named `grout-pump`
2. Rename it to `grout-pump.ino`
3. Remove the `#include <Arduino.h>` line from the top
4. Install ESP32 board support if not already installed:
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Search "ESP32" → Install
5. Select your ESP32 board: Tools → Board → ESP32 Arduino → ESP32 Dev Module
6. Select the correct port: Tools → Port → [Your COM Port]
7. Click Upload

### Using PlatformIO
1. Create a `platformio.ini` file (see example below)
2. Run: `pio run --target upload`

Example `platformio.ini`:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
```

## Wiring Diagram

```
ESP32 (Freenove WROOM)   SSR #1          Hydraulic Valve
GPIO 25 ----------------> IN+             
GND --------------------> IN-             Power Control (Extend)

ESP32 (Freenove WROOM)   SSR #2
GPIO 26 ----------------> IN+
GND --------------------> IN-             Power Control (Retract)

ESP32 (Freenove WROOM)   Remote Control
GPIO 32 <---------------- Button A (active-low with internal pull-up)
GPIO 33 <---------------- Button B (active-low with internal pull-up)
                          
                          For GPIO 34 & 35 (input-only pins):
                          Add 10kΩ pull-up resistors to 3.3V
3.3V ----[10kΩ]------+
                     |
GPIO 34 <------------+--- Button C (active-low, external pull-up required)
                     
3.3V ----[10kΩ]------+
                     |
GPIO 35 <------------+--- Button D (active-low, external pull-up required)

GND <-------------------- Common Ground for all buttons

ESP32 (Freenove WROOM)   End Stops
                          For GPIO 36 & 39 (input-only pins):
                          Add 10kΩ pull-up resistors to 3.3V
3.3V ----[10kΩ]------+
                     |
GPIO 36 <------------+--- IN Limit Switch (active-low, external pull-up required)
                     
3.3V ----[10kΩ]------+
                     |
GPIO 39 <------------+--- OUT Limit Switch (active-low, external pull-up required)

GND <-------------------- Common Ground for all sensors
```

### Pull-up Resistor Requirements
- **GPIO 32, 33**: Internal pull-ups enabled in code (10kΩ equivalent)
- **GPIO 34, 35, 36, 39**: **MUST** use external 10kΩ pull-up resistors to 3.3V
  - These are input-only pins without internal pull-up capability
  - Connect one side of resistor to 3.3V, other side to the GPIO pin
  - Switch/sensor connects between GPIO pin and GND

## License
This project is open source and available for modification and distribution.
