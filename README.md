# ESP32 IoT Vibration Monitoring Module (VIOT)

Firmware for an ESP32-based vibration monitor using an ADXL345 accelerometer, WiFi AP/STA networking, a web configuration UI, MQTT/MQTTS publishing, FFT-on-demand over MQTT, and deep sleep wakeup by timer or ADXL345 interrupt.

This README reflects the current code in `src/` and `include/`.

## Current Status

- ADXL345 acquisition and vibration analysis are implemented.
- WiFi STA + fallback/config AP are implemented.
- Web configuration pages and JSON APIs are implemented.
- MQTT and MQTTS are implemented.
- MQTT command-driven FFT snapshot publishing is implemented.
- Deep sleep with timer wake and ADXL345 INT1 wake is implemented.
- BLE is not implemented in the current codebase.

## Hardware Mapping

Pin assignments come from [include/config.h](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\include\config.h:7>).

- `GPIO27`: mode switch, `HIGH = debug`, `LOW = normal`
- `GPIO32`: status LED
- `GPIO33`: MQTT activity LED
- `GPIO34`: battery ADC input
- `GPIO25`: ADXL345 `INT1`
- `GPIO26`: ADXL345 `INT2`
- `GPIO21`: I2C SDA
- `GPIO22`: I2C SCL

Notes:

- ADXL345 wake uses `INT1` for deep sleep wake.
- ADXL345 interrupt outputs are configured active-low in the firmware.

## Build Environment

PlatformIO settings are defined in [platformio.ini](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\platformio.ini:1>).

- Framework: Arduino
- Platform: `espressif32`
- Board setting currently used by PlatformIO: `esp32dev`
- Serial monitor: `115200`
- Filesystem: `SPIFFS`

Build:

```bash
platformio run
```

Upload firmware:

```bash
platformio run --target upload
```

Upload SPIFFS web assets:

```bash
platformio run --target uploadfs
```

Serial monitor:

```bash
platformio device monitor --baud 115200
```

## Main Features

- ADXL345 sampling at `400`, `800`, or `1600` Hz
- Sample window size: `1024` samples
- RMS acceleration with high-pass filtering
- Signal validation using configurable `min_rms_g` and `min_peak_g`
- Frequency estimation by zero-crossing for valid signals
- FFT spectrum generation for dashboard and MQTT command responses
- Battery voltage monitoring
- WiFi STA connection with AP fallback/service mode
- MQTT publish of main telemetry
- MQTT subscribe command channel for FFT requests
- Deep sleep wake by timer and ADXL345 activity interrupt

## Runtime Behavior

### Normal Mode

When `GPIO27` is `LOW`:

- deep sleep is enabled
- the device wakes, connects WiFi, connects MQTT if configured, publishes, then sleeps again
- after repeated motion wakes, motion wake can be temporarily suppressed
- motion wake can later be re-enabled after clean timer wakes

### Debug Mode

When `GPIO27` is `HIGH`:

- deep sleep is disabled
- serial logs are verbose
- AP service mode is allowed while STA is not connected
- MEMS, WiFi, MQTT, heap, and wake-policy logs are visible

## WiFi Behavior

WiFi logic is implemented in [src/wifi_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\wifi_handler.cpp:23>).

- STA connection is attempted when `wifi.ssid` is configured.
- Retry limit is `5` attempts.
- Connection timeout per attempt is `5` seconds.
- In debug mode, AP service mode may run while STA is unavailable.
- When STA connects successfully, SoftAP is stopped.

Default AP behavior:

- AP SSID defaults to `VIOT_XXYY`
- AP password defaults to `12345678`
- AP IP is typically `192.168.4.1`

## Web UI and APIs

The web server is implemented in [src/web_server.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\web_server.cpp:177>) and serves assets from `data/www/`.

Main pages:

- `/`
- `/index.html`
- `/mqtt_setting.html`
- `/network_setting.html`
- `/mems_setting.html`
- `/system_setting.html`
- `/fft_chart.html`
- `/mqtt_log`

Main APIs:

- `GET /api/config`
- `POST /api/config`
- `POST /api/mqtt_config`
- `POST /api/network_config`
- `POST /api/mems_config`
- `POST /api/system_config`
- `POST /api/ap_config`
- `GET /api/status`
- `GET /api/dashboard`
- `GET /api/fft_spectrum`
- `GET /api/fft_csv`
- `GET /api/mqtt_publish_summary`
- `GET /api/scan_ssid`
- `POST /api/reboot`
- `POST /api/reset`
- `WS /ws`

Important runtime detail:

- when pages or APIs are accessed over the STA IP, MQTT may be paused temporarily to reduce heap pressure during web access

## Configuration File

Configuration is stored in SPIFFS at `/config.json`.

The default config is created in [src/storage.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\storage.cpp:139>).

Current JSON structure:

```json
{
  "wifi": {
    "ssid": "",
    "password": "",
    "ap_enabled": true,
    "ap_ssid": "",
    "ap_password": "12345678",
    "sta_use_static_ip": false,
    "sta_static_ip": "",
    "sta_gateway": "",
    "sta_subnet": "",
    "sta_dns1": "",
    "sta_dns2": ""
  },
  "mqtt": {
    "broker": "broker.hivemq.com",
    "port": 1883,
    "client_id": "ESP32_VIOT",
    "username": "",
    "password": "",
    "topic_publish": "viot/vibration",
    "topic_fft_x": "viot/vibration/fft/x",
    "topic_fft_y": "viot/vibration/fft/y",
    "topic_fft_z": "viot/vibration/fft/z",
    "topic_subscribe": "viot/config",
    "topic_ack": "viot/config/ack",
    "topic_result": "viot/config/result",
    "publish_interval_sec": 60,
    "use_tls": false,
    "aws_iot_enabled": false
  },
  "adxl345": {
    "rate_hz": 1600,
    "range_g": 16,
    "offset_x": 0.0,
    "offset_y": 0.0,
    "offset_z": 0.0,
    "int_threshold_mg": 250,
    "int_enabled": true
  },
  "vibration": {
    "min_rms_g": 0.02,
    "min_peak_g": 0.05,
    "noise_floor_db": -20.0,
    "deadband_g": 0.005,
    "min_freq_hz": 5.0,
    "max_freq_hz": 1000.0
  },
  "power": {
    "sleep_enabled": true,
    "sleep_interval_sec": 3600,
    "log_enabled": true
  }
}
```

## MQTT Topics and Payloads

Default MQTT topics are defined in [src/storage.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\storage.cpp:153>).

- main publish: `viot/vibration`
- FFT X: `viot/vibration/fft/x`
- FFT Y: `viot/vibration/fft/y`
- FFT Z: `viot/vibration/fft/z`
- command subscribe: `viot/config`
- command ack: `viot/config/ack`
- command result: `viot/config/result`

### Main telemetry payload

The main payload is built in [src/mqtt_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\mqtt_handler.cpp:513>).

Example shape:

```json
{
  "timestamp": 123456,
  "data": {
    "accel_x_rms": 0.012,
    "accel_y_rms": 0.011,
    "accel_z_rms": 0.016,
    "vibration_x_rms_mm_s": 0.0,
    "vibration_y_rms_mm_s": 0.0,
    "vibration_z_rms_mm_s": 0.0,
    "vibration_freq_x_hz": 0.0,
    "vibration_freq_y_hz": 0.0,
    "vibration_freq_z_hz": 0.0,
    "displacement_x_um": 0.0,
    "displacement_y_um": 0.0,
    "displacement_z_um": 0.0,
    "pitch_deg": -17.4,
    "roll_deg": -5.6,
    "yaw_deg": 0.0,
    "battery_v": 3.8,
    "wifi_rssi": -55,
    "uptime_ms": 12345
  }
}
```

### FFT command flow

FFT command handling is implemented in [src/mqtt_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\mqtt_handler.cpp:484>).

- device listens on `topic_subscribe`
- command requests an axis and FFT step
- firmware waits for `3` MEMS processing rounds
- a snapshot is published to:
  - `topic_result`
  - selected axis topic (`topic_fft_x`, `topic_fft_y`, or `topic_fft_z`)
- status/ack messages are published to `topic_ack`

## MQTTS Note

TLS mode is supported, but the current implementation uses:

```cpp
secure_client.setInsecure();
```

See [src/mqtt_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\mqtt_handler.cpp:102>).

That means:

- encryption is used
- server certificate verification is currently skipped
- logs will show `Skipping SSL Verification. INSECURE!`

## Sensor and Analysis Details

From [include/config.h](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\include\config.h:25>) and [src/sensors.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\sensors.cpp:223>):

- sample count: `1024`
- default data rate: `1600 Hz`
- FFT display points: `120`
- default MQTT FFT points per snapshot: `32`
- RMS high-pass cutoff: `10 Hz`
- default valid vibration thresholds:
  - `min_rms_g = 0.02`
  - `min_peak_g = 0.05`
  - `min_freq_hz = 5`
  - `max_freq_hz = 1000`

Important behavior:

- velocity and displacement are only reported for signals that pass validation
- weak/noisy vibration commonly appears as `valid=NO`, with frequency, velocity, and displacement forced to zero
- pitch/roll are always derived from averaged acceleration

## Power and Wake Policy

Deep sleep is handled in [src/power_management.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\power_management.cpp:25>).

Current policy:

- normal wake sources are timer and ADXL345 `INT1`
- motion wake may be suppressed after `5` consecutive motion wakes
- motion wake may be re-enabled after `2` consecutive clean timer wakes while suppressed
- wake-policy logs now include:
  - pre-state
  - event decision
  - action taken
  - post-state

Main loop sleep trigger is in [src/main.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\main.cpp:625>):

- in normal mode
- after at least one successful MQTT publish
- if FFT streaming is not active
- if no pending FFT command work exists

## FreeRTOS Tasks

Task creation and scheduling are in [src/main.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\main.cpp:500>) and sizes in [include/config.h](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\include\config.h:85>).

| Task | Priority | Core | Stack |
|------|----------|------|-------|
| `MEMS` | 8 | 0 | 8192 |
| `WiFi` | 5 | 1 | 6144 |
| `MQTT` | 6 | 1 | 6144 |
| `MQTT-Pub` | 7 | 1 | 5120 |
| `Web` | 4 | 1 | 6144 |
| `ADC` | 2 | 1 | 3072 |

Practical timing:

- MEMS task runs about every `2500 ms`
- ADC task runs every `30000 ms`
- MQTT publish task checks every `5000 ms`
- main MQTT publish interval is config-driven

## Known Limitations

- BLE is not implemented.
- MQTTS certificate verification is not implemented yet.
- PlatformIO environment name and hardware naming in older docs may not match the current pin map exactly.
- The ESP-IDF log `Incorrect wakeup source (3) to disable` may still appear when disabling EXT1 wake; this is a runtime cleanup issue, not a documented feature.

## Troubleshooting

### No vibration values even though MEMS data exists

Check `min_rms_g` and `min_peak_g` in the vibration config. The firmware intentionally zeroes velocity, displacement, and vibration frequency for signals below the validation thresholds.

### STA web access interrupts MQTT behavior

This is expected. The web server may temporarily pause MQTT activity during STA-hosted page/API access to reduce heap pressure.

### MQTTS connects but logs `INSECURE`

This is expected with the current TLS implementation because certificate verification is disabled.

### Device wakes by timer but motion wake does not return

Check the wake-policy logs:

- `ADXL345 INT boot state: before_init=... after_init=... policy=...`
- `Wake policy pre: ...`
- `Wake policy event: ...`
- `Wake policy post: ...`

These logs are the quickest way to see whether timer wakes are being counted or ignored.

## Related Files

- [src/main.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\main.cpp>)
- [src/sensors.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\sensors.cpp>)
- [src/wifi_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\wifi_handler.cpp>)
- [src/mqtt_handler.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\mqtt_handler.cpp>)
- [src/web_server.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\web_server.cpp>)
- [src/power_management.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\power_management.cpp>)
- [src/storage.cpp](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\src\storage.cpp>)

## License

MIT License. See [LICENSE](<c:\Users\User\Documents\PlatformIO\Projects\iotmodule2\LICENSE>).
