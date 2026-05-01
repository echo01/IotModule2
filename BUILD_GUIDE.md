# VIOT Build & Deployment Guide

## Quick Start

### 1. Prerequisites
- PlatformIO (https://platformio.org/)
- Python 3.6+
- ESP32 board connected via USB

### 2. Build
```bash
cd iotmodule2
platformio run
```

### 3. Upload
```bash
platformio run --target upload
```

### 4. Monitor
```bash
platformio device monitor --baud 115200
```

## Detailed Setup

### Windows Setup

#### Step 1: Install PlatformIO

**Option A: VS Code Extension (Recommended)**
1. Install Visual Studio Code
2. Open Extensions (Ctrl+Shift+X)
3. Search for "PlatformIO IDE"
4. Click Install
5. Reload VS Code

**Option B: Command Line**
```bash
pip install platformio
pio --version
```

#### Step 2: Prepare Project
```bash
git clone <repo> iotmodule2
cd iotmodule2
cd iotmodule2
```

#### Step 3: Build Project
```bash
pio run
```

First build may take 3-5 minutes (compiling libraries)

#### Step 4: Upload to Board
```bash
# Find COM port
pio device list

# Upload (auto-detects port)
pio run --target upload

# Or specify port explicitly
pio run --target upload --upload-port COM3
```

**Wait for "Upload successful" message**

#### Step 5: Monitor Serial
```bash
pio device monitor --baud 115200
```

Press Ctrl+Shift+Q to quit monitor

### Linux/Mac Setup

Same commands, but use `/dev/ttyUSB0` or `/dev/cu.SLAB_USBtoUART` for port names.

```bash
# Linux
pio run --target upload --upload-port /dev/ttyUSB0
pio device monitor --upload-port /dev/ttyUSB0 --baud 115200

# Mac
pio run --target upload --upload-port /dev/cu.SLAB_USBtoUART
pio device monitor --upload-port /dev/cu.SLAB_USBtoUART --baud 115200
```

## Configuration After Upload

### Initial Boot

1. **Power on the ESP32**
   - Status LED (GPIO32) should light up
   - Check serial monitor for startup messages

2. **First Boot Behavior**
   - If `config.json` missing → uses default config
   - AP mode enabled: Connect to `VIOT_XXYY` WiFi
   - Password: `12345678`

3. **Access Web Interface**
   - Open browser: `http://192.168.4.1`
   - Configure WiFi SSID/password
   - Configure MQTT broker details
   - Click Save

### Configure via Serial (Debug Mode)

1. **Enable Debug Mode**
   - Set GPIO27 to HIGH (connect to 3.3V or button)
   - Reboot device

2. **Watch Serial Monitor**
   ```
   [INFO] DEBUG MODE ENABLED (GPIO27 = HIGH)
   [INFO] WiFi Handler initializing...
   [INFO] MQTT Handler initialized...
   [INFO] Setup Complete
   ```

3. **Test Sensor**
   ```
   [DEBUG] MEMS samples: 10
   [DEBUG] RMS Accel: X=0.015 Y=0.012 Z=1.001 G
   ```

### Manual Configuration (JSON)

1. **Create config.json**
   ```json
   {
     "wifi": {
       "ssid": "MyNetwork",
       "password": "MyPassword"
     },
     "mqtt": {
       "broker": "mqtt.example.com",
       "port": 1883
     }
   }
   ```

2. **Upload to SPIFFS**
   ```bash
   # Create www directory
   mkdir data
   mkdir data/www
   
   # Place config.json in data/
   # Upload with PlatformIO
   pio run --target uploadfs
   ```

## Building for Different Scenarios

### Standard Build
```bash
pio run
```

### Debug Build (with extra logging)
Edit `platformio.ini`:
```ini
build_flags =
    -D CORE_DEBUG_LEVEL=5
    -D LOG_LOCAL_LEVEL=5
```
Then: `pio run`

### Clean Build (clear previous artifacts)
```bash
pio run --target clean
pio run
```

### Verbose Output
```bash
pio run -v
pio run --target upload -v
```

## Uploading Web Dashboard

The dashboard HTML is served inline, but for custom versions:

1. **Place HTML in data/www/**
   ```
   data/
   └── www/
       └── index.html
   ```

2. **Upload filesystem**
   ```bash
   pio run --target uploadfs
   ```

3. **Access at device IP**
   - `http://192.168.4.1` (AP mode)
   - `http://<sta-ip>` (WiFi mode)

## Firmware Update Process

### OTA Update (Over-the-Air)

Implement OTA endpoint:
```cpp
// In web_server.cpp
server.on("/api/update", HTTP_POST, [](AsyncWebServerRequest* request) {
    int errorCode = Update.hasError() ? 1 : 0;
    request->send(200, "text/plain", errorCode ? "FAIL" : "OK");
});
```

Then upload new firmware via Web API.

### Manual Update

1. Connect board via USB
2. Run: `platformio run --target upload`
3. Reboot when complete

## Troubleshooting Build Issues

### "Board not found" Error
```bash
# List available boards
pio boards esp32

# Check board definition
pio boards esp32doit-devkit-v1

# Update ESP32 platform
pio platform update espressif32
```

### Library Compilation Errors
```bash
# Clear library cache
pio lib update
pio lib uninstall ArduinoJson
pio lib install ArduinoJson@^7.0.0

# Full clean
pio run --target clean
pio platform update
```

### USB Connection Issues

**Windows:**
- Install CH340 driver: https://sparkfun.com/ch340-driverhttps://github.com/MarlinFirmware/Marlin/wiki/Installing-Marlin
- Find COM port: Device Manager → COM & LPT Ports
- Try different USB port

**Linux:**
```bash
# Check device
ls -la /dev/ttyUSB*

# Add user to group
sudo usermod -a -G dialout $USER
sudo reboot
```

**Mac:**
```bash
# Install CP210x drivers
# https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

ls /dev/cu.*
```

### Baud Rate Issues
```bash
# Verify in serial monitor
platformio device monitor --baud 115200 --filter esp32_exception_decoder
```

If garbage output:
- Check board voltage (should be 3.3V stable)
- Try lower baud: 9600 or 74880
- Verify USB cable (data not charge-only)

## Partition & Memory Management

### Check Flash
```bash
pio run --target size
```

Output shows:
```
RAM:   [===       ] 35.1% (used 114961 bytes from 327680 bytes)
Flash: [====      ] 39.2% (used 512816 bytes from 1310720 bytes)
```

If flash usage >80%:
- Reduce log verbosity
- Disable unused features
- Use LittleFS instead of SPIFFS

### Custom Partition

Edit `platformio.ini`:
```ini
board_build.partitions = custom_partitions.csv
```

Create `custom_partitions.csv`:
```
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
otadata,  data, ota,     0xf000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
app1,     app,  ota_1,   0x150000,0x140000,
spiffs,   data, spiffs,  0x290000,0x170000,
```

## Performance Optimization

### Reduce Build Time
```bash
# Disable optimization (faster build, slower runtime)
build_flags = -Os

# Fast async I/O
-D CONFIG_ASYNC_TCP_RUNNING_CORE=1
```

### Reduce Firmware Size
```bash
# Strip symbols
build_flags = -s

# Compiler optimization
build_flags = -Os -flto
```

## CI/CD Integration

### GitHub Actions Example
```yaml
name: Build
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - uses: actions/setup-python@v2
      - run: pip install platformio
      - run: pio run
```

### GitLab CI Example
```yaml
build:firmware:
  image: python:3.9
  script:
    - pip install platformio
    - pio run
  artifacts:
    paths:
      - .pio/build/esp32doit-devkit-v1/
```

## Production Deployment

### Pre-Release Checklist
- [ ] Compile with optimizations: `-Os -flto`
- [ ] Disable debug logging in config.h
- [ ] Test deep sleep functionality
- [ ] Verify MQTT reconnection logic
- [ ] Test WiFi fallback to AP mode
- [ ] Validate battery voltage reading
- [ ] Check SPIFFS filesystem integrity

### Release Build
```bash
# Set version
export VERSION=1.0.0

# Build optimized
platformio run -e esp32doit-devkit-v1

# Create release archive
.pio/build/esp32doit-devkit-v1/firmware.bin → viot_v1.0.0.bin
```

### Deployment Steps
1. Backup current firmware
2. Upload new firmware to single test device
3. Verify all features work
4. Batch deploy if successful
5. Monitor for errors in production

## Support & Resources

- **PlatformIO Docs**: https://docs.platformio.org/
- **ESP32 Arduino**: https://github.com/espressif/arduino-esp32
- **ESP-IDF**: https://docs.espressif.com/projects/esp-idf/
- **Community**: https://community.platformio.org/

---

**Last Updated**: March 2024
**PlatformIO Version**: Latest API
