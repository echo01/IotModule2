# ESP32 IoT Vibration Monitoring Module (VIOT)

Firmware for an ESP32-based vibration monitor using an ADXL345 accelerometer, WiFi AP/STA networking, a web configuration UI, MQTT/MQTTS publishing, FFT-on-demand over MQTT, and deep sleep wakeup by timer or ADXL345 interrupt.

This README reflects the current code in `src/` and `include/`.

## English

### Current Status

- ADXL345 acquisition and vibration analysis are implemented.
- WiFi STA + fallback/config AP are implemented.
- Web configuration pages and JSON APIs are implemented.
- MQTT and MQTTS are implemented.
- MQTT command-driven FFT snapshot publishing is implemented.
- Deep sleep with timer wake and ADXL345 INT1 wake is implemented.
- BLE is not implemented in the current codebase.

### Hardware Mapping

Pin assignments come from [include/config.h](include/config.h).

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

### Build Environment

PlatformIO settings are defined in [platformio.ini](platformio.ini).

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

### Main Features

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

### Runtime Behavior

#### Normal Mode

When `GPIO27` is `LOW`:

- deep sleep is enabled
- the device wakes, connects WiFi, connects MQTT if configured, publishes, then sleeps again
- after repeated motion wakes, motion wake can be temporarily suppressed
- motion wake can later be re-enabled after clean timer wakes

#### Debug Mode

When `GPIO27` is `HIGH`:

- deep sleep is disabled
- serial logs are verbose
- AP service mode is allowed while STA is not connected
- MEMS, WiFi, MQTT, heap, and wake-policy logs are visible

### WiFi Behavior

WiFi logic is implemented in [src/wifi_handler.cpp](src/wifi_handler.cpp).

- STA connection is attempted when `wifi.ssid` is configured.
- Retry limit is `5` attempts.
- Connection timeout per attempt is `5` seconds.
- In debug mode, AP service mode may run while STA is unavailable.
- When STA connects successfully, SoftAP is stopped.

Default AP behavior:

- AP SSID defaults to `VIOT_XXYY`
- AP password defaults to `12345678`
- AP IP is typically `192.168.4.1`

### Web UI and APIs

The web server is implemented in [src/web_server.cpp](src/web_server.cpp) and serves assets from `data/www/`.

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

### Configuration File

Configuration is stored in SPIFFS at `/config.json`.

The default config is created in [src/storage.cpp](src/storage.cpp).

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

### MQTT Topics and Payloads

Default MQTT topics are defined in [src/storage.cpp](src/storage.cpp).

- main publish: `viot/vibration`
- FFT X: `viot/vibration/fft/x`
- FFT Y: `viot/vibration/fft/y`
- FFT Z: `viot/vibration/fft/z`
- command subscribe: `viot/config`
- command ack: `viot/config/ack`
- command result: `viot/config/result`

#### Main Telemetry Payload

The main payload is built in [src/mqtt_handler.cpp](src/mqtt_handler.cpp).

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

#### Payload Field Units and Example

Example payload from a real publish:

```json
{
  "timestamp": 25110,
  "data": {
    "accel_x_rms": 0.009932,
    "accel_y_rms": 0.011381,
    "accel_z_rms": 0.016495,
    "vibration_x_rms_mm_s": 0,
    "vibration_y_rms_mm_s": 0,
    "vibration_z_rms_mm_s": 0,
    "vibration_freq_x_hz": 0,
    "vibration_freq_y_hz": 0,
    "vibration_freq_z_hz": 0,
    "displacement_x_um": 0,
    "displacement_y_um": 0,
    "displacement_z_um": 0,
    "pitch_deg": -40.15989,
    "roll_deg": -5.715371,
    "yaw_deg": 0,
    "battery_v": 3.605421,
    "wifi_rssi": -86,
    "uptime_ms": 25574
  }
}
```

Field meanings and units:

- `timestamp`
  - Unit: `ms`
  - Meaning: publish timestamp derived from `analysis.timestamp_us / 1000`
- `accel_x_rms`, `accel_y_rms`, `accel_z_rms`
  - Unit: `g`
  - Meaning: RMS acceleration per axis after the firmware applies a `10 Hz` high-pass filter
  - Example reading:
    - `accel_x_rms = 0.009932 g`
    - `accel_y_rms = 0.011381 g`
    - `accel_z_rms = 0.016495 g`
  - Approximate conversion to SI acceleration:
    - `0.009932 g x 9.81 = 0.0974 m/s^2`
    - `0.011381 g x 9.81 = 0.1116 m/s^2`
    - `0.016495 g x 9.81 = 0.1618 m/s^2`
- `vibration_x_rms_mm_s`, `vibration_y_rms_mm_s`, `vibration_z_rms_mm_s`
  - Unit: `mm/s`
  - Meaning: RMS vibration velocity per axis
  - In this example all three values are `0` because the acceleration RMS values are below the default validation threshold `min_rms_g = 0.02`
- `vibration_freq_x_hz`, `vibration_freq_y_hz`, `vibration_freq_z_hz`
  - Unit: `Hz`
  - Meaning: dominant vibration frequency per axis estimated from zero-crossings
  - In this example all three values are `0` because the signal did not pass vibration validation, so frequency output is suppressed
- `displacement_x_um`, `displacement_y_um`, `displacement_z_um`
  - Unit: `um`
  - Meaning: vibration displacement per axis
  - In this example all three values are `0` because displacement is only calculated when both vibration velocity is valid and frequency is greater than zero
- `pitch_deg`
  - Unit: `degree`
  - Meaning: tilt angle around the Y/Z gravity plane, computed from averaged acceleration samples
  - Example reading: `-40.15989 deg`
- `roll_deg`
  - Unit: `degree`
  - Meaning: tilt angle around the X/Z gravity plane, computed from averaged acceleration samples
  - Example reading: `-5.715371 deg`
- `yaw_deg`
  - Unit: `degree`
  - Meaning: yaw output placeholder
  - Current firmware behavior: always `0`
- `battery_v`
  - Unit: `V`
  - Meaning: measured battery voltage
  - Example reading: `3.605421 V`
- `wifi_rssi`
  - Unit: `dBm`
  - Meaning: Wi-Fi received signal strength
  - Example reading: `-86 dBm`
- `uptime_ms`
  - Unit: `ms`
  - Meaning: device uptime since boot
  - Example reading: `25574 ms` = about `25.574 s`

How to interpret this example:

- The device is measuring low vibration on all three axes because all `accel_*_rms` values are below `0.02 g`
- Since the signal is below the default validation threshold, the firmware forces vibration velocity, vibration frequency, and displacement to `0`
- Orientation is still available because `pitch_deg` and `roll_deg` are calculated from averaged acceleration, not from vibration validation
- Battery level is about `3.61 V` and Wi-Fi signal is weak at `-86 dBm`

#### FFT Command Flow

FFT command handling is implemented in [src/mqtt_handler.cpp](src/mqtt_handler.cpp).

- device listens on `topic_subscribe`
- command requests an axis and FFT step
- firmware waits for `3` MEMS processing rounds
- a snapshot is published to:
  - `topic_result`
  - selected axis topic (`topic_fft_x`, `topic_fft_y`, or `topic_fft_z`)
- status/ack messages are published to `topic_ack`

### MQTTS Note

TLS mode is supported, but the current implementation uses:

```cpp
secure_client.setInsecure();
```

That means:

- encryption is used
- server certificate verification is currently skipped
- logs will show `Skipping SSL Verification. INSECURE!`

### Sensor and Analysis Details

From [include/config.h](include/config.h) and [src/sensors.cpp](src/sensors.cpp):

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
- weak or noisy vibration commonly appears as `valid=NO`, with frequency, velocity, and displacement forced to zero
- pitch and roll are always derived from averaged acceleration

### Power and Wake Policy

Deep sleep is handled in [src/power_management.cpp](src/power_management.cpp).

Current policy:

- normal wake sources are timer and ADXL345 `INT1`
- motion wake may be suppressed after `5` consecutive motion wakes
- motion wake may be re-enabled after `2` consecutive clean timer wakes while suppressed
- wake-policy logs now include:
  - pre-state
  - event decision
  - action taken
  - post-state

Main loop sleep trigger is in [src/main.cpp](src/main.cpp):

- in normal mode
- after at least one successful MQTT publish
- if FFT streaming is not active
- if no pending FFT command work exists

### FreeRTOS Tasks

Task creation and scheduling are in [src/main.cpp](src/main.cpp) and sizes in [include/config.h](include/config.h).

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

### Known Limitations

- BLE is not implemented.
- MQTTS certificate verification is not implemented yet.
- PlatformIO environment name and hardware naming in older docs may not match the current pin map exactly.
- The ESP-IDF log `Incorrect wakeup source (3) to disable` may still appear when disabling EXT1 wake; this is a runtime cleanup issue, not a documented feature.

### Troubleshooting

#### No vibration values even though MEMS data exists

Check `min_rms_g` and `min_peak_g` in the vibration config. The firmware intentionally zeroes velocity, displacement, and vibration frequency for signals below the validation thresholds.

#### STA web access interrupts MQTT behavior

This is expected. The web server may temporarily pause MQTT activity during STA-hosted page or API access to reduce heap pressure.

#### MQTTS connects but logs `INSECURE`

This is expected with the current TLS implementation because certificate verification is disabled.

#### Device wakes by timer but motion wake does not return

Check the wake-policy logs:

- `ADXL345 INT boot state: before_init=... after_init=... policy=...`
- `Wake policy pre: ...`
- `Wake policy event: ...`
- `Wake policy post: ...`

These logs are the quickest way to see whether timer wakes are being counted or ignored.

### Related Files

- [src/main.cpp](src/main.cpp)
- [src/sensors.cpp](src/sensors.cpp)
- [src/wifi_handler.cpp](src/wifi_handler.cpp)
- [src/mqtt_handler.cpp](src/mqtt_handler.cpp)
- [src/web_server.cpp](src/web_server.cpp)
- [src/power_management.cpp](src/power_management.cpp)
- [src/storage.cpp](src/storage.cpp)

### License

MIT License. See [LICENSE](LICENSE).

## ภาษาไทย

### สถานะปัจจุบัน

- มีการอ่านค่า ADXL345 และวิเคราะห์การสั่นแล้ว
- มี WiFi แบบ STA และ AP สำรองสำหรับตั้งค่าแล้ว
- มีหน้าเว็บตั้งค่าและ JSON API แล้ว
- มี MQTT และ MQTTS แล้ว
- มีการสั่งขอ FFT snapshot ผ่าน MQTT แล้ว
- มี deep sleep พร้อมปลุกด้วย timer และ ADXL345 INT1 แล้ว
- ยังไม่มี BLE ในโค้ดปัจจุบัน

### การแมปฮาร์ดแวร์

การกำหนดขามาจาก [include/config.h](include/config.h)

- `GPIO27`: สวิตช์โหมด, `HIGH = debug`, `LOW = normal`
- `GPIO32`: ไฟสถานะ
- `GPIO33`: ไฟแสดงกิจกรรม MQTT
- `GPIO34`: ขา ADC วัดแบตเตอรี่
- `GPIO25`: ขา `INT1` ของ ADXL345
- `GPIO26`: ขา `INT2` ของ ADXL345
- `GPIO21`: I2C SDA
- `GPIO22`: I2C SCL

หมายเหตุ:

- การปลุกจาก deep sleep ของ ADXL345 ใช้ `INT1`
- ขา interrupt ของ ADXL345 ถูกตั้งเป็น active-low ในเฟิร์มแวร์

### สภาพแวดล้อมการ build

การตั้งค่า PlatformIO อยู่ใน [platformio.ini](platformio.ini)

- Framework: Arduino
- Platform: `espressif32`
- บอร์ดที่ PlatformIO ใช้อยู่ตอนนี้: `esp32dev`
- Serial monitor: `115200`
- Filesystem: `SPIFFS`

คำสั่ง build:

```bash
platformio run
```

อัปโหลดเฟิร์มแวร์:

```bash
platformio run --target upload
```

อัปโหลดไฟล์เว็บไป SPIFFS:

```bash
platformio run --target uploadfs
```

เปิด serial monitor:

```bash
platformio device monitor --baud 115200
```

### ความสามารถหลัก

- อ่านค่า ADXL345 ที่ `400`, `800` หรือ `1600` Hz
- ขนาดหน้าต่างตัวอย่าง `1024` samples
- คำนวณ RMS acceleration พร้อม high-pass filtering
- ตรวจสอบความถูกต้องของสัญญาณด้วย `min_rms_g` และ `min_peak_g`
- ประเมินความถี่ด้วย zero-crossing เมื่อสัญญาณผ่านเกณฑ์
- สร้าง FFT spectrum สำหรับ dashboard และการตอบคำสั่ง MQTT
- วัดแรงดันแบตเตอรี่
- เชื่อมต่อ WiFi แบบ STA พร้อม AP สำรองหรือ service mode
- publish telemetry หลักผ่าน MQTT
- subscribe คำสั่ง MQTT สำหรับขอ FFT
- ปลุกจาก deep sleep ด้วย timer และ interrupt การเคลื่อนไหวของ ADXL345

### พฤติกรรมขณะรัน

#### โหมดปกติ

เมื่อ `GPIO27` เป็น `LOW`:

- เปิดใช้งาน deep sleep
- อุปกรณ์จะตื่นขึ้นมา เชื่อม WiFi เชื่อม MQTT ถ้ามีการตั้งค่า publish ข้อมูล แล้วกลับไปนอน
- ถ้ามี motion wake ติดต่อกันหลายครั้ง ระบบอาจกดการปลุกจาก motion ชั่วคราว
- การปลุกจาก motion สามารถเปิดกลับได้หลังจากมี timer wake ที่ปกติ

#### โหมดดีบัก

เมื่อ `GPIO27` เป็น `HIGH`:

- ปิด deep sleep
- log ทาง serial จะละเอียดขึ้น
- อนุญาต AP service mode เมื่อ STA ยังไม่เชื่อม
- มองเห็น log ของ MEMS, WiFi, MQTT, heap และ wake policy

### พฤติกรรม WiFi

ลอจิก WiFi อยู่ใน [src/wifi_handler.cpp](src/wifi_handler.cpp)

- จะพยายามเชื่อมแบบ STA เมื่อมีการตั้ง `wifi.ssid`
- จำนวน retry สูงสุด `5` ครั้ง
- timeout ต่อครั้ง `5` วินาที
- ในโหมดดีบัก AP service mode อาจทำงานได้แม้ STA ยังไม่พร้อม
- เมื่อ STA เชื่อมสำเร็จ SoftAP จะถูกปิด

พฤติกรรม AP เริ่มต้น:

- SSID เริ่มต้นเป็น `VIOT_XXYY`
- รหัสผ่านเริ่มต้นเป็น `12345678`
- IP ของ AP โดยทั่วไปคือ `192.168.4.1`

### Web UI และ API

เว็บเซิร์ฟเวอร์อยู่ใน [src/web_server.cpp](src/web_server.cpp) และเสิร์ฟไฟล์จาก `data/www/`

หน้าหลัก:

- `/`
- `/index.html`
- `/mqtt_setting.html`
- `/network_setting.html`
- `/mems_setting.html`
- `/system_setting.html`
- `/fft_chart.html`
- `/mqtt_log`

API หลัก:

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

รายละเอียดสำคัญระหว่างรัน:

- เมื่อมีการเปิดหน้าเว็บหรือเรียก API ผ่าน STA IP ระบบอาจ pause MQTT ชั่วคราวเพื่อลดแรงกดดันด้าน heap ระหว่างใช้งานเว็บ

### ไฟล์คอนฟิก

คอนฟิกถูกเก็บใน SPIFFS ที่ `/config.json`

คอนฟิกเริ่มต้นถูกสร้างใน [src/storage.cpp](src/storage.cpp)

โครงสร้าง JSON ปัจจุบัน:

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

### MQTT Topics และ Payloads

MQTT topic เริ่มต้นถูกกำหนดใน [src/storage.cpp](src/storage.cpp)

- publish หลัก: `viot/vibration`
- FFT X: `viot/vibration/fft/x`
- FFT Y: `viot/vibration/fft/y`
- FFT Z: `viot/vibration/fft/z`
- subscribe คำสั่ง: `viot/config`
- ack คำสั่ง: `viot/config/ack`
- result ของคำสั่ง: `viot/config/result`

#### Main telemetry payload

payload หลักถูกประกอบใน [src/mqtt_handler.cpp](src/mqtt_handler.cpp)

รูปแบบตัวอย่าง:

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

#### คำอธิบาย field และหน่วยของ payload

ตัวอย่าง payload จากการ publish จริง:

```json
{
  "timestamp": 25110,
  "data": {
    "accel_x_rms": 0.009932,
    "accel_y_rms": 0.011381,
    "accel_z_rms": 0.016495,
    "vibration_x_rms_mm_s": 0,
    "vibration_y_rms_mm_s": 0,
    "vibration_z_rms_mm_s": 0,
    "vibration_freq_x_hz": 0,
    "vibration_freq_y_hz": 0,
    "vibration_freq_z_hz": 0,
    "displacement_x_um": 0,
    "displacement_y_um": 0,
    "displacement_z_um": 0,
    "pitch_deg": -40.15989,
    "roll_deg": -5.715371,
    "yaw_deg": 0,
    "battery_v": 3.605421,
    "wifi_rssi": -86,
    "uptime_ms": 25574
  }
}
```

ความหมายของแต่ละฟิลด์และหน่วย:

- `timestamp`
  - หน่วย: `ms`
  - ความหมาย: เวลา timestamp ของข้อมูลที่ส่งออก โดยมาจาก `analysis.timestamp_us / 1000`
- `accel_x_rms`, `accel_y_rms`, `accel_z_rms`
  - หน่วย: `g`
  - ความหมาย: ค่า RMS ของความเร่งในแต่ละแกน หลังจากเฟิร์มแวร์ผ่าน high-pass filter ที่ `10 Hz`
  - ตัวอย่างค่าจาก payload นี้:
    - `accel_x_rms = 0.009932 g`
    - `accel_y_rms = 0.011381 g`
    - `accel_z_rms = 0.016495 g`
  - ถ้าต้องการแปลงเป็น `m/s^2`:
    - `0.009932 x 9.81 = 0.0974 m/s^2`
    - `0.011381 x 9.81 = 0.1116 m/s^2`
    - `0.016495 x 9.81 = 0.1618 m/s^2`
- `vibration_x_rms_mm_s`, `vibration_y_rms_mm_s`, `vibration_z_rms_mm_s`
  - หน่วย: `mm/s`
  - ความหมาย: ค่า RMS ของความเร็วการสั่นในแต่ละแกน
  - ใน payload นี้เป็น `0` ทั้งหมด เพราะค่า `accel_*_rms` ยังต่ำกว่า threshold เริ่มต้นของระบบคือ `min_rms_g = 0.02`
- `vibration_freq_x_hz`, `vibration_freq_y_hz`, `vibration_freq_z_hz`
  - หน่วย: `Hz`
  - ความหมาย: ความถี่การสั่นหลักของแต่ละแกน คำนวณจาก zero-crossing
  - ใน payload นี้เป็น `0` ทั้งหมด เพราะสัญญาณไม่ผ่านเงื่อนไข validation จึงไม่ส่งค่าความถี่ออกมา
- `displacement_x_um`, `displacement_y_um`, `displacement_z_um`
  - หน่วย: `um`
  - ความหมาย: ระยะกระจัดของการสั่นในแต่ละแกน
  - ใน payload นี้เป็น `0` ทั้งหมด เพราะจะคำนวณได้ก็ต่อเมื่อ vibration velocity ถูกยอมรับว่า valid และความถี่มากกว่า `0`
- `pitch_deg`
  - หน่วย: `degree`
  - ความหมาย: มุมเอียง pitch ที่คำนวณจากค่าเฉลี่ย acceleration
  - ค่าตัวอย่าง: `-40.15989 deg`
- `roll_deg`
  - หน่วย: `degree`
  - ความหมาย: มุมเอียง roll ที่คำนวณจากค่าเฉลี่ย acceleration
  - ค่าตัวอย่าง: `-5.715371 deg`
- `yaw_deg`
  - หน่วย: `degree`
  - ความหมาย: ค่ามุม yaw
  - พฤติกรรมในเฟิร์มแวร์ปัจจุบัน: ตั้งค่าเป็น `0` ตลอด ยังไม่มีการคำนวณจริง
- `battery_v`
  - หน่วย: `V`
  - ความหมาย: แรงดันแบตเตอรี่ที่วัดได้
  - ค่าตัวอย่าง: `3.605421 V`
- `wifi_rssi`
  - หน่วย: `dBm`
  - ความหมาย: ความแรงสัญญาณ Wi-Fi ที่อุปกรณ์รับได้
  - ค่าตัวอย่าง: `-86 dBm`
- `uptime_ms`
  - หน่วย: `ms`
  - ความหมาย: เวลาที่อุปกรณ์ทำงานต่อเนื่องตั้งแต่บูต
  - ค่าตัวอย่าง: `25574 ms` หรือประมาณ `25.574 s`

วิธีตีความ payload ชุดนี้:

- อุปกรณ์ตรวจพบการสั่นค่อนข้างต่ำในทั้ง 3 แกน เพราะค่า `accel_*_rms` ทุกแกนยังต่ำกว่า `0.02 g`
- เมื่อสัญญาณต่ำกว่า threshold การตรวจจับการสั่น เฟิร์มแวร์จะตั้งค่า `vibration_*_rms_mm_s`, `vibration_freq_*_hz` และ `displacement_*_um` เป็น `0`
- แม้ว่าค่าการสั่นจะไม่ผ่าน validation แต่ค่า `pitch_deg` และ `roll_deg` ยังสามารถคำนวณได้ เพราะใช้ค่าเฉลี่ยของ acceleration ไม่ได้ขึ้นกับผล validation ของ vibration
- แบตเตอรี่ในช่วงตัวอย่างนี้อยู่ที่ประมาณ `3.61 V`
- สัญญาณ Wi-Fi ค่อนข้างอ่อน เพราะ `wifi_rssi = -86 dBm`

#### การทำงานของคำสั่ง FFT

การจัดการคำสั่ง FFT อยู่ใน [src/mqtt_handler.cpp](src/mqtt_handler.cpp)

- อุปกรณ์รอฟังคำสั่งที่ `topic_subscribe`
- คำสั่งจะระบุแกนและค่า FFT step
- เฟิร์มแวร์รอการประมวลผล MEMS `3` รอบ
- จากนั้น publish snapshot ไปที่:
  - `topic_result`
  - topic ของแกนที่เลือก (`topic_fft_x`, `topic_fft_y`, หรือ `topic_fft_z`)
- ส่วน status หรือ ack จะถูก publish ไปที่ `topic_ack`

### หมายเหตุเรื่อง MQTTS

ระบบรองรับ TLS แต่ implementation ปัจจุบันใช้:

```cpp
secure_client.setInsecure();
```

ความหมายคือ:

- มีการเข้ารหัสข้อมูล
- ยังไม่ตรวจสอบ server certificate
- ใน log จะเห็นข้อความ `Skipping SSL Verification. INSECURE!`

### รายละเอียดเซนเซอร์และการวิเคราะห์

อ้างอิงจาก [include/config.h](include/config.h) และ [src/sensors.cpp](src/sensors.cpp)

- จำนวน sample: `1024`
- data rate เริ่มต้น: `1600 Hz`
- จำนวน FFT display points: `120`
- จำนวน MQTT FFT points ต่อ snapshot: `32`
- high-pass cutoff สำหรับ RMS: `10 Hz`
- threshold เริ่มต้นสำหรับการยอมรับสัญญาณสั่น:
  - `min_rms_g = 0.02`
  - `min_peak_g = 0.05`
  - `min_freq_hz = 5`
  - `max_freq_hz = 1000`

พฤติกรรมสำคัญ:

- velocity และ displacement จะถูกรายงานเฉพาะเมื่อสัญญาณผ่าน validation
- ถ้าการสั่นอ่อนหรือมี noise มาก มักจะได้ `valid=NO` และค่า frequency, velocity, displacement จะถูกบังคับเป็นศูนย์
- ค่า pitch และ roll ถูกคำนวณจากค่าเฉลี่ยของ acceleration เสมอ

### นโยบายพลังงานและการปลุก

deep sleep ถูกจัดการใน [src/power_management.cpp](src/power_management.cpp)

นโยบายปัจจุบัน:

- แหล่งปลุกปกติคือ timer และ ADXL345 `INT1`
- motion wake อาจถูกกดชั่วคราวหลังจากเกิดติดต่อกัน `5` ครั้ง
- motion wake อาจถูกเปิดกลับหลังจากมี clean timer wake ติดต่อกัน `2` ครั้งในช่วงที่กำลังกดอยู่
- log ของ wake policy จะมี:
  - pre-state
  - event decision
  - action taken
  - post-state

เงื่อนไขที่ main loop จะสั่งนอนอยู่ใน [src/main.cpp](src/main.cpp):

- อยู่ใน normal mode
- มี MQTT publish สำเร็จอย่างน้อยหนึ่งครั้ง
- ไม่มี FFT streaming ทำงานอยู่
- ไม่มีงาน FFT command ค้างอยู่

### งาน FreeRTOS

การสร้าง task และการจัดตารางอยู่ใน [src/main.cpp](src/main.cpp) และขนาด stack อยู่ใน [include/config.h](include/config.h)

| Task | Priority | Core | Stack |
|------|----------|------|-------|
| `MEMS` | 8 | 0 | 8192 |
| `WiFi` | 5 | 1 | 6144 |
| `MQTT` | 6 | 1 | 6144 |
| `MQTT-Pub` | 7 | 1 | 5120 |
| `Web` | 4 | 1 | 6144 |
| `ADC` | 2 | 1 | 3072 |

ช่วงเวลาทำงานโดยประมาณ:

- MEMS task ทำงานทุกประมาณ `2500 ms`
- ADC task ทำงานทุก `30000 ms`
- MQTT publish task ตรวจทุก `5000 ms`
- ช่วงเวลาการ publish หลักขึ้นกับค่าคอนฟิก

### ข้อจำกัดที่ทราบ

- ยังไม่มี BLE
- ยังไม่มีการตรวจสอบ certificate ของ MQTTS
- ชื่อ environment ของ PlatformIO และชื่อฮาร์ดแวร์ในเอกสารเก่า อาจไม่ตรงกับ pin map ปัจจุบันทั้งหมด
- log ของ ESP-IDF ที่ว่า `Incorrect wakeup source (3) to disable` อาจยังโผล่ตอนปิด EXT1 wake ซึ่งเป็นปัญหาการ cleanup ระหว่างรัน ไม่ใช่ฟีเจอร์ที่ตั้งใจไว้

### การแก้ปัญหาเบื้องต้น

#### มีข้อมูล MEMS แต่ไม่มีค่าการสั่น

ตรวจสอบ `min_rms_g` และ `min_peak_g` ในคอนฟิก vibration เฟิร์มแวร์ตั้งใจบังคับค่า velocity, displacement และ vibration frequency ให้เป็นศูนย์เมื่อสัญญาณต่ำกว่าเกณฑ์ validation

#### การเปิดเว็บผ่าน STA ทำให้ MQTT ดูเหมือนหยุด

เป็นพฤติกรรมที่คาดไว้ได้ เว็บเซิร์ฟเวอร์อาจ pause กิจกรรม MQTT ชั่วคราวระหว่างการเปิดหน้าเว็บหรือเรียก API ผ่าน STA เพื่อลดแรงกดดันของ heap

#### MQTTS เชื่อมได้แต่มี log `INSECURE`

เป็นพฤติกรรมที่คาดไว้ได้ใน implementation ปัจจุบัน เพราะปิดการตรวจสอบ certificate อยู่

#### อุปกรณ์ตื่นด้วย timer แต่ motion wake ไม่กลับมา

ให้ดู log ของ wake policy:

- `ADXL345 INT boot state: before_init=... after_init=... policy=...`
- `Wake policy pre: ...`
- `Wake policy event: ...`
- `Wake policy post: ...`

log ชุดนี้ช่วยให้ดูได้เร็วที่สุดว่า timer wake ถูกนับหรือถูกมองข้าม

### ไฟล์ที่เกี่ยวข้อง

- [src/main.cpp](src/main.cpp)
- [src/sensors.cpp](src/sensors.cpp)
- [src/wifi_handler.cpp](src/wifi_handler.cpp)
- [src/mqtt_handler.cpp](src/mqtt_handler.cpp)
- [src/web_server.cpp](src/web_server.cpp)
- [src/power_management.cpp](src/power_management.cpp)
- [src/storage.cpp](src/storage.cpp)

### ใบอนุญาต

MIT License ดูได้ที่ [LICENSE](LICENSE)
