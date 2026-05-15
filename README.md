# VIOT ESP32 Firmware

Firmware for an ESP32-based vibration monitoring node built around an `ADXL345`, WiFi, HTTP/WebSocket APIs, and MQTT/MQTTS. This document is written as a handoff for the next step: building a Mobile App that can discover the device, read live status, configure it, and request FFT data.

This README reflects the current code in:

- `src/main.cpp`
- `src/web_server.cpp`
- `src/discovery_service.cpp`
- `src/mqtt_handler.cpp`
- `src/wifi_handler.cpp`
- `src/storage.cpp`
- `src/sensors.cpp`

## 1. What The Firmware Does

- Samples `1024` accelerometer samples from the `ADXL345`
- Computes RMS acceleration, RMS velocity, dominant vibration frequency, displacement, and FFT
- Exposes an HTTP API for config, status, dashboard, discovery, FFT, reboot, and reset
- Pushes live dashboard data over `WebSocket /ws`
- Publishes telemetry to MQTT or MQTTS
- Accepts MQTT commands to generate FFT result payloads per axis
- Stores configuration in SPIFFS at `/config.json`
- Supports device discovery with `mDNS`, `UDP`, and `GET /api/discover`

## 2. Mobile App Integration Summary

If the next system is a Mobile App, the most useful integration path is:

1. Discover the device on LAN with `mDNS` or UDP.
2. Call `GET /api/discover`.
3. Poll `GET /api/status` for connectivity/health.
4. Poll `GET /api/dashboard` or subscribe to `WS /ws` for live data.
5. Read `GET /api/config` before showing settings screens.
6. Save settings through the `POST /api/*_config` endpoints.
7. Use MQTT only if the app also needs broker-based remote telemetry or FFT command workflow.

The full contract is in [API_DISCOVERY_GUIDE.md](API_DISCOVERY_GUIDE.md).

## 3. Current Network Model

### Discovery

- mDNS service: `_iot-sensor._tcp.local`
- UDP discovery port: `37020`
- HTTP discovery endpoint: `GET /api/discover`
- Protocol identifier: `viot-discovery-v1`

### Connection modes

- `STA connected`: device is on the existing WiFi network and is reachable via STA IP or hostname `.local`
- `AP_ONLY`: device exposes its own AP when AP mode is allowed and STA is not connected
- `WIFI_FAILED`: STA retries exhausted

Important current behavior from `src/wifi_handler.cpp`:

- SoftAP is not always kept on in normal mode.
- SoftAP is mainly available during debug fallback or explicit AP-enabled fallback scenarios.
- When STA connects successfully, SoftAP is stopped.

That means the Mobile App should not assume the device AP will always be present after provisioning.

## 4. HTTP / WebSocket Surface

### Main pages

- `/`
- `/index.html`
- `/mqtt_setting.html`
- `/network_setting.html`
- `/mems_setting.html`
- `/operate_config.html`
- `/system_setting.html`
- `/fft_chart.html`
- `/mqtt_log`

### Main APIs

- `GET /api/discover`
- `GET /api/status`
- `GET /api/config`
- `GET /api/dashboard`
- `GET /api/fft_spectrum?axis=x`
- `GET /api/fft_csv?axis=x`
- `GET /api/mqtt_publish_summary`
- `GET /api/scan_ssid`
- `POST /api/config`
- `POST /api/network_config`
- `POST /api/mqtt_config`
- `POST /api/mems_config`
- `POST /api/operate_config`
- `POST /api/system_config`
- `POST /api/ap_config`
- `POST /api/reboot`
- `POST /api/reset`
- `WS /ws`

### Important request format note

Current `POST` handlers read values with `request->hasParam(name, true)`, so Mobile clients should send:

- `application/x-www-form-urlencoded`, or
- `multipart/form-data`

Do not assume JSON request bodies are accepted by the current firmware.

## 5. Data The Mobile App Can Use

### `GET /api/status`

Best for:

- device online/offline state
- MQTT connection state
- battery snapshot
- WiFi RSSI
- sampling timing diagnostics

### `GET /api/dashboard`

Best for:

- latest processed vibration values
- RMS velocity per axis
- dominant vibration frequency per axis
- displacement
- battery and RSSI in the same payload

### `WS /ws`

Best for:

- near real-time dashboard updates while a screen is open

Current WebSocket behavior:

- server push only
- no incoming command protocol implemented
- payload includes accel, velocity, vibration frequency, FFT peaks, orientation, displacement, battery, RSSI, and MQTT publish summary fields

### `GET /api/fft_spectrum`

Best for:

- charting local FFT from the app without MQTT

Parameters:

- `axis=x|y|z`

### `GET /api/fft_csv`

Best for:

- quick export
- debug download
- simple chart import

## 6. MQTT Surface

### Default topics

- main publish: `viot/vibration`
- FFT X: `viot/vibration/fft/x`
- FFT Y: `viot/vibration/fft/y`
- FFT Z: `viot/vibration/fft/z`
- command subscribe: `viot/config`
- command ack: `viot/config/ack`
- command result: `viot/config/result`

### Main telemetry payload

Main publish is a compact JSON object shaped like:

```json
{
  "timestamp": 1715772000,
  "data": {
    "accel_x_rms": 0.01,
    "accel_y_rms": 0.02,
    "accel_z_rms": 1.00,
    "vibration_x_rms_mm_s": 0.50,
    "vibration_y_rms_mm_s": 0.80,
    "vibration_z_rms_mm_s": 1.20,
    "vibration_freq_x_hz": 48.0,
    "vibration_freq_y_hz": 49.0,
    "vibration_freq_z_hz": 50.0,
    "displacement_x_um": 10.0,
    "displacement_y_um": 11.0,
    "displacement_z_um": 12.0,
    "pitch_deg": 0.0,
    "roll_deg": 0.0,
    "yaw_deg": 0.0,
    "battery_v": 4.02,
    "wifi_rssi": -55,
    "uptime_ms": 120000
  }
}
```

### MQTT FFT command flow

The app can request FFT by publishing one of:

- plain text: `fft_x`, `fft_y`, `fft_z`
- JSON:

```json
{
  "action": "fft_x",
  "request_id": "req-001",
  "step_hz": 10
}
```

Supported step values are normalized to:

- `10`, `15`, `20`, `25`, `30`, `35`, `40`, `45`, `50`

Result flow:

1. Device receives command on subscribe topic.
2. Device publishes ack on ack topic.
3. Device waits until `3` analysis rounds are collected.
4. Device publishes FFT result on:
   - command result topic
   - axis topic (`topic_fft_x`, `topic_fft_y`, or `topic_fft_z`)

This is the recommended remote FFT pattern when the Mobile App talks to the broker directly.

## 7. Configuration Model

Config is stored in SPIFFS at `/config.json`.

Main sections:

- `wifi`
- `mqtt`
- `adxl345`
- `vibration`
- `power`
- `operate` is returned by `GET /api/config` as a UI-oriented view that overlaps with `mqtt`, `adxl345`, and `power`

Important UI mapping:

- firmware stores `adxl345.rate_hz` as output data rate
- web/mobile UI may show it as vibration bandwidth
- current mapping is:
  - `400` ODR -> `200 Hz` bandwidth
  - `800` ODR -> `400 Hz` bandwidth
  - `1600` ODR -> `800 Hz` bandwidth

## 8. Runtime Behavior That Affects App UX

- Some HTTP requests pause MQTT temporarily when the request is served over the STA IP, to protect heap during web/API access.
- `GET /api/dashboard` may return `has_data=false` until the first valid analysis is ready.
- `GET /api/fft_spectrum` and `GET /api/fft_csv` can fail with `500` if no FFT snapshot is available yet.
- Some endpoints can return `503 {"error":"low memory"}` when heap is low.
- Battery value timing differs by mode:
  - normal mode: sampled once during startup and reused for that wake cycle
  - debug mode: refreshed every `30` seconds

## 9. Hardware Mapping

From [include/config.h](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/include/config.h):

- `GPIO27`: mode switch, `HIGH = debug`, `LOW = normal`
- `GPIO32`: status LED
- `GPIO33`: MQTT activity LED
- `GPIO34`: battery ADC
- `GPIO25`: ADXL345 `INT1`
- `GPIO26`: ADXL345 `INT2`
- `GPIO23`: debug sample pulse
- `GPIO21`: I2C SDA
- `GPIO22`: I2C SCL

## 10. Build And Flash

PlatformIO environment:

- environment name: `esp32-s3-devkitc-1`
- board configured in `platformio.ini`: `esp32dev`
- framework: Arduino
- filesystem: SPIFFS
- monitor speed: `115200`

Commands:

```bash
pio run
pio run --target upload
pio run --target uploadfs
pio device monitor --baud 115200
```

Detailed setup and validation steps are in [BUILD_GUIDE.md](BUILD_GUIDE.md).

## 11. Files Most Relevant To Mobile Handoff

- [src/web_server.cpp](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/src/web_server.cpp)
- [src/discovery_service.cpp](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/src/discovery_service.cpp)
- [src/mqtt_handler.cpp](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/src/mqtt_handler.cpp)
- [src/wifi_handler.cpp](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/src/wifi_handler.cpp)
- [src/storage.cpp](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/src/storage.cpp)
- [include/common.h](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/include/common.h)
- [include/config.h](/c:/Users/User/Documents/PlatformIO/Projects/iotmodule2/include/config.h)

## 12. Recommended Next Mobile Tasks

1. Build a LAN discovery service using mDNS first, UDP fallback second.
2. Create a device repository layer around `GET /api/discover`, `GET /api/status`, and `GET /api/dashboard`.
3. Add a WebSocket dashboard channel for live monitoring.
4. Implement settings screens backed by `GET /api/config` and the `POST /api/*_config` endpoints.
5. Decide whether FFT in the Mobile App should come from:
   - local HTTP `GET /api/fft_spectrum`, or
   - broker-mediated MQTT command/result flow
6. Handle `503 low memory`, temporary MQTT pauses, and reboot/reset flows in UI.
