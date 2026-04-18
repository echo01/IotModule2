# ESP32 IoT Vibration Monitoring Module (VIOT)

A production-grade firmware project for ESP32 with ADXL345 6-axis MEMS sensor, featuring FreeRTOS task management, WiFi + MQTT communication, web configuration dashboard, and low-power operation.

## Features

- **Sensor Integration**: ADXL345 accelerometer via I2C (configurable 1600 Hz sampling)
- **Vibration Analysis**: RMS acceleration/velocity, FFT frequency analysis, pitch/roll calculation
- **Power Management**: Deep sleep with timer/interrupt wakeup, GPIO27 debug mode switch
- **Communication**: WiFi AP+STA dual mode, MQTT with TLS/SSL support, WebSocket real-time dashboard
- **Configuration**: JSON-based config in SPIFFS, web-based settings interface
- **Monitoring**: Battery voltage ADC, WiFi RSSI, MQTT status LED feedback
- **Debug Mode**: Comprehensive serial logging when GPIO27 = HIGH

## Hardware Requirements

- **Board**: ESP32-DOIT-DEVKIT-V1
- **Sensor**: ADXL345 acceleration sensor (I2C)
- **GPIO Mapping**:
  - GPIO27: Mode switch (HIGH=Debug/Always ON, LOW=Normal)
  - GPIO32: Status LED (active HIGH, ON at wakeup)
  - GPIO33: MQTT status (blinks 0.1s when publishing)
  - GPIO3: Battery voltage ADC (100k/100k divider)
  - GPIO12: ADXL345 INT1 (interrupt 1)
  - GPIO14: ADXL345 INT2 (interrupt 2)
  - GPIO21: I2C SDA
  - GPIO22: I2C SCL

## Installation & Setup

### 1. Prerequisites

- PlatformIO (CLI or VS Code extension)
- Python 3.6+

### 2. Clone/Download Project

```bash
git clone <repo-url> iotmodule2
cd iotmodule2
```

### 3. Build the Project

```bash
# Build for the default environment (esp32doit-devkit-v1)
platformio run

# Or with verbose output
platformio run -v
```

### 4. Upload to Board

```bash
# Connect board via USB and upload
platformio run --target upload

# Or specify port:
platformio run --target upload --upload-port COM3
```

### 5. Monitor Serial Output

```bash
# Open serial monitor at 115200 baud
platformio device monitor --baud 115200

# With line ending and timestamp
platformio device monitor --baud 115200 --filter time
```

## Configuration

### First Boot

1. Power on the device
2. If `config.json` doesn't exist, default configuration is used
3. Connect to AP: `VIOT_XXYY` (last 2 bytes of MAC address)
4. Access web interface: `http://192.168.4.1`
5. Configure WiFi SSID/password and MQTT settings

### Manual Configuration

1. Edit `config.example.json` with your settings
2. Upload to SPIFFS using PlatformIO
3. Rename to `config.json`

Or via web interface: `http://<device-ip>/api/config`

### Configuration Parameters

```json
{
  "wifi": {
    "ssid": "YourSSID",
    "password": "password",
    "ap_enabled": true
  },
  "mqtt": {
    "broker": "broker.hivemq.com",
    "port": 1883,
    "topic_publish": "viot/vibration",
    "publish_interval_sec": 60
  },
  "adxl345": {
    "rate_hz": 1600,
    "range_g": 16,
    "offset_x": 0.0
  },
  "power": {
    "sleep_enabled": true,
    "sleep_interval_sec": 3600
  }
}
```

## Operating Modes

### Normal Mode (GPIO27 = LOW)

- Deep sleep enabled after WiFi/MQTT operations
- Minimal serial output (errors only)
- Wake on timer (1 hour default) or ADXL345 interrupt
- Optimized for battery operation

### Debug Mode (GPIO27 = HIGH)

- Deep sleep disabled, always awake
- Comprehensive serial logging at 115200 baud
- Real-time data streaming (CSV/JSON format)
- Includes:
  - Wakeup reason
  - WiFi connection status & RSSI
  - MQTT status
  - MEMS raw samples (first 10)
  - Acceleration RMS (X, Y, Z) in G
  - Velocity RMS (X, Y, Z) in mm/s
  - FFT peak frequencies and power (dB)
  - Battery voltage
  
## Web Dashboard

Access the web interface:

- **URL**: `http://<device-ip-or-ap-ip>`
- **Default AP**: `http://192.168.4.1`
- **Endpoints**:
  - `GET /api/config` - Get current configuration
  - `POST /api/config` - Update configuration
  - `GET /api/status` - Get system status
  - `POST /api/reboot` - Reboot device
  - `POST /api/reset` - Factory reset
  - `WS /ws` - WebSocket for real-time data

## Data Flow

```
Sensor (ADXL345, ADC)
    ↓
[MEMS Task] - 1600 Hz sampling, 2-second buffers
    ↓
[Process Task] - RMS, FFT, Orientation calculation
    ↓
  ┌─┴──────────────────────┬─────────────────┐
  ↓                         ↓                 ↓
[MQTT Task]          [WebSocket Task]   [LocalBuffer]
  ↓                         ↓
MQTT Broker          Dashboard Clients
  
[WiFi Task] ← Manages connectivity
[ADC Task] ← Battery monitoring every 30s
[Web Task] ← HTTP server & WebSocket
```

## MQTT Payload Example

Published to `viot/vibration`:

```json
{
  "timestamp": 1234567890000,
  "accel": {
    "x_rms": 0.15,
    "y_rms": 0.12,
    "z_rms": 1.01
  },
  "velocity": {
    "x_rms_mm_s": 2.34,
    "y_rms_mm_s": 1.89,
    "z_rms_mm_s": 15.67
  },
  "fft": {
    "peak_freq_x_hz": 45.2,
    "peak_freq_y_hz": 38.5,
    "peak_freq_z_hz": 50.1,
    "power_x_db": 22.3,
    "power_y_db": 19.8,
    "power_z_db": 25.6
  },
  "orientation": {
    "pitch_deg": 2.34,
    "roll_deg": -1.56
  },
  "system": {
    "battery_v": 3.95,
    "wifi_rssi": -45,
    "uptime_ms": 12345678
  },
  "raw": {
    "accel_x": [0.05, 0.06, ...],
    "accel_y": [...],
    "accel_z": [...]
  }
}
```

## Power Consumption

| Mode | Current | Duration | Notes |
|------|---------|----------|-------|
| Idle (AP) | ~80 mA | Listening for config | WiFi radio active |
| WiFi Active | ~120 mA | 30-60 sec | Connecting/maintaining |
| MQTT Publish | ~150 mA | 5-10 sec | WiFi TX burst |
| Deep Sleep | ~10 µA | Up to 1 hour | All peripherals off |

## FreeRTOS Task Configuration

| Task | Priority | Core | Stack | Frequency |
|------|----------|------|-------|-----------|
| MEMS | 8 | 0 | 8 KB | Real-time (1600 Hz) |
| WiFi | 5 | 1 | 8 KB | 10 sec |
| MQTT | 6 | 1 | 8 KB | 5 sec |
| MQTT-Pub | 7 | 1 | 4 KB | 60 sec (or config) |
| Web | 4 | 1 | 4 KB | Async |
| ADC | 2 | 1 | 2 KB | 30 sec |

## Troubleshooting

### Device not connecting to WiFi

1. Check SSID/password in config
2. Enable debug mode (GPIO27 = HIGH)
3. Check serial output for WiFi status
4. Verify AP mode fallback at `VIOT_XXYY`

### MQTT not connecting

1. Verify broker address and port
2. Check network connectivity
3. Verify TLS certificates (if enabled)
4. Check MQTT log in debug mode

### Sensor reading errors

1. Verify I2C connections (GPIO21/22)
2. Check I2C address (0x53 vs 0x1D)
3. Enable sensor self-test in debug mode
4. Check power supply (3.3V stable)

### High current consumption

1. Check if WiFi is stuck connecting
2. Verify deep sleep is enabled (GPIO27 = LOW)
3. Monitor CPU temperature
4. Check for infinite loops in debug

## Development Notes

### Adding Custom Features

1. **New Tasks**: Create `src/new_task.cpp` and include in main
2. **New Endpoints**: Add to `web_server.cpp` handlers
3. **New Queues**: Define in main.cpp and pass via parameters

### Build Configuration

Edit `platformio.ini` to:
- Change board: `board = esp32doit-devkit-v1`
- Add libraries: Under `lib_deps`
- Adjust stack sizes: In `config.h`

### Memory Management

- PSRAM support enabled by default
- SPIFFS: 1 MB for filesystem
- Adjust partition scheme in `platformio.ini`

## API Reference

### Sensors
- `MEMSSensor::readRawData()` - Capture 2 seconds at 1600 Hz
- `MEMSSensor::processVibrationData()` - Calculate RMS and FFT
- `BatterySensor::readVoltage()` - ADC to voltage

### Communication
- `WiFiHandler::connectToAP()` - Establish WiFi connection
- `MQTTHandler::publishVibrationData()` - Send JSON payload
- `WebServer::updateRealtimeData()` - Push to WebSocket

### Power
- `PowerManager::enterDeepSleep()` - Sleep mode
- `PowerManager::setupInterrupt()` - Configure wakeup

### Storage
- `Storage::loadConfig()` - Read JSON from SPIFFS
- `Storage::saveConfig()` - Persist configuration

## Performance Metrics

- **Sampling Rate**: 1600 Hz (configurable 6-1600 Hz)
- **FFT Window**: 1024 samples (1.6 kHz bandwidth)
- **Frequency Resolution**: 1.56 Hz per bin
- **Data Latency**: <3 sec sensor to cloud
- **Max MQTT Payload**: 16 KB

## License

MIT License - See LICENSE file

## Support

For issues, feature requests, or contributions, please open an issue on the repository.

## Changelog

### v1.0.0
- Initial release
- ADXL345 integration
- WiFi + MQTT communication
- Web dashboard with WebSocket
- Deep sleep power management
- JSON configuration storage

---

**Last Updated**: March 2024
**Firmware Version**: 1.0.0
**Board**: ESP32-DOIT-DEVKIT-V1
