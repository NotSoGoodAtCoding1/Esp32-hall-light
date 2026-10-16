# ESP32 LED Controller (LED-IR)

ESP32-based project for controlling WS2812 RGB LED strips with PIR motion sensors and WiFi web configuration.

## Hardware Requirements

- **Microcontroller**: ESP32 dev board
- **LED Strip**: WS2812/WS2812B RGB (60 pixels)
- **PIR Sensors**: HC-SR501 x2

### Pin Configuration

| Function | GPIO Pin | Description |
|----------|----------|-------------|
| LED Strip Data | 5 | Main signal to WS2812 strip |
| PIR Sensor 1 | 33 | Segment 1 (pixels 0-29) |
| PIR Sensor 2 | 34 | Segment 2 (pixels 30-59) |

## Dependencies

This project uses PlatformIO for dependency management. The required libraries are defined in `platformio.ini`:

```ini
lib_deps = 
    adafruit/Adafruit NeoPixel@^1.15.5
    ArduinoJson@^7.0
```

### Python Requirements (Development)

For testing and development, the following Python dependencies may be needed:

See [requirements.txt](./requirements.txt) for a list of Python-compatible packages.

## Installation

1. Install PlatformIO IDE or use CLI tools
2. Open this project in PlatformIO
3. Build and upload using the IDE or run `pio run --target upload`

## Features

- **Dual-segment LED control**: Each PIR sensor controls its own 30-pixel segment
- **Motion-based lighting**: LEDs activate only when motion is detected
- **Web configuration interface**: Adjust brightness, colors, and timeout via browser
- **WiFi fallback AP**: Creates access point if WiFi connection fails
- **Configurable settings**: Persistent storage using EEPROM

## Configuration Options (Web Interface)

- Brightness per segment
- RGB color for each segment
- Motion timeout duration
- WiFi credentials or Access Point mode
