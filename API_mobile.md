# VIOT Mobile API Guide

คู่มือสรุป API สำหรับนำไปใช้พัฒนา Mobile App ให้เชื่อมต่อกับอุปกรณ์ VIOT ตาม firmware ปัจจุบัน

อ้างอิงจากโค้ดจริงใน:

- `src/web_server.cpp`
- `src/discovery_service.cpp`
- `src/mqtt_handler.cpp`
- `src/wifi_handler.cpp`
- `src/storage.cpp`

## 1. Base URL

แอปสามารถเรียกอุปกรณ์ผ่าน:

- `http://<sta_ip>`
- `http://<hostname>.local`
- `http://<ap_ip>`

ตัวอย่าง:

- `http://192.168.1.111`
- `http://esp32-viot-0e7da0.local`
- `http://192.168.4.1`

## 2. Request Rules

### GET

- ใช้ HTTP GET ปกติ
- response ส่วนใหญ่เป็น JSON

### POST

ปัจจุบัน firmware รับค่าแบบ form fields เท่านั้น:

- `application/x-www-form-urlencoded`
- หรือ `multipart/form-data`

ไม่ควรส่ง raw JSON body

## 3. API Table

| API | Method | Purpose | Request | Response |
|---|---|---|---|---|
| `/api/discover` | `GET` | ดึงข้อมูลระบุตัวอุปกรณ์และ network discovery | none | JSON |
| `/api/status` | `GET` | ดึงสถานะ runtime แบบเบา | none | JSON |
| `/api/config` | `GET` | ดึง config ปัจจุบันทั้งหมด | none | JSON |
| `/api/dashboard` | `GET` | ดึง snapshot ค่าการวัดล่าสุด | none | JSON |
| `/api/fft_spectrum?axis=x` | `GET` | ดึง FFT spectrum ของแกนที่เลือก | query `axis=x|y|z` | JSON |
| `/api/fft_csv?axis=x` | `GET` | ดึง FFT เป็น CSV | query `axis=x|y|z` | text/plain |
| `/api/mqtt_publish_summary` | `GET` | ดึงสถานะการ publish / subscribe MQTT ล่าสุด | none | JSON |
| `/api/scan_ssid` | `GET` | สแกน WiFi ที่มองเห็น | none | JSON |
| `/api/config` | `POST` | บันทึก WiFi แบบสั้น | form fields | JSON |
| `/api/network_config` | `POST` | บันทึก network config แบบเต็ม | form fields | JSON |
| `/api/mqtt_config` | `POST` | บันทึก MQTT / MQTTS config | form fields | JSON |
| `/api/mems_config` | `POST` | บันทึก MEMS / vibration / sleep config | form fields | JSON |
| `/api/operate_config` | `POST` | บันทึก operate config และ debug flags | form fields | JSON |
| `/api/system_config` | `POST` | บันทึก system config บางส่วน | form fields | JSON |
| `/api/ap_config` | `POST` | เปลี่ยน AP SSID / password | form fields | JSON |
| `/api/reboot` | `POST` | รีบูตอุปกรณ์ | none | JSON |
| `/api/reset` | `POST` | reset config / optionally clear SPIFFS | form fields | JSON |
| `/ws` | `WS` | live push dashboard data | websocket | JSON push |

## 4. Discovery APIs

### 4.1 `GET /api/discover`

ใช้เป็น API แรกหลังจากหา IP หรือ hostname เจอ

ตัวอย่าง request:

```http
GET /api/discover HTTP/1.1
Host: 192.168.1.111
```

ตัวอย่าง response:

```json
{
  "protocol": "viot-discovery-v1",
  "device_type": "viot-sensor-node",
  "device_name": "esp32-viot-0e7da0",
  "device_id": "esp32-viot-0e7da0",
  "hostname": "esp32-viot-0e7da0",
  "mdns_host": "esp32-viot-0e7da0.local",
  "mdns_service": "_iot-sensor._tcp.local",
  "web_port": 80,
  "udp_port": 37020,
  "discover_path": "/api/discover",
  "status_path": "/api/status",
  "wifi_state": "WIFI_CONNECTED",
  "sta_connected": true,
  "sta_ip": "192.168.1.111",
  "ap_ip": "192.168.4.1",
  "ap_ssid": "VIOT_7DA0",
  "mac": "AA:BB:CC:0E:7D:A0",
  "mqtt_client_id": "ESP32_VIOT",
  "rssi": -49,
  "uptime_sec": 42,
  "mdns_ready": true,
  "udp_ready": true,
  "fw_build_date": "May 16 2026",
  "fw_build_time": "10:45:00"
}
```

## 5. Status APIs

### 5.1 `GET /api/status`

ใช้สำหรับ health check, battery, RSSI, MQTT status

ตัวอย่าง response:

```json
{
  "debug_mode": true,
  "log_enabled": true,
  "uptime_sec": 123,
  "battery_v": 4.03,
  "wifi_rssi": -48,
  "wifi_state": "WIFI_CONNECTED",
  "ap_ip": "192.168.4.1",
  "sta_ip": "192.168.1.111",
  "mqtt_connected": true,
  "mqtt_status": "CONNECTED",
  "mqtt_broker": "example.s1.eu.hivemq.cloud",
  "mems_timing": {
    "effective_sample_rate_hz": 1584.1,
    "target_period_us": 625,
    "capture_elapsed_us": 646444,
    "avg_read_high_us": 283,
    "avg_wait_low_us": 342
  }
}
```

### 5.2 `GET /api/dashboard`

ใช้สำหรับหน้า dashboard หลัก

ตัวอย่าง response:

```json
{
  "has_data": true,
  "ts": 1715823300,
  "accel": {
    "x": 0.011,
    "y": 0.010,
    "z": 1.002
  },
  "velocity": {
    "x": 0.442,
    "y": 0.221,
    "z": 0.118
  },
  "vibration_freq": {
    "x": 50.2,
    "y": 49.8,
    "z": 0.0
  },
  "orientation": {
    "pitch": 0.8,
    "roll": -0.5,
    "yaw": 0.0
  },
  "displacement": {
    "x_um": 1.40,
    "y_um": 0.71,
    "z_um": 0.0
  },
  "battery": 4.03,
  "rssi": -48,
  "wifi_state": "WIFI_CONNECTED",
  "mqtt": {
    "broker": "example.s1.eu.hivemq.cloud",
    "status": "CONNECTED"
  },
  "ap_ip": "192.168.4.1",
  "mems_timing": {
    "effective_sample_rate_hz": 1584.1,
    "target_period_us": 625,
    "capture_elapsed_us": 646444,
    "avg_read_high_us": 283,
    "avg_wait_low_us": 342
  }
}
```

## 6. FFT APIs

### Important Current FFT Behavior

- ช่วงความถี่ปัจจุบัน: `10..800 Hz`
- จำนวนจุดปัจจุบัน: `80 points`
- `freq_hz`: ส่ง `1` ตำแหน่งทศนิยม
- `amp_mm_s`: ส่ง `3` ตำแหน่งทศนิยม

### 6.1 `GET /api/fft_spectrum?axis=x|y|z`

ตัวอย่าง request:

```http
GET /api/fft_spectrum?axis=x HTTP/1.1
Host: 192.168.1.111
```

ตัวอย่าง response:

```json
{
  "axis": "x",
  "freq_hz": [10.0, 20.0, 30.0, 40.0, 50.0],
  "amp_mm_s": [0.448, 0.074, 0.099, 0.122, 0.087]
}
```

error ที่ควร handle:

```json
{
  "error": "low memory"
}
```

หรือ

```json
{
  "error": "fft spectrum unavailable"
}
```

### 6.2 `GET /api/fft_csv?axis=x|y|z`

ตัวอย่าง response:

```text
Frequency(Hz),Amplitude(mm/s)
10.00,0.4476
20.00,0.0743
30.00,0.0986
40.00,0.1216
50.00,0.0866
```

## 7. Config APIs

### 7.1 `GET /api/config`

ใช้ดึงค่า config ปัจจุบันทั้งหมดก่อน render หน้าตั้งค่า

ตัวอย่าง response:

```json
{
  "wifi": {
    "ssid": "BANONGLEE_2.4G",
    "password": "******",
    "ap_enabled": true,
    "ap_ssid": "",
    "ap_ssid_effective": "VIOT_7DA0",
    "ap_password": "12345678",
    "sta_use_static_ip": false,
    "sta_static_ip": "",
    "sta_gateway": "",
    "sta_subnet": "",
    "sta_dns1": "",
    "sta_dns2": "",
    "state": "WIFI_CONNECTED"
  },
  "mqtt": {
    "broker": "example.s1.eu.hivemq.cloud",
    "port": 8883,
    "client_id": "ESP32_VIOT",
    "username": "user",
    "password": "pass",
    "topic_publish": "viot/vibration",
    "topic_fft_x": "viot/vibration/fft/x",
    "topic_fft_y": "viot/vibration/fft/y",
    "topic_fft_z": "viot/vibration/fft/z",
    "topic_subscribe": "viot/config",
    "topic_ack": "viot/config/ack",
    "topic_result": "viot/config/result",
    "publish_interval_s": 60,
    "publish_on_vibration_trigger": false,
    "publish_vibration_threshold_mm_s": 5.0,
    "use_tls": true,
    "protocol": "mqtts",
    "status": "CONNECTED"
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
    "log_enabled": true,
    "debug_log_mask": 255
  },
  "operate": {
    "publish_interval_s": 60,
    "wakeup_int_threshold_mg": 250,
    "wakeup_int_enabled": true,
    "wakeup_timer_sec": 3600,
    "publish_on_vibration_trigger": false,
    "publish_vibration_threshold_mm_s": 5.0,
    "log_enabled": true,
    "debug_log_mask": 255
  }
}
```

### 7.2 `POST /api/config`

ใช้บันทึก WiFi แบบขั้นต่ำ

ตัวอย่าง request:

```http
POST /api/config
Content-Type: application/x-www-form-urlencoded

ssid=BANONGLEE_2.4G&password=12345678
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "WiFi config saved. STA reconnect scheduled."
}
```

### 7.3 `POST /api/network_config`

ใช้บันทึก network config แบบเต็ม

ตัวอย่าง request:

```http
POST /api/network_config
Content-Type: application/x-www-form-urlencoded

ssid=BANONGLEE_2.4G&password=12345678&ap_enabled=1&ap_ssid=VIOT_TEST&ap_password=12345678&sta_use_static_ip=0
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "Network config applied."
}
```

### 7.4 `POST /api/mqtt_config`

ตัวอย่าง request:

```http
POST /api/mqtt_config
Content-Type: application/x-www-form-urlencoded

broker=example.s1.eu.hivemq.cloud&port=8883&client_id=ESP32_VIOT&username=user&password=pass&topic_publish=viot/vibration&topic_fft_x=viot/vibration/fft/x&topic_fft_y=viot/vibration/fft/y&topic_fft_z=viot/vibration/fft/z&topic_subscribe=viot/config&topic_ack=viot/config/ack&topic_result=viot/config/result&publish_interval_s=60&use_tls=1
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "MQTT config saved. Reconnect scheduled with new settings."
}
```

### 7.5 `POST /api/mems_config`

ตัวอย่าง request:

```http
POST /api/mems_config
Content-Type: application/x-www-form-urlencoded

rate_hz=1600&range_g=16&offset_x=0&offset_y=0&offset_z=0&int_threshold_mg=250&int_enabled=1&min_rms_g=0.02&min_peak_g=0.05&noise_floor_db=-20&deadband_g=0.005&min_freq_hz=5&max_freq_hz=1000&sleep_interval_sec=3600
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "MEMS config applied."
}
```

### 7.6 `POST /api/operate_config`

ตัวอย่าง request:

```http
POST /api/operate_config
Content-Type: application/x-www-form-urlencoded

publish_interval_s=60&int_threshold_mg=250&int_enabled=1&sleep_interval_sec=3600&publish_on_vibration_trigger=0&publish_vibration_threshold_mm_s=5.0&log_enabled=1&debug_log_wifi=1&debug_log_mqtt=1&debug_log_mems=1&debug_log_power=1&debug_log_web=1&debug_log_battery=1&debug_log_operate=1&debug_log_system=1
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "Operate config applied."
}
```

### 7.7 `POST /api/system_config`

ตัวอย่าง request:

```http
POST /api/system_config
Content-Type: application/x-www-form-urlencoded

log_enabled=1
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "System config saved."
}
```

### 7.8 `POST /api/ap_config`

ตัวอย่าง request:

```http
POST /api/ap_config
Content-Type: application/x-www-form-urlencoded

ap_ssid=VIOT_TEST&ap_password=12345678
```

ตัวอย่าง response:

```json
{
  "status": "ok",
  "message": "AP config applied immediately"
}
```

## 8. Utility APIs

### 8.1 `GET /api/scan_ssid`

ตัวอย่าง response:

```json
{
  "networks": [
    {
      "ssid": "BANONGLEE_2.4G",
      "rssi": -42,
      "channel": 6,
      "secure": true
    },
    {
      "ssid": "GuestWifi",
      "rssi": -75,
      "channel": 11,
      "secure": false
    }
  ]
}
```

### 8.2 `GET /api/mqtt_publish_summary`

ตัวอย่าง response:

```json
{
  "has_publish": true,
  "success": true,
  "publish_count": 12,
  "last_attempt_ms": 80123,
  "last_success_ms": 80123,
  "seconds_since_last_success": 2,
  "publish_interval_s": 60,
  "next_publish_due_ms": 140123,
  "seconds_until_next_publish": 58,
  "tls_connecting": false,
  "main_size": 322,
  "fft_x_size": 0,
  "fft_y_size": 0,
  "fft_z_size": 0,
  "subscribe_receive_count": 4,
  "last_subscribe_ms": 79000,
  "seconds_since_last_subscribe": 3,
  "last_subscribe_size": 55
}
```

## 9. Control APIs

### 9.1 `POST /api/reboot`

ตัวอย่าง response:

```json
{
  "status": "rebooting"
}
```

### 9.2 `POST /api/reset`

ตัวอย่าง request:

```http
POST /api/reset
Content-Type: application/x-www-form-urlencoded

clear_spiffs=true
```

ตัวอย่าง response:

```json
{
  "status": "resetting"
}
```

## 10. WebSocket

### `WS /ws`

ใช้สำหรับ live push dashboard

ตัวอย่าง message:

```json
{
  "ts": 1715823300,
  "accel": {
    "x": 0.011,
    "y": 0.010,
    "z": 1.002
  },
  "velocity": {
    "x": 0.442,
    "y": 0.221,
    "z": 0.118
  },
  "vibration_freq": {
    "x": 50.2,
    "y": 49.8,
    "z": 0.0
  },
  "fft": {
    "x": 50.0,
    "y": 50.0,
    "z": 0.0
  },
  "orientation": {
    "pitch": 0.8,
    "roll": -0.5,
    "yaw": 0.0,
    "inclination": 0.94
  },
  "displacement": {
    "x_um": 1.40,
    "y_um": 0.71,
    "z_um": 0.0
  },
  "battery": 4.03,
  "rssi": -48,
  "wifi_state": "WIFI_CONNECTED",
  "mqtt": {
    "broker": "example.s1.eu.hivemq.cloud",
    "status": "CONNECTED",
    "publish": {
      "has_publish": true,
      "success": true,
      "count": 12,
      "last_attempt_ms": 80123,
      "last_success_ms": 80123,
      "tls_connecting": false,
      "main_size": 322,
      "fft_x_size": 0,
      "fft_y_size": 0,
      "fft_z_size": 0
    }
  },
  "ap_ip": "192.168.4.1"
}
```

## 11. MQTT Command FFT For App

ถ้าแอปคุยกับ broker โดยตรง สามารถสั่ง FFT ผ่าน MQTT ได้

### Command Topic

- default: `viot/config`

### Ack Topic

- default: `viot/config/ack`

### Result Topic

- default: `viot/config/result`

### Axis Topics

- `viot/vibration/fft/x`
- `viot/vibration/fft/y`
- `viot/vibration/fft/z`

### Request Example

```json
{
  "action": "fft_x",
  "request_id": "req-001",
  "step_hz": 10
}
```

หมายเหตุปัจจุบัน:

- FFT command จะจบที่ `800 Hz`
- accepted `step_hz`: `10, 15, 20, 25, 30, 35, 40, 45, 50`

### Ack Example

```json
{
  "status": "accepted",
  "axis": "x",
  "request_id": "req-001",
  "detail": "queued_for_3_rounds",
  "timestamp": 123456
}
```

### Result Example

```json
{
  "timestamp": 1715823300,
  "axis": "x",
  "request_id": "req-001",
  "step_hz": 10,
  "data": {
    "x_freq_hz": [10.00, 20.00, 30.00, 40.00, 50.00],
    "x_amplitude_mm_s": [0.45, 0.07, 0.10, 0.12, 0.09]
  }
}
```

## 12. App Handling Recommendations

- handle `503 {"error":"low memory"}`
- handle reboot/reset แล้ว connection หลุด
- FFT page ควรใช้ช่วง `10..800 Hz`
- อย่าคาดหวังจำนวนจุดเก่า `120`
- ตอนนี้ใช้ `80` จุด
- POST config ทุกตัวให้ส่งแบบ form
- debug mode ปัจจุบันคือ `AP + STA`
- normal mode ปัจจุบันคือ `STA only`

## 13. Mobile Screen To API Map

ส่วนนี้สรุปจากหน้าเว็บจริงใน `data/www/` เพื่อใช้แทนการทำ web page บนอุปกรณ์

### 13.1 Dashboard Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/index.html`

API ที่ใช้:

- `GET /api/dashboard?keep_mqtt=1`
- `GET /api/mqtt_publish_summary?keep_mqtt=1`

ความถี่บนเว็บเดิม:

- ทุก `4` วินาที

ข้อมูลที่ควรแสดง:

- accel x/y/z
- velocity x/y/z
- vibration frequency x/y/z
- displacement x/y/z
- pitch / roll / yaw
- battery
- RSSI
- effective sample rate
- MQTT publish summary

### 13.2 FFT Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/fft_chart.html`

API ที่ใช้:

- `GET /api/fft_spectrum?axis=x|y|z`

ความถี่บนเว็บเดิม:

- ทุก `4` วินาที

ข้อมูลที่ควรแสดง:

- FFT spectrum
- axis selector
- auto/manual scale
- point count

หมายเหตุ:

- ช่วงปัจจุบัน `10..800 Hz`
- จำนวนจุดปัจจุบัน `80`

### 13.3 Network Config Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/network_setting.html`

API ที่ใช้:

- `GET /api/config`
- `GET /api/scan_ssid`
- `POST /api/network_config`

ข้อมูลที่ควรแสดง/แก้ไข:

- STA SSID
- STA password
- static IP / gateway / subnet / DNS
- AP SSID
- AP password
- AP enabled
- current WiFi state
- scan SSID result

### 13.4 MQTT Config Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/mqtt_setting.html`

API ที่ใช้:

- `GET /api/config`
- `POST /api/mqtt_config`

ข้อมูลที่ควรแสดง/แก้ไข:

- protocol `mqtt` / `mqtts`
- broker
- port
- client_id
- username
- password
- topic_publish
- topic_fft_x
- topic_fft_y
- topic_fft_z
- topic_subscribe
- topic_ack
- topic_result

### 13.5 MEMS Config Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/mems_setting.html`

API ที่ใช้:

- `GET /api/config`
- `POST /api/mems_config`

ข้อมูลที่ควรแสดง/แก้ไข:

- measurement bandwidth
- range G
- offset x/y/z

mapping สำคัญ:

- `200 Hz bandwidth -> rate_hz 400`
- `400 Hz bandwidth -> rate_hz 800`
- `800 Hz bandwidth -> rate_hz 1600`

### 13.6 Operate Config Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/operate_config.html`

API ที่ใช้:

- `GET /api/config`
- `POST /api/operate_config`

ข้อมูลที่ควรแสดง/แก้ไข:

- publish interval
- MEMS interrupt threshold
- wakeup INT enabled
- wakeup timer
- publish on vibration trigger
- velocity trigger threshold
- log enabled
- debug log category flags

### 13.7 System Action Screen

อ้างอิงจากหน้าเว็บ:

- `data/www/system_setting.html`

API ที่ใช้:

- `POST /api/reboot`
- `POST /api/reset`

action ที่ควรมี:

- reboot device
- factory reset
- optional clear SPIFFS

### 13.8 Optional Discovery Screen

หน้าเว็บเดิมไม่ได้มีเป็น page แยกชัดเจน แต่ Mobile App ควรมี

API ที่แนะนำ:

- `GET /api/discover`
- `GET /api/status`

ใช้สำหรับ:

- show hostname / device id
- show STA IP / AP IP
- show connection health
- show firmware build info

## 14. Minimal API Set Per Screen

| Mobile Screen | APIs |
|---|---|
| Device Discovery | `GET /api/discover`, `GET /api/status` |
| Dashboard | `GET /api/dashboard`, `GET /api/mqtt_publish_summary` |
| FFT | `GET /api/fft_spectrum?axis=x|y|z` |
| Network | `GET /api/config`, `GET /api/scan_ssid`, `POST /api/network_config` |
| MQTT | `GET /api/config`, `POST /api/mqtt_config` |
| MEMS | `GET /api/config`, `POST /api/mems_config` |
| Operate | `GET /api/config`, `POST /api/operate_config` |
| System | `POST /api/reboot`, `POST /api/reset` |

## 15. Recommended Mobile Request Pattern

เพื่อใช้ heap ฝั่ง ESP32 ให้น้อยกว่า web page เดิม ควรเรียกแบบนี้:

- Discovery screen:
  - เรียก `GET /api/discover` ตอนเข้า screen หรือ refresh
  - เรียก `GET /api/status` เป็นช่วงห่าง เช่น `5-10` วินาที

- Dashboard screen:
  - เรียก `GET /api/dashboard`
  - เรียก `GET /api/mqtt_publish_summary`
  - polling แนะนำ `3-5` วินาที

- FFT screen:
  - เรียก `GET /api/fft_spectrum`
  - polling แนะนำ `4-6` วินาที
  - หยุด polling ทันทีเมื่อออกจาก screen

- Config screens:
  - เรียก `GET /api/config` แค่ตอนเปิดหน้า
  - `POST` เมื่อกด save เท่านั้น

- System screen:
  - ไม่ต้อง polling
  - ยิง action เฉพาะตอนกดปุ่ม
