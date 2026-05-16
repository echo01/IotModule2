#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ========== GPIO CONFIGURATION ========== */
#define GPIO_MODE_SWITCH        27     // HIGH=Debug/Always ON, LOW=Normal (sleep enabled)
#define GPIO_LED_STATUS         32     // LED ON at wakeup
#define GPIO_MQTT_STATUS        33     // Blink when MQTT payload sent (0.1s period)
#define GPIO_BATTERY_ADC        34     // ADC for battery voltage (100k/100k divider)
#define GPIO_ADXL345_INT1       25     // ADXL345 interrupt 1 (avoid boot-strap pins)
#define GPIO_ADXL345_INT2       26     // ADXL345 interrupt 2
#define GPIO_DEBUG_SAMPLE_PULSE 23     // Debug pulse output for oscilloscope timing checks

/* ========== I2C CONFIGURATION ========== */
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22
#define I2C_FREQUENCY           400000  // 400 kHz (ADXL345 I2C max)
#define ADXL345_I2C_ADDRESS     0x53   // ADXL345 default I2C address (ALT ADDRESS LOW)

/* ========== SERIAL CONFIGURATION ========== */
#define SERIAL_BAUD             115200
#define SERIAL_TX_BUFFER        4096

/* ========== TIMING & POWER ========== */
#define DEEP_SLEEP_INTERVAL_SEC (60 * 60)  // 1 hour wake timer
#define MEMS_SAMPLING_RATE      800        // Hz
#define MEMS_SAMPLE_COUNT       1024       // FFT-sized sampling window to keep MQTTS heap stable
#define MEMS_STARTUP_DELAY_MS   2000       // Skip first 2 seconds

/* ========== ADXL345 SETTINGS ========== */
#define ADXL345_DATA_RATE_HZ    800
#define ADXL345_RANGE_G         16
#define ADXL345_OFFSET_X        0.0f
#define ADXL345_OFFSET_Y        0.0f
#define ADXL345_OFFSET_Z        0.0f

/* ========== MQTT SETTINGS ========== */
#define MQTT_BUFFER_SIZE        1024
#define MQTT_MAX_PAYLOAD_SIZE   16384    // For large FFT data
#define MQTT_FFT_POINTS         32       // Keep each FFT JSON packet below MQTT_BUFFER_SIZE
#define MQTT_RECONNECT_DELAY_MS 5000
#define MQTT_PUBLISH_INTERVAL_S 60       // Default 60 sec (configurable 30-3600)
#define MQTTS_CONNECT_QUIESCE_MS 3500    // Let MEMS release large buffers before TLS handshake
#ifndef MQTT_SOCKET_TIMEOUT
#define MQTT_SOCKET_TIMEOUT     10
#endif
#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE          60
#endif

/* ========== WEB SERVER ========== */
#define WEB_SERVER_PORT         80
#define WEBSOCKET_BUFFER_SIZE   8192

/* ========== DISCOVERY ========== */
#define DISCOVERY_UDP_PORT      37020
#define DISCOVERY_SERVICE_NAME  "iot-sensor"
#define DISCOVERY_PROTOCOL_ID   "viot-discovery-v1"

/* ========== FFT SETTINGS ========== */
#define FFT_SIZE                1024      // FFT samples
#define FFT_FREQUENCY_RESOLUTION (MEMS_SAMPLING_RATE / FFT_SIZE)
#define RMS_HIGHPASS_CUTOFF_HZ  10.0f

/* ========== VIBRATION SIGNAL VALIDATION ========== */
#define VIBRATION_MIN_RMS_G         0.02f   // Minimum RMS acceleration for valid vibration
#define VIBRATION_MIN_PEAK_G        0.05f   // Minimum peak acceleration for valid vibration
#define VIBRATION_NOISE_FLOOR_DB    -20.0f  // FFT noise floor threshold (dB)
#define VIBRATION_DEADBAND_G        0.005f  // Zero-crossing deadband threshold
#define VIBRATION_MIN_FREQUENCY_HZ  5.0f    // Minimum detectable frequency
#define VIBRATION_MAX_FREQUENCY_HZ  1000.0f // Maximum detectable frequency

/* ========== BATTERY MONITORING ========== */
#define ADC_RESOLUTION          12        // bits (0-4095)
#define ADC_REFERENCE_VOLTAGE   3.3f      // V
#define VOLTAGE_DIVIDER_RATIO   2.0f      // 100k/100k
#define BATTERY_ADC_CALIBRATION_FACTOR 1.0732f // Empirical trim: 3.81V meter / 3.55V ADC
#define BATTERY_LOW_THRESHOLD_V 3.0f      // Low battery warning

/* ========== ACCELERATION TO VELOCITY CONVERSION ========== */
#define GRAVITY_ACCELERATION    9.81f     // m/s²
#define G_TO_MMS2              9810.0f    // 1G = 9810 mm/s²

/* ========== STORAGE ========== */
#define CONFIG_FILE_PATH        "/config.json"
#define SPIFFS_SIZE             (1024 * 1024)  // 1MB
#define CONFIG_MAX_SIZE         4096

/* ========== TASK SETTINGS (FreeRTOS) ========== */
#define MEMS_TASK_PRIORITY      8          // High priority for timing-sensitive task
#define MEMS_TASK_STACK_SIZE    8192
#define MEMS_TASK_CORE          0

#define WIFI_TASK_PRIORITY      5
#define WIFI_TASK_STACK_SIZE    6144
#define WIFI_TASK_CORE          1

#define MQTT_TASK_PRIORITY      6
#define MQTT_TASK_STACK_SIZE    6144
#define MQTT_TASK_CORE          1

#define MQTT_PUBLISH_TASK_STACK_SIZE 5120

#define WEB_TASK_PRIORITY       4
#define WEB_TASK_STACK_SIZE     6144
#define WEB_TASK_CORE           1

#define ADC_TASK_PRIORITY       2
#define ADC_TASK_STACK_SIZE     3072
#define ADC_TASK_CORE           1

#define STACK_LOW_WATERMARK_WORDS 512
#define WIFI_CONNECT_MAX_RETRIES 5
#define WIFI_CONNECT_TIMEOUT_MS  5000
#define WIFI_RETRY_DELAY_MS      1000
#define MQTT_CONNECT_MAX_RETRIES 5

/* ========== QUEUE SIZES ========== */
#define MEMS_DATA_QUEUE_SIZE    10
#define MQTT_PAYLOAD_QUEUE_SIZE 1
#define WEBSOCKET_QUEUE_SIZE    8

/* ========== DEBUG SETTINGS ========== */
#define DEBUG_MAX_LOG_LINE      256

/* ========== CONSTANTS ========== */
// Use Arduino's built-in PI macro from Arduino.h (avoid redefinition conflicts)

#endif // CONFIG_H
