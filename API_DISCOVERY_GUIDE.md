# VIOT API And Discovery Guide

Reference for Mobile App integration against the current firmware. This guide is based on:

- `src/discovery_service.cpp`
- `src/web_server.cpp`
- `src/mqtt_handler.cpp`
- `src/wifi_handler.cpp`
- `include/common.h`
- `include/config.h`

## 1. Recommended Client Flow

For a Mobile App on the same LAN, use this order:

1. Try `mDNS` discovery.
2. If not found, try UDP discovery on port `37020`.
3. Call `GET /api/discover` on the resolved IP/host.
4. Cache the returned `device_id`, `hostname`, `sta_ip`, and `ap_ip`.
5. Use:
   - `GET /api/status` for health
   - `GET /api/dashboard` or `WS /ws` for live data
   - `GET /api/config` for settings
6. Save user changes through the config `POST` endpoints.

## 2. Discovery Methods

### 2.1 mDNS

The firmware advertises this service when STA WiFi is connected:

- service type: `_iot-sensor._tcp.local`
- port: `80`
- protocol tag: `viot-discovery-v1`

Published TXT records:

- `path=/api/discover`
- `protocol=viot-discovery-v1`
- `id=<hostname>`
- `mac=<device-mac>`

Example resolved URL:

- `http://esp32-viot-a1b2c3.local/api/discover`

Notes:

- mDNS is available only when STA is connected.
- If the device is not on the main WiFi, mDNS may not be usable.

### 2.2 UDP Discovery

Listener:

- UDP port: `37020`

Accepted payloads:

- `VIOT_DISCOVER`
- `DISCOVER_VIOT`
- JSON containing `"type":"discover"`
- JSON containing `"protocol":"viot-discovery-v1"`

Recommended request:

```text
VIOT_DISCOVER
```

Recommended client behavior:

- send broadcast UDP packet on port `37020`
- wait for one or more JSON replies
- parse each reply as one device candidate

### 2.3 HTTP Discovery

After the host or IP is known, call:

- `GET /api/discover`

This returns the same summary as UDP discovery.

## 3. Discovery Response Contract

Example response:

```json
{
  "protocol": "viot-discovery-v1",
  "device_type": "viot-sensor-node",
  "device_name": "esp32-viot-a1b2c3",
  "device_id": "esp32-viot-a1b2c3",
  "hostname": "esp32-viot-a1b2c3",
  "mdns_host": "esp32-viot-a1b2c3.local",
  "mdns_service": "_iot-sensor._tcp.local",
  "web_port": 80,
  "udp_port": 37020,
  "discover_path": "/api/discover",
  "status_path": "/api/status",
  "wifi_state": "WIFI_CONNECTED",
  "sta_connected": true,
  "sta_ip": "192.168.1.45",
  "ap_ip": "192.168.4.1",
  "ap_ssid": "VIOT_A1B2",
  "mac": "AA:BB:CC:A1:B2:C3",
  "mqtt_client_id": "ESP32_VIOT",
  "rssi": -48,
  "uptime_sec": 123,
  "mdns_ready": true,
  "udp_ready": true,
  "fw_build_date": "May 15 2026",
  "fw_build_time": "10:30:00"
}
```

Field meaning:

- `device_id`: stable ID for app-side caching, currently same as hostname
- `hostname`: mDNS/identity name derived from MQTT client ID
- `sta_ip`: primary IP when STA is connected
- `ap_ip`: AP IP when SoftAP is running, usually `192.168.4.1`
- `wifi_state`: current firmware WiFi state machine state
- `mdns_ready`: whether mDNS is currently active
- `udp_ready`: whether UDP discovery is currently listening

## 4. Base URLs

Typical base URLs:

- `http://192.168.1.45`
- `http://esp32-viot-a1b2c3.local`
- `http://192.168.4.1`

The app should prefer:

1. `sta_ip` or `mdns_host` when `sta_connected=true`
2. `ap_ip` when the app is attached to the device AP

## 5. Request Format Rules

### GET endpoints

- standard HTTP GET
- JSON response unless explicitly documented as text/CSV

### POST endpoints

Current firmware reads form fields with `hasParam(..., true)`.

Use one of:

- `application/x-www-form-urlencoded`
- `multipart/form-data`

Do not send raw JSON bodies unless firmware is updated to parse them.

Boolean conventions accepted by current handlers:

- `"1"` means enabled/true in most config endpoints
- some endpoints also accept `"true"` for specific fields such as `clear_spiffs` and `use_tls`

## 6. Core GET Endpoints

### 6.1 `GET /api/status`

Purpose:

- lightweight health/status poll

Example response:

```json
{
  "debug_mode": true,
  "log_enabled": true,
  "uptime_sec": 123,
  "battery_v": 3.98,
  "wifi_rssi": -48,
  "wifi_state": "WIFI_CONNECTED",
  "ap_ip": "192.168.4.1",
  "sta_ip": "192.168.1.45",
  "mqtt_connected": true,
  "mqtt_status": "CONNECTED",
  "mqtt_broker": "broker.hivemq.com",
  "mems_timing": {
    "effective_sample_rate_hz": 799.2,
    "target_period_us": 1250,
    "capture_elapsed_us": 1281000,
    "avg_read_high_us": 260,
    "avg_wait_low_us": 990
  }
}
```

Use for:

- online badge
- signal strength
- battery widget
- MQTT connectivity indicator
- engineering diagnostics

### 6.2 `GET /api/config`

Purpose:

- read all current settings before rendering edit screens

Key sections in response:

- `wifi`
- `mqtt`
- `adxl345`
- `vibration`
- `power`
- `operate`

Important fields:

- `wifi.ap_ssid_effective`: actual AP SSID the device will use
- `mqtt.protocol`: derived convenience field, `mqtt` or `mqtts`
- `mqtt.status`: current runtime MQTT state
- `power.debug_logs.*`: bitmask expanded into booleans
- `operate.*`: app-friendly operational settings mirror

Important interpretation:

- `adxl345.rate_hz` is the sensor output data rate
- UI may present it as bandwidth
- mapping in current firmware/UI:
  - `400` -> `200 Hz` bandwidth
  - `800` -> `400 Hz` bandwidth
  - `1600` -> `800 Hz` bandwidth

### 6.3 `GET /api/dashboard`

Purpose:

- latest processed vibration snapshot

Example shape:

```json
{
  "has_data": true,
  "ts": 1715772000,
  "accel": { "x": 0.01, "y": 0.02, "z": 1.00 },
  "velocity": { "x": 0.50, "y": 0.80, "z": 1.20 },
  "vibration_freq": { "x": 48.0, "y": 49.0, "z": 50.0 },
  "orientation": { "pitch": 0.0, "roll": 0.0, "yaw": 0.0 },
  "displacement": { "x_um": 10.0, "y_um": 11.0, "z_um": 12.0 },
  "battery": 4.02,
  "rssi": -55,
  "wifi_state": "WIFI_CONNECTED",
  "mqtt": { "broker": "broker.hivemq.com", "status": "CONNECTED" },
  "ap_ip": "192.168.4.1",
  "mems_timing": {
    "effective_sample_rate_hz": 799.2,
    "target_period_us": 1250,
    "capture_elapsed_us": 1281000,
    "avg_read_high_us": 260,
    "avg_wait_low_us": 990
  }
}
```

Notes:

- `has_data=false` means analysis has not been produced yet
- this is a better dashboard payload than `status`

### 6.4 `GET /api/fft_spectrum?axis=x`

Parameters:

- `axis=x|y|z`

Example response:

```json
{
  "axis": "x",
  "freq_hz": [10.0, 20.0, 30.0],
  "amp_mm_s": [0.12, 0.08, 0.03]
}
```

Use for:

- in-app FFT chart
- local analysis without MQTT

Errors:

- `500 {"error":"fft spectrum unavailable"}`
- `503 {"error":"low memory"}`

### 6.5 `GET /api/fft_csv?axis=x`

Response type:

- `text/plain`

Example:

```text
Frequency(Hz),Amplitude(mm/s)
10.00,0.1200
20.00,0.0800
30.00,0.0300
```

### 6.6 `GET /api/mqtt_publish_summary`

Purpose:

- inspect recent MQTT activity

Example response:

```json
{
  "has_publish": true,
  "success": true,
  "publish_count": 22,
  "last_attempt_ms": 80000,
  "last_success_ms": 80000,
  "seconds_since_last_success": 2,
  "publish_interval_s": 60,
  "next_publish_due_ms": 140000,
  "seconds_until_next_publish": 58,
  "tls_connecting": false,
  "main_size": 310,
  "fft_x_size": 0,
  "fft_y_size": 0,
  "fft_z_size": 0,
  "subscribe_receive_count": 4,
  "last_subscribe_ms": 75000,
  "seconds_since_last_subscribe": 7,
  "last_subscribe_size": 52
}
```

Useful for:

- MQTT diagnostics screen
- broker connectivity troubleshooting
- command processing history

### 6.7 `GET /api/scan_ssid`

Purpose:

- request local WiFi scan results for a provisioning screen

Example response:

```json
{
  "networks": [
    { "ssid": "OfficeWiFi", "rssi": -51, "channel": 6, "secure": true },
    { "ssid": "LabGuest", "rssi": -73, "channel": 11, "secure": false }
  ]
}
```

## 7. WebSocket Endpoint

### `WS /ws`

Purpose:

- live dashboard stream

Current payload includes:

- `ts`
- `accel.{x,y,z}`
- `velocity.{x,y,z}`
- `vibration_freq.{x,y,z}`
- `fft.{x,y,z}`
- `orientation.{pitch,roll,yaw,inclination}`
- `displacement.{x_um,y_um,z_um}`
- `battery`
- `rssi`
- `wifi_state`
- `mqtt.broker`
- `mqtt.status`
- `mqtt.publish.*`
- `ap_ip`

Notes:

- current firmware only handles connect/disconnect events
- there is no request/response command protocol over WebSocket
- use it as a server-push live feed only

## 8. POST Configuration Endpoints

### 8.1 `POST /api/config`

Minimal WiFi save endpoint.

Fields:

- `ssid` required
- `password` optional

Response:

```json
{
  "status": "ok",
  "message": "WiFi config saved. STA reconnect scheduled."
}
```

### 8.2 `POST /api/network_config`

Full network provisioning endpoint.

Fields:

- `ssid`
- `password`
- `ap_enabled`
- `ap_ssid`
- `ap_password`
- `sta_use_static_ip`
- `sta_static_ip`
- `sta_gateway`
- `sta_subnet`
- `sta_dns1`
- `sta_dns2`

Validation:

- AP password must be at least `8` chars if provided
- static IP mode requires `ip`, `gateway`, `subnet`

### 8.3 `POST /api/mqtt_config`

Fields:

- `broker` required
- `port`
- `client_id`
- `username`
- `password`
- `topic_publish`
- `topic_fft_x`
- `topic_fft_y`
- `topic_fft_z`
- `topic_subscribe`
- `topic_ack`
- `topic_result`
- `publish_interval_s`
- `use_tls`
- `protocol`

Notes:

- `protocol=mqtts` also enables TLS
- publish interval is clamped to `1..3600`
- blank topics are replaced with defaults

### 8.4 `POST /api/mems_config`

Fields:

- `rate_hz`
- `range_g`
- `offset_x`
- `offset_y`
- `offset_z`
- `int_threshold_mg`
- `int_enabled`
- `min_rms_g`
- `min_peak_g`
- `noise_floor_db`
- `deadband_g`
- `min_freq_hz`
- `max_freq_hz`
- `sleep_interval_sec`

Normalization:

- `rate_hz` is quantized to `400`, `800`, or `1600`
- `sleep_interval_sec` is clamped to `60..86400`

### 8.5 `POST /api/operate_config`

Fields:

- `publish_interval_s`
- `int_threshold_mg`
- `int_enabled`
- `sleep_interval_sec`
- `publish_on_vibration_trigger`
- `publish_vibration_threshold_mm_s`
- `log_enabled`
- `debug_log_wifi`
- `debug_log_mqtt`
- `debug_log_mems`
- `debug_log_power`
- `debug_log_web`
- `debug_log_battery`
- `debug_log_operate`
- `debug_log_system`

Use this endpoint for:

- operational mode settings
- trigger-based publish behavior
- debug logging switches

### 8.6 `POST /api/system_config`

Currently only updates:

- `log_enabled`

### 8.7 `POST /api/ap_config`

Fields:

- `ap_ssid`
- `ap_password`

Behavior:

- AP config is saved
- AP is restarted immediately without reboot

### 8.8 `POST /api/reboot`

Response:

```json
{ "status": "rebooting" }
```

### 8.9 `POST /api/reset`

Fields:

- `clear_spiffs=true` optional

Response:

```json
{ "status": "resetting" }
```

Behavior:

- clears config file
- optionally formats SPIFFS
- restores WiFi settings
- reboots device

## 9. MQTT Integration Contract

### 9.1 Main telemetry publish

Topic:

- `mqtt.topic_publish`

Payload shape:

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

### 9.2 FFT command request

Accepted request formats on `mqtt.topic_subscribe`:

Plain text:

```text
fft_x
```

JSON:

```json
{
  "action": "fft_x",
  "request_id": "req-001",
  "step_hz": 10
}
```

Also accepted:

- `command` instead of `action`
- `resolution_hz` instead of `step_hz`

Axis commands:

- `fft_x`
- `fft_y`
- `fft_z`

Allowed FFT step values:

- `10`, `15`, `20`, `25`, `30`, `35`, `40`, `45`, `50`

### 9.3 FFT ack payload

Published to `mqtt.topic_ack` or fallback derived topic.

Example:

```json
{
  "status": "accepted",
  "axis": "x",
  "request_id": "req-001",
  "detail": "queued_for_3_rounds",
  "timestamp": 123456
}
```

Observed statuses in current code:

- `accepted`
- `processing`
- `done`
- `already_done`
- `error`

### 9.4 FFT result payload

Published to:

- `mqtt.topic_result`
- axis-specific topic `mqtt.topic_fft_x|y|z`

Example:

```json
{
  "timestamp": 1715772000,
  "axis": "x",
  "request_id": "req-001",
  "step_hz": 10,
  "data": {
    "x_freq_hz": [10.00, 20.00, 30.00],
    "x_amplitude_mm_s": [0.12, 0.08, 0.03]
  }
}
```

## 10. Runtime Caveats For Mobile

- Some requests over STA IP pause MQTT temporarily for heap protection.
- Some endpoints may return `503` with `{"error":"low memory"}`.
- `GET /api/dashboard` can temporarily report `has_data=false`.
- FFT endpoints depend on the latest sensor analysis and may return unavailable errors.
- Battery reading timing differs between normal mode and debug mode.

## 11. Recommended Mobile Error Handling

Handle at least these cases:

- `400` validation errors from config endpoints
- `500` save/apply failures
- `503 low memory`
- reboot/reset responses that immediately drop the connection
- discovery results where `sta_connected=false`
- missing FFT data during startup or sleep transitions
