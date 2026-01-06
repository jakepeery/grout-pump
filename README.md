# grout-pump
ESP32 project to control oscillating a hydraulic valve using the **Freenove ESP32-WROOM development board**.

## Features
- Manual control via wireless remote (4 momentary buttons)
- Automatic cycling mode with end-stop detection
- 2 SSR outputs for hydraulic valve control
- Debounced inputs for reliable operation
- Serial debugging output
- **PlatformIO compatible**
- Optimized for Freenove ESP32-WROOM board

## Quick Start with PlatformIO

### Prerequisites
- [PlatformIO](https://platformio.org/) installed (via VS Code extension or CLI)

### Build and Upload
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Open serial monitor
pio device monitor
```

### Using VS Code
1. Open this folder in VS Code
2. Install the PlatformIO IDE extension
3. Click the PlatformIO icon in the sidebar
4. Use "Build" and "Upload" buttons

## Project Structure
```
grout-pump/
├── src/
│   └── main.cpp          - Main ESP32 application
├── platformio.ini        - PlatformIO configuration
├── HARDWARE.md          - Detailed hardware documentation
└── README.md            - This file
```

## Documentation
See [HARDWARE.md](HARDWARE.md) for:
- Complete pin configuration
- Wiring diagrams
- Operation modes
- Hardware requirements
- Customization options

## Alternative: Arduino IDE
If you prefer Arduino IDE:
1. Rename `src/main.cpp` to `grout-pump.ino`
2. Remove the `#include <Arduino.h>` line
3. Open in Arduino IDE and upload

## License
This project is open source and available for modification and distribution.
