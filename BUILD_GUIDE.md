# VIOT Build Guide

Build and deployment guide for the current firmware, with emphasis on producing a testable device for Mobile App development.

This guide reflects:

- `platformio.ini`
- `src/`
- `data/www/`

## 1. Toolchain Summary

Current PlatformIO environment from `platformio.ini`:

- environment: `esp32-s3-devkitc-1`
- platform: `espressif32`
- board: `esp32dev`
- framework: Arduino
- monitor speed: `115200`
- upload speed: `460800`
- filesystem: SPIFFS
- partition table: `default.csv`

Library dependencies:

- `ArduinoJson`
- `pubsubclient`
- `ESP Async WebServer`
- `AsyncTCP`
- `arduinoFFT`

Important note:

- The environment name says `esp32-s3-devkitc-1`, but the configured board is currently `esp32dev`.
- Keep this mismatch in mind when validating real hardware assumptions.

## 2. Prerequisites

Install one of:

### Option A: VS Code + PlatformIO

1. Install Visual Studio Code.
2. Install the `PlatformIO IDE` extension.
3. Open this project folder in VS Code.

### Option B: PlatformIO CLI

```bash
pip install platformio
pio --version
```

## 3. Project Layout You Should Know

- `src/` firmware source
- `include/` headers and runtime data structures
- `data/www/` static web UI assets served from SPIFFS
- `config.example.json` sample config shape
- `platformio.ini` build settings

## 4. First Build

From the project root:

```bash
pio run
```

Expected outcome:

- firmware compiles successfully
- `.pio/` build artifacts are created

If the first build is slow, that is normal because PlatformIO will fetch dependencies and toolchains.

## 5. Upload Firmware

Auto-detect port:

```bash
pio run --target upload
```

Specify port explicitly if needed:

```bash
pio run --target upload --upload-port COM3
```

To list ports:

```bash
pio device list
```

## 6. Upload Web Assets To SPIFFS

The HTTP UI depends on files in `data/www/`, so for a complete device bring-up you should also upload the filesystem image:

```bash
pio run --target uploadfs
```

This step matters for Mobile App testing because:

- provisioning pages live in SPIFFS
- browser/manual fallback tools live in SPIFFS
- `GET /` and related pages depend on it

## 7. Open Serial Monitor

```bash
pio device monitor --baud 115200
```

Useful boot logs to look for:

- SPIFFS mounted
- ADXL345 initialized
- WiFi handler initializing
- MQTT handler initialized
- Web server started
- mDNS or UDP discovery ready

## 8. Typical Flash Sequence

For a clean firmware + UI refresh:

```bash
pio run
pio run --target upload
pio run --target uploadfs
pio device monitor --baud 115200
```

## 9. Runtime Modes To Validate

### Debug mode

Condition:

- `GPIO27 = HIGH`

Behavior:

- deep sleep disabled
- verbose logs available
- AP fallback can stay available while STA is not connected
- battery refresh runs periodically

Recommended for:

- Mobile App development
- API inspection
- discovery tests
- WiFi provisioning tests

### Normal mode

Condition:

- `GPIO27 = LOW`

Behavior:

- deep sleep enabled
- battery sampled once near startup for that wake cycle
- device may connect, publish, and go back to sleep

Recommended for:

- power behavior validation
- production-like MQTT publish flow

## 10. Initial Bring-Up Checklist

After upload, validate in this order:

1. Serial boots cleanly without repeated crash/reset loop.
2. Sensor init succeeds.
3. SPIFFS mounts successfully.
4. Device exposes either:
   - STA connection, or
   - AP fallback when allowed
5. `GET /api/discover` responds.
6. `GET /api/status` responds.
7. `GET /api/config` responds.
8. `GET /api/dashboard` eventually returns `has_data=true`.

## 11. Provisioning Paths

### Path A: Use device AP and web UI

Use when:

- device has no STA credentials
- you want a quick manual setup before testing the Mobile App

Typical AP defaults:

- SSID: `VIOT_XXXX`
- password: `12345678`
- IP: `192.168.4.1`

Then:

1. connect phone/laptop to the device AP
2. open `http://192.168.4.1`
3. save WiFi and MQTT settings
4. let the device reconnect

### Path B: Provision with API

Use:

- `POST /api/network_config`
- `POST /api/mqtt_config`
- `POST /api/mems_config`
- `POST /api/operate_config`

Important:

- current firmware expects form fields, not JSON body

## 12. Quick API Smoke Test

Once the device is reachable on IP `192.168.1.45`, these are good checks:

```bash
curl http://192.168.1.45/api/discover
curl http://192.168.1.45/api/status
curl http://192.168.1.45/api/config
curl "http://192.168.1.45/api/fft_spectrum?axis=x"
```

Example config update using form encoding:

```bash
curl -X POST http://192.168.1.45/api/network_config ^
  -H "Content-Type: application/x-www-form-urlencoded" ^
  -d "ssid=OfficeWiFi&password=secret123&ap_enabled=1"
```

## 13. MQTT Validation

If MQTT is configured, validate:

1. broker connection succeeds
2. telemetry appears on `topic_publish`
3. command topic receives FFT requests
4. ack topic returns status payloads
5. result topic and axis topic receive FFT payloads

Simple FFT request example:

Topic:

- `viot/config`

Payload:

```json
{
  "action": "fft_x",
  "request_id": "mobile-test-001",
  "step_hz": 10
}
```

## 14. Common Development Commands

Build:

```bash
pio run
```

Clean build:

```bash
pio run --target clean
pio run
```

Verbose build:

```bash
pio run -v
```

Verbose upload:

```bash
pio run --target upload -v
```

## 15. Common Issues

### Build succeeds but web pages are missing

Cause:

- firmware uploaded, SPIFFS not uploaded

Fix:

```bash
pio run --target uploadfs
```

### Device is online but Mobile App cannot find it by mDNS

Cause:

- mDNS works only when STA is connected

Fix:

- try UDP discovery
- or connect directly to known IP

### Mobile App posts JSON and nothing changes

Cause:

- current endpoints parse form fields, not JSON body

Fix:

- switch to `application/x-www-form-urlencoded` or `multipart/form-data`

### `503 {"error":"low memory"}`

Cause:

- heap protection in API handlers

Fix:

- retry after a short delay
- reduce simultaneous polling
- avoid opening many heavy pages while MQTT/TLS work is active

### MQTT disconnects during web/API access

Cause:

- firmware intentionally pauses MQTT during some STA-served web requests to protect heap

Fix:

- treat it as temporary
- avoid interpreting it as a fatal network error immediately

## 16. Recommended Setup For Mobile App Development

Use this setup when the goal is fast iteration:

1. Run the device in debug mode with `GPIO27 = HIGH`.
2. Upload both firmware and SPIFFS.
3. Join the same LAN as the phone/emulator/dev machine.
4. Validate `GET /api/discover`, `GET /api/status`, and `GET /api/dashboard`.
5. Keep a serial monitor open while testing discovery and config flows.
6. Use the web UI only as a fallback reference, not as the primary test path.

## 17. Release Checklist For A Test Device

Before handing the device to the Mobile team:

1. Confirm build and upload succeed.
2. Confirm `uploadfs` completed.
3. Confirm sensor initializes.
4. Confirm discovery works by at least one method.
5. Confirm `GET /api/config` returns expected defaults or test config.
6. Confirm at least one config `POST` endpoint applies changes successfully.
7. Confirm MQTT works if the app depends on it.
8. Record:
   - firmware build date/time from `GET /api/discover`
   - device hostname
   - current STA IP or AP IP
   - configured MQTT topics
