#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <cstring>
#include <Arduino.h>
#include "config.h"

/* ========== GLOBAL DEBUG FLAG ========== */
extern bool g_debug_mode;

/* ========== DEBUG MACROS ========== */
#define DEBUG_PRINT(fmt, ...) do { \
    if (g_debug_mode) { \
        Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } \
} while(0)

#define ERROR_PRINT(fmt, ...) do { \
    Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); \
} while(0)

#define INFO_PRINT(fmt, ...) do { \
    Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__); \
} while(0)

/* ========== DATA STRUCTURES ========== */

enum WakeupReason {
    WAKE_TIMER,
    WAKE_EXT_INT,
    WAKE_UNKNOWN,
    WAKE_FIRST_BOOT
};

enum WiFiStatus {
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED,
    AP_ONLY,
    WIFI_INIT
};

enum MQTTStatus {
    MQTTSTATUS_DISCONNECTED,
    MQTTSTATUS_CONNECTING,
    MQTTSTATUS_CONNECTED,
    MQTTSTATUS_ERROR
};

/* MEMS Sensor Data */
struct MEMSData {
    uint64_t timestamp_us;
    uint32_t sample_count;
    float accel_x[MEMS_SAMPLE_COUNT];
    float accel_y[MEMS_SAMPLE_COUNT];
    float accel_z[MEMS_SAMPLE_COUNT];
};

/* Processed Vibration Analysis */
struct VibrationAnalysis {
    uint64_t timestamp_us;
    
    // RMS Acceleration (G)
    float rms_accel_x;
    float rms_accel_y;
    float rms_accel_z;
    
    // RMS Velocity (mm/s)
    float rms_velocity_x;
    float rms_velocity_y;
    float rms_velocity_z;

    // Vibration Frequency (Hz, dominant/peak per axis)
    float vibration_freq_x;
    float vibration_freq_y;
    float vibration_freq_z;
    
    // FFT Peak Frequency (Hz)
    float fft_peak_freq_x;
    float fft_peak_freq_y;
    float fft_peak_freq_z;
    
    // FFT Power (dB)
    float fft_power_x;
    float fft_power_y;
    float fft_power_z;
    
    // Temperature (if sensor supports it)
    float temperature;
    
    // Pitch & Roll (from accelerometer)
    float pitch;
    float roll;
};

/* System Status */
struct SystemStatus {
    WakeupReason wakeup_reason;
    WiFiStatus wifi_status;
    MQTTStatus mqtt_status;
    
    float battery_voltage;
    int8_t wifi_rssi;
    uint32_t uptime_seconds;
    
    bool debug_mode;
    uint32_t data_sent_count;
};

/* Configuration (stored in JSON) */
struct SystemConfig {
    // WiFi
    char wifi_ssid[64];
    char wifi_password[64];
    bool wifi_ap_enabled;
    char ap_ssid[32];
    char ap_password[64];
    bool sta_use_static_ip;
    char sta_static_ip[16];
    char sta_gateway[16];
    char sta_subnet[16];
    char sta_dns1[16];
    char sta_dns2[16];
    
    // MQTT
    char mqtt_broker[256];
    uint16_t mqtt_port;
    char mqtt_client_id[64];
    char mqtt_username[64];
    char mqtt_password[128];
    char mqtt_topic_publish[256];  // Main payload topic
    char mqtt_topic_fft_x[256];
    char mqtt_topic_fft_y[256];
    char mqtt_topic_fft_z[256];
    char mqtt_topic_subscribe[256];
    uint16_t mqtt_publish_interval_s;
    
    bool mqtt_use_tls;
    bool mqtt_aws_iot_enabled;
    
    // ADXL345
    uint16_t adxl345_rate_hz;
    uint8_t adxl345_range_g;
    float adxl345_offset_x;
    float adxl345_offset_y;
    float adxl345_offset_z;
    uint16_t adxl345_int_threshold_mg;
    bool adxl345_int_enabled;
    
    // Vibration signal validation
    float vibration_min_rms_g;
    float vibration_min_peak_g;
    float vibration_noise_floor_db;
    float vibration_deadband_g;
    float vibration_min_freq_hz;
    float vibration_max_freq_hz;
    
    // Power
    bool sleep_enabled;
    uint32_t sleep_interval_sec;
};

/* ========== GLOBAL SYSTEM CONFIG ========== */
extern SystemConfig g_system_config;

/* MQTT Payload */
struct MQTTPayload {
    uint64_t timestamp_us;
    
    // Vibration data
    VibrationAnalysis vibration;
    
    // Raw acceleration samples (subset)
    float raw_accel_x[100];
    float raw_accel_y[100];
    float raw_accel_z[100];
    float raw_freq_hz[100];
    uint32_t raw_sample_count;
    
    // System info
    float battery_voltage;
    int8_t wifi_rssi;
    int32_t uptime_ms;
};

/* Wakeup Info */
struct WakeupInfo {
    WakeupReason reason;
    uint32_t ext_int_pin;
    uint64_t sleep_duration_us;
};

/* ========== FUNCTION DECLARATIONS ========== */

// Get wakeup reason from RTC memory or hardware
WakeupReason get_wakeup_reason();

// Print system information
void print_system_info(const SystemStatus& status);

// Get current system status
SystemStatus get_system_status();

// Timestamp helpers
uint64_t millis_to_us(uint32_t ms);
uint32_t us_to_ms(uint64_t us);

const char* wifi_status_to_string(WiFiStatus status);

#endif // COMMON_H
