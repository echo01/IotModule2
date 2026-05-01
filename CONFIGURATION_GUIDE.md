# VIOT Configuration Guide

Complete reference for all configurable parameters and their effects.

## Configuration Storage

Configurations are stored in **SPIFFS** at `/config.json`

- **File size**: ~2 KB
- **Persistence**: Survives power cycles
- **Access**: Via web API or serial interface
- **Format**: JSON

## Configuration JSON Reference

### Root Level

```json
{
  "wifi": { ... },
  "mqtt": { ... },
  "adxl345": { ... },
  "power": { ... }
}
```

---

## WiFi Configuration

### Section: `"wifi"`

```json
"wifi": {
  "ssid": "YourNetwork",
  "password": "YourPassword",
  "ap_enabled": true,
  "ap_ssid": "VIOT_Config"
}
```

### Parameters

| Parameter | Type | Default | Valid Range | Description |
|-----------|------|---------|-------------|-------------|
| `ssid` | string | "YourSSID" | 1-32 chars | WiFi network name to connect to |
| `password` | string | "YourPassword" | 8-63 chars | WiFi password |
| `ap_enabled` | boolean | true | true/false | Enable AP mode (configuration access point) |
| `ap_ssid` | string | "VIOT_Config" | 1-32 chars | Access point network name |

### WiFi Behavior

**AP Mode (Access Point)**
- Always active for configuration
- SSID: `VIOT_<MAC>` (auto-generated from last 2 bytes of MAC)
  - Example: `VIOT_A5C3` 
- Default/AP SSID: `VIOT_Config`
- Password: `12345678` (hardcoded)
- IP: `192.168.4.1`
- Allows web configuration when not connected to STA

**STA Mode (Station)**
- Connects to configured network via `ssid`/`password`
- DHCP enabled automatically
- Falls back to AP if unable to connect after 20 seconds
- Reconnect attempts every 30 seconds

### Example: Network with No Password

```json
"wifi": {
  "ssid": "OpenNetwork",
  "password": "",
  "ap_enabled": true,
  "ap_ssid": "VIOT_Config"
}
```

### Example: Specific AP SSID

```json
"wifi": {
  "ssid": "MyNetwork",
  "password": "SecurePassword",
  "ap_enabled": true,
  "ap_ssid": "VIOT_Module_01"
}
```

---

## MQTT Configuration

### Section: `"mqtt"`

```json
"mqtt": {
  "broker": "broker.hivemq.com",
  "port": 1883,
  "client_id": "ESP32_VIOT_001",
  "username": "",
  "password": "",
  "topic_publish": "viot/vibration",
  "topic_subscribe": "viot/config",
  "publish_interval_sec": 60,
  "use_tls": false,
  "aws_iot_enabled": false
}
```

### Parameters

| Parameter | Type | Default | Valid Range | Description |
|-----------|------|---------|-------------|-------------|
| `broker` | string | "broker.hivemq.com" | Hostname/IP | MQTT broker address |
| `port` | number | 1883 | 1-65535 | MQTT port (1883 = plaintext, 8883 = TLS) |
| `client_id` | string | "ESP32_VIOT" | 1-128 chars | MQTT client identifier |
| `username` | string | "" | 0-128 chars | MQTT username (empty = anonymous) |
| `password` | string | "" | 0-128 chars | MQTT password |
| `topic_publish` | string | "viot/vibration" | 1-256 chars | Topic for publishing sensor data |
| `topic_subscribe` | string | "viot/config" | 1-256 chars | Topic for receiving commands |
| `publish_interval_sec` | number | 60 | 30-3600 | Seconds between MQTT publishes |
| `use_tls` | boolean | false | true/false | Enable SSL/TLS encryption |
| `aws_iot_enabled` | boolean | false | true/false | AWS IoT Core support |

### Publish Interval Effects

| Value | Use Case | Battery Impact |
|-------|----------|------------------|
| 30s | High-speed monitoring | High (frequent WiFi) |
| 60s | Balanced (recommended) | Medium |
| 300s (5m) | Low-bandwidth | Low |
| 3600s (1h) | Ultra-low battery | Very Low |

### Example: HiveMQ Broker (Public MQTT)

```json
"mqtt": {
  "broker": "broker.hivemq.com",
  "port": 1883,
  "client_id": "mymodule_001",
  "username": "",
  "password": "",
  "topic_publish": "myorg/site01/module01",
  "topic_subscribe": "myorg/site01/module01/cmd",
  "publish_interval_sec": 60,
  "use_tls": false,
  "aws_iot_enabled": false
}
```

### Example: AWS IoT Core

```json
"mqtt": {
  "broker": "a12bcd345ef6uvw.iot.us-east-1.amazonaws.com",
  "port": 8883,
  "client_id": "mymodule_001",
  "username": "",
  "password": "",
  "topic_publish": "$aws/things/mymodule_001/shadow/update",
  "topic_subscribe": "$aws/things/mymodule_001/shadow/delta",
  "publish_interval_sec": 60,
  "use_tls": true,
  "aws_iot_enabled": true
}
```

### Example: Private MQTT with Auth

```json
"mqtt": {
  "broker": "mqtt.company.local",
  "port": 1883,
  "client_id": "viot_production_02",
  "username": "hiot_user",
  "password": "SecurePassword123",
  "topic_publish": "factory/floor1/machine3",
  "topic_subscribe": "factory/floor1/machine3/config",
  "publish_interval_sec": 120,
  "use_tls": false,
  "aws_iot_enabled": false
}
```

### MQTT Payload Structure

Published to `topic_publish`:

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

---

## ADXL345 Sensor Configuration

### Section: `"adxl345"`

```json
"adxl345": {
  "rate_hz": 1600,
  "range_g": 16,
  "offset_x": 0.0,
  "offset_y": 0.0,
  "offset_z": 0.0
}
```

### Parameters

| Parameter | Type | Default | Valid Range | Description |
|-----------|------|---------|-------------|-------------|
| `rate_hz` | number | 1600 | 6, 12, 25, 50, 100, 200, 400, 800, 1600 | Sampling rate in Hz |
| `range_g` | number | 16 | 2, 4, 8, 16 | Full-scale measurement range (±G) |
| `offset_x` | number | 0.0 | -16 to +16 | Gravity offset X axis (calibration) |
| `offset_y` | number | 0.0 | -16 to +16 | Gravity offset Y axis (calibration) |
| `offset_z` | number | 0.0 | -16 to +16 | Gravity offset Z axis (calibration) |

### Sampling Rate Effects

| Rate | Buffer Size | Duration | Frequency Res | Use Case |
|------|------------|----------|---------------|----------|
| 6 Hz | 12 | 2 sec | 0.009 Hz | Ultra low power |
| 25 Hz | 50 | 2 sec | 0.024 Hz | Low power |
| 100 Hz | 200 | 2 sec | 0.098 Hz | Standard |
| 400 Hz | 800 | 2 sec | 0.39 Hz | High precision |
| **1600 Hz** | **3200** | **2 sec** | **1.56 Hz** | **Full bandwidth** |

### Range Selection

| Range | Resolution | Sensitivity | Best For |
|-------|-----------|-------------|----------|
| ±2G | 3.9 mg | 256 LSB/G | Precision monitoring |
| ±4G | 7.8 mg | 128 LSB/G | Standard machinery |
| ±8G | 15.6 mg | 64 LSB/G | Heavy equipment |
| **±16G** | **31.2 mg** | **32 LSB/G** | **Impact/shock** |

### Calibration (Offset)

Offsets compensate for sensor mounting orientation:

**Calibration Steps:**
1. Mount sensor in known orientation
2. Measure output for 10 seconds
3. Calculate average
4. Set offset = -(average value)
5. Verify new reading ≈ 0G (or 1G on gravity axis)

**Example: Horizontal Mount (Z faces up)**
- Expected: X ≈ 0G, Y ≈ 0G, Z ≈ 1G
- If reading: X = 0.02G, Y = -0.01G, Z = 0.98G
- Set: offset_x = -0.02, offset_y = 0.01, offset_z = -0.02

### Example: High-Speed Bearings

```json
"adxl345": {
  "rate_hz": 1600,
  "range_g": 8,
  "offset_x": -0.015,
  "offset_y": 0.008,
  "offset_z": 0.0
}
```

### Example: Heavy Machinery (Shock-Resistant)

```json
"adxl345": {
  "rate_hz": 400,
  "range_g": 16,
  "offset_x": 0.0,
  "offset_y": 0.0,
  "offset_z": 0.02
}
```

---

## Power Management Configuration

### Section: `"power"`

```json
"power": {
  "sleep_enabled": true,
  "sleep_interval_sec": 3600
}
```

### Parameters

| Parameter | Type | Default | Valid Range | Description |
|-----------|------|---------|-------------|-------------|
| `sleep_enabled` | boolean | true | true/false | Enable deep sleep mode |
| `sleep_interval_sec` | number | 3600 | 60-86400 | Seconds to sleep before wake timer |

### Sleep Interval Reference

| Value | Time | Use Case | Day Samples |
|-------|------|----------|------------|
| 300 | 5 min | High-frequency | 288 |
| 600 | 10 min | Standard | 144 |
| 1800 | 30 min | Balanced | 48 |
| **3600** | **1 hour** | **Recommended** | **24** |
| 7200 | 2 hours | Low power | 12 |
| 86400 | 24 hours | Ultra-low | 1 |

### Sleep Mode Behavior

When `sleep_enabled = true`:

1. **WiFi/MQTT operations complete**
2. **Data stored/sent**
3. **All peripherals disabled**
4. **ESP32 enters deep sleep**
5. **Current: ~10 µA** (RTC only)
6. **Wake on**: Timer OR ADXL345 vibration interrupt
7. **Wake duration**: ~2-3 seconds for WiFi connect + data send

### Debug Mode Override

GPIO27 = HIGH **always** disables sleep:
- Ignores `sleep_enabled` setting
- Device always awake
- Serial debug output enabled

Press GPIO27 = LOW to resume normal operation.

### Example: Battery Conservation

```json
"power": {
  "sleep_enabled": true,
  "sleep_interval_sec": 7200
}
```
Wakes every 2 hours, reducing daily publishes to 12 (battery lasts months).

---

## Complete Configuration Examples

### Example 1: IoT Platform (Motion-Sensitive Monitoring)

```json
{
  "wifi": {
    "ssid": "ArmoredFactoryNetwork",
    "password": "Secure@Factory2024",
    "ap_enabled": true,
    "ap_ssid": "VIOT_MAINT"
  },
  "mqtt": {
    "broker": "iot.company.local",
    "port": 1883,
    "client_id": "viot_bearing_01",
    "username": "factory_service",
    "password": "ServicePass123",
    "topic_publish": "factory/line1/bearing/vibration",
    "topic_subscribe": "factory/line1/bearing/cmd",
    "publish_interval_sec": 30,
    "use_tls": false,
    "aws_iot_enabled": false
  },
  "adxl345": {
    "rate_hz": 1600,
    "range_g": 8,
    "offset_x": -0.01,
    "offset_y": 0.02,
    "offset_z": 0.0
  },
  "power": {
    "sleep_enabled": false,
    "sleep_interval_sec": 3600
  }
}
```

### Example 2: Battery-Powered Remote Monitoring

```json
{
  "wifi": {
    "ssid": "RemoteNetwork",
    "password": "RemotePass2024",
    "ap_enabled": false,
    "ap_ssid": "VIOT_Remote"
  },
  "mqtt": {
    "broker": "broker.hivemq.com",
    "port": 1883,
    "client_id": "remote_vibration_monitor",
    "username": "",
    "password": "",
    "topic_publish": "outdoor/building5/equipment",
    "topic_subscribe": "",
    "publish_interval_sec": 300,
    "use_tls": false,
    "aws_iot_enabled": false
  },
  "adxl345": {
    "rate_hz": 100,
    "range_g": 16,
    "offset_x": 0.0,
    "offset_y": 0.0,
    "offset_z": 0.0
  },
  "power": {
    "sleep_enabled": true,
    "sleep_interval_sec": 3600
  }
}
```

### Example 3: AWS IoT LoRaWAN Gateway

```json
{
  "wifi": {
    "ssid": "AWS-Gateway",
    "password": "GatewaySecure2024",
    "ap_enabled": true,
    "ap_ssid": "VIOT_GW_Config"
  },
  "mqtt": {
    "broker": "a1bcd2efg3hij4k5l6m.iot.us-west-2.amazonaws.com",
    "port": 8883,
    "client_id": "viot_gateway_facility3",
    "username": "",
    "password": "",
    "topic_publish": "$aws/things/viot_gateway_facility3/shadow/update",
    "topic_subscribe": "$aws/things/viot_gateway_facility3/shadow/delta",
    "publish_interval_sec": 60,
    "use_tls": true,
    "aws_iot_enabled": true
  },
  "adxl345": {
    "rate_hz": 400,
    "range_g": 16,
    "offset_x": 0.0,
    "offset_y": 0.0,
    "offset_z": 0.0
  },
  "power": {
    "sleep_enabled": false,
    "sleep_interval_sec": 3600
  }
}
```

---

## Configuration via Web API

### GET /api/config

Retrieve current configuration:

```bash
curl http://192.168.4.1/api/config
```

Response:
```json
{
  "wifi": { ... },
  "mqtt": { ... },
  ...
}
```

### POST /api/config

Update configuration:

```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Content-Type: application/json" \
  -d '{...json data...}'
```

Changes take effect on next reconnect cycle.

---

## Environment-Specific Settings

### Development

- WiFi: Any local network
- MQTT: Localhost (mosquitto)
- Power: `sleep_enabled = false`
- Advanced: Debug mode enabled

### Testing

- WiFi: Test network (bridged to production)
- MQTT: Test broker
- Power: `sleep_enabled = true`, `sleep_interval_sec = 300`
- Monitoring: Real-time validation

### Production

- WiFi: Production network
- MQTT: Production broker (TLS recommended)
- Power: `sleep_enabled = true`, tuned interval
- Monitoring: Log-based error tracking

---

## Common Configuration Issues

### WiFi Keeps Disconnecting

**Possible Causes:**
- AP channel interference → change WiFi channel in router
- Password mismatch → verify credentials spelled correctly
- Signal too weak → place router closer

**Fix:**
```json
"wifi": {
  "ssid": "YourSSID",
  "password": "ExactPassword123",
  "ap_enabled": true
}
```

### MQTT Never Connects

**Possible Causes:**
- Wrong broker address/port → verify `broker` and `port`
- Network blocking MQTT (cloud) → check firewall
- Broker requires auth → add `username` and `password`

**Fix:**
```json
"mqtt": {
  "broker": "mqtt.company.local",
  "port": 1883,
  "username": "user123",
  "password": "pass123"
}
```

### High Current Consumption

**Causes:**
- Sleep not enabled → set `sleep_enabled = true`
- Debug mode active → pull GPIO27 LOW
- Publish interval too short → increase `publish_interval_sec`

**Fix:**
```json
"power": {
  "sleep_enabled": true,
  "sleep_interval_sec": 3600
}
```

---

## Configuration Validation

VIOT uses ArduinoJson for config parsing. Validation rules:

- ✓ Missing keys → uses defaults
- ✓ Wrong types → auto-converts if possible
- ✗ Invalid ranges → clamped to nearest valid
- ✗ Malformed JSON → uses defaults

All config changes logged to serial monitor.

---

**Last Updated**: March 2024
**VIOT Version**: 1.0.0
