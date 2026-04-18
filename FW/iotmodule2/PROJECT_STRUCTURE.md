# Project Structure

This document describes the complete directory structure of the VIOT firmware project.

```
iotmodule2/
├── platformio.ini              # PlatformIO configuration (boards, libraries, build flags)
├── README.md                   # Main documentation and feature overview
├── BUILD_GUIDE.md              # Detailed build and deployment instructions
├── CONFIGURATION_GUIDE.md      # Complete configuration parameter reference
├── config.example.json         # Example configuration file
├── PROJECT_STRUCTURE.md        # This file
│
├── include/                    # Header files (.h)
│   ├── config.h                # Project-wide configuration constants
│   ├── common.h                # Common data structures and enums
│   ├── sensors.h               # MEMS & Battery sensor interfaces
│   ├── wifi_handler.h          # WiFi task and management
│   ├── mqtt_handler.h          # MQTT communication handler
│   ├── web_server.h            # Web server and dashboard
│   ├── power_management.h      # Sleep and wake management
│   └── storage.h               # SPIFFS filesystem and JSON config
│
├── src/                        # Source files (.cpp)
│   ├── main.cpp                # Main setup(), loop(), task orchestration
│   ├── common.cpp              # Common utility implementations
│   ├── sensors.cpp             # MEMS & Battery sensor implementations
│   ├── wifi_handler.cpp        # WiFi task function and handler
│   ├── mqtt_handler.cpp        # MQTT handler with JSON payload building
│   ├── web_server.cpp          # Web server endpoints and WebSocket
│   ├── power_management.cpp    # Sleep/wake implementations
│   └── storage.cpp             # SPIFFS operations and JSON serialization
│
├── data/                       # Data files for SPIFFS
│   └── www/                    # Web assets
│       └── index.html          # Dashboard HTML + JavaScript
│
├── lib/                        # (Auto-populated by PlatformIO)
│   └── README                  # Placeholder
│
└── test/                       # Test files (optional)
    └── README                  # Placeholder
```

## File Descriptions

### Configuration Files

#### `platformio.ini`
- Board configuration: `esp32doit-devkit-v1`
- Library dependencies (Adafruit, ArduinoJSON, AsyncHTTP, etc.)
- Build flags and compiler settings
- Serial monitor configuration

#### `config.example.json`
- Template for user configuration
- Contains all default values
- Used on first boot if no config exists

### Header Files (include/)

#### `config.h`
- GPIO pin mappings
- I2C settings
- Timing constants (sampling rates, sleep intervals)
- Task priorities and stack sizes
- FFT and ADC parameters
- **Edit this file to change pin assignments**

#### `common.h`
- Global debug flag: `g_debug_mode`
- Debug/error macros: `DEBUG_PRINT`, `ERROR_PRINT`, `INFO_PRINT`
- Enums: `WakeupReason`, `WiFiStatus`, `MQTTStatus`
- Data structures: `MEMSData`, `VibrationAnalysis`, `SystemStatus`, `SystemConfig`, `MQTTPayload`
- Helper function declarations

#### `sensors.h`
- `MEMSSensor` class: I2C ADXL345 interface
  - `readRawData()`: Capture acceleration samples
  - `processVibrationData()`: RMS, FFT, pitch/roll calculations
  - Configurable rate, range, offset
- `BatterySensor` class: GPIO3 ADC voltage monitoring

#### `wifi_handler.h`
- `WiFiHandler` class: WiFi management
  - AP+STA dual mode support
  - Reconnection logic
  - RSSI monitoring
- `wifi_task()`: FreeRTOS task function

#### `mqtt_handler.h`
- `MQTTHandler` class: PubSubClient wrapper
  - TLS/SSL support
  - AWS IoT compatible
  - Configurable publish interval
- `mqtt_task()`: Connection maintenance task
- JSON payload building methods

#### `web_server.h`
- `WebServer` class: ESPAsyncWebServer wrapper
  - REST API endpoints (/api/config, /api/status, etc.)
  - WebSocket support for real-time data
  - File upload handlers
- `web_task()`: Async server task

#### `power_management.h`
- `PowerManager` class: Sleep control
  - Deep sleep with timer/interrupt wake
  - Debug mode override
- Wakeup reason detection

#### `storage.h`
- `Storage` class: SPIFFS filesystem interface
  - JSON config load/save
  - File operations
  - Default config generation
- JSON serialization helpers

### Source Files (src/)

#### `main.cpp` (500+ lines)
**The orchestrator of everything:**

- Global object declarations (sensors, WiFi, MQTT, etc.)
- FreeRTOS queue and event group creation
- Task functions:
  - `mems_task()` → Sensor acquisition + processing
  - `adc_task()` → Battery monitoring
  - `mqtt_publish_task()` → Data publishing
- `setup()` → Hardware init, filesystem, task creation
- `loop()` → GPIO monitoring, WebSocket updates

**Key Features:**
- Wakeup reason detection
- Debug mode based on GPIO27
- Task priority assignment (MEMS on core 0, others on core 1)
- Event-driven updates

#### `common.cpp`
- Global `g_debug_mode` variable
- `get_wakeup_reason()` implementation
- Debug macro implementations
- Status printing functions
- Timestamp conversion helpers

#### `sensors.cpp` (400+ lines)
**MEMS & ADC implementations:**

`MEMSSensor`:
- I2C initialization
- Raw data acquisition (1600 Hz, 2-second buffers)
- FFT using arduinoFFT library
- RMS calculations (acceleration & velocity)
- Pitch/roll from gravity
- Temperature (if available)

`BatterySensor`:
- ADC pin reading
- Voltage conversion (12-bit → 0-3.3V)
- Battery percentage calculation
- Low battery detection

#### `wifi_handler.cpp` (150+ lines)
**WiFi connectivity:**

- SSID/Password handling
- AP mode with auto-generated SSID
- STA connection with timeout
- RSSI monitoring
- Reconnection retry logic

#### `mqtt_handler.cpp` (300+ lines)
**MQTT communication:**

- PubSubClient initialization with TLS support
- Connect/disconnect with auth
- Vibration data JSON building
- System status publishing
- Configurable publish interval
- QoS handling

#### `web_server.cpp` (300+ lines)
**HTTP + WebSocket:**

- ESPAsyncWebServer routing
- REST endpoints for config/status
- WebSocket for real-time updates (async)
- HTML fallback (inline) if SPIFFS missing
- File upload handlers

#### `power_management.cpp` (60+ lines)
**Power control:**

- Timer wakeup configuration (1 hour default)
- External interrupt setup
- Deep sleep entry
- Wakeup reason tracking

#### `storage.cpp` (250+ lines)
**Filesystem & Config:**

- SPIFFS mount with auto-format fallback
- JSON config serialization/deserialization
- File read/write operations
- Default config generation
- Type conversion helpers

### Web Assets

#### `data/www/index.html` (400+ lines)
**Interactive dashboard:**

- Real-time WebSocket connection
- Chart.js graphs (acceleration, velocity, FFT)
- System status display (battery, WiFi, MQTT)
- Raw data stream viewer
- Responsive design (mobile-friendly)
- Auto-reconnect logic

---

## Code Architecture

### Layering Model

```
┌─────────────────────────────────────┐
│         main.cpp (Orchestrator)     │
│  - Task creation & coordination     │
│  - Event group management           │
└──────┬──────────────────────────────┘
       │
       ├─ Hardware Abstraction Layer
       │  ├─ sensors.h/cpp (I2C, ADC)
       │  ├─ power_management.h/cpp
       │  └─ common.h/cpp (GPIO, debug)
       │
       ├─ Communication Layer
       │  ├─ wifi_handler.h/cpp
       │  ├─ mqtt_handler.h/cpp
       │  └─ web_server.h/cpp
       │
       └─ Storage Layer
          └─ storage.h/cpp (SPIFFS, JSON)
```

### Task Scheduling

| Task | Priority | Core | Frequency | Purpose |
|------|----------|------|-----------|---------|
| MEMS | 8 (High) | 0 | Real-time | Sensor sampling (1600 Hz) |
| WiFi | 5 | 1 | 10 sec | Connection maintenance |
| MQTT | 6 | 1 | 5 sec | Broker connectivity |
| MQTT-Pub | 7 | 1 | 60 sec | Data publishing |
| ADC | 2 (Low) | 1 | 30 sec | Battery monitoring |
| Web | 4 | 1 | Async | HTTP/WebSocket handling |

### Data Flow Pipeline

```
ADXL345 (I2C)
    ↓ [mems_task reads @ 1600 Hz]
    ↓ Raw acceleration data buffer (3200 samples = 2 sec)
    ↓ [mems_task processes]
    ├→ RMS acceleration (X, Y, Z)
    ├→ RMS velocity (mm/s) via integration
    ├→ FFT: frequency analysis
    ├→ Orientation: pitch/roll from gravity
    ↓ [Send to queue]
    ├→ mqtt_payload_queue → [mqtt_publish_task]
    │                    ↓ [Format to JSON]
    │                    ↓ [Publish to MQTT]
    │
    ├→ websocket_queue → [WebSocket clients]
    │              ↓ [Browser dashboard]
    │
    └→ LocalBuffer → [Next cycle]
```

### Configuration Flow

```
First Boot
    ↓ [Storage loads /config.json]
    ↓ [If missing → create defaults]
    ↓ [load config → SystemConfig struct]
    ↓ [Pass to each handler]
    │
User Updates via Web API
    ↓ [POST /api/config with JSON]
    ↓ [Validate & store]
    ↓ [Handlers reload on next cycle]
    ↓ [SPIFFS persists across power cycles]
```

---

## Compilation Process

1. **Preprocessing** → Macros expanded, includes resolved
2. **Compilation** → Each .cpp compiled to .o object files
3. **Linking** → Objects linked with libraries
4. **Binary Creation** → firmware.bin created
5. **Upload** → USB → ESP32 flash

**Build time:** ~3-5 minutes first build, ~30 sec incremental.

---

## Memory Usage

### RAM Distribution (328 KB total)

- **DRAM**: ~200 KB (heap, stack)
- **IRAM**: ~128 KB (fast code execution)
- **PSRAM** (optional): 4 MB external

### Flash Distribution (4 MB total)

- **OTA App0**: ~1.3 MB (executable)
- **SPIFFS**: ~1.7 MB (filesystem)
- **NVS**: ~64 KB (settings)
- **Partition table**: ~64 KB

### Task Stack Allocation

```
MEMS Task:     8 KB  (high priority, sensor data)
WiFi Task:     8 KB  (network operations)
MQTT Task:     8 KB  (connection management)
MQTT-Pub:      4 KB  (lightweight publishing)
Web Task:      4 KB  (async, non-blocking)
ADC Task:      2 KB  (simple periodic read)
────────────────────
Total:         34 KB
```

---

## Adding New Modules

Template for new task/module:

1. **Create header**: `include/new_module.h`
   ```cpp
   class NewModule {
   public:
       bool begin();
       void update();
   };
   void new_module_task(void* parameter);
   ```

2. **Create source**: `src/new_module.cpp`
   ```cpp
   #include "new_module.h"
   // Implementation...
   void new_module_task(void* parameter) {
       NewModule* obj = (NewModule*)parameter;
       while (1) {
           obj->update();
           vTaskDelayUntil(...);
       }
   }
   ```

3. **Integrate in main.cpp**:
   ```cpp
   NewModule g_new_module;
   g_new_module.begin();
   xTaskCreatePinnedToCore(new_module_task, "NewMod", 4096, &g_new_module, 5, nullptr, 1);
   ```

---

## Development Workflow

### Editing Code

1. Edit `.cpp` or `.h` files
2. PlatformIO auto-detects changes
3. Save triggers incremental compile
4. Any errors shown in Problems panel

### Testing

1. Make code changes
2. `platformio run` builds
3. `platformio run --target upload` deploys
4. `platformio device monitor` watches output

### Debugging

- Use `DEBUG_PRINT()` macro for debug logs
- Pull GPIO27 HIGH to enable debug mode
- Check serial monitor output
- Monitor battery consumption with power meter

---

## Build Variants

### Development
- Full logging: `-D CORE_DEBUG_LEVEL=5`
- No optimization: faster compile
- Sleep disabled (easier testing)

### Production
- Minimal logging: `-D CORE_DEBUG_LEVEL=2`
- Optimized: `-Os -flto` (smaller, faster)
- Sleep enabled, deep power saving

---

**Last Updated**: March 2024
**Project Version**: 1.0.0
**Framework**: Arduino for ESP32 + FreeRTOS
