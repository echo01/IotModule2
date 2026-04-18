#include "common.h"
#include <esp_sleep.h>

/* Global debug mode flag - set based on GPIO27 */
bool g_debug_mode = false;

const char* wifi_status_to_string(WiFiStatus status) {
    switch (status) {
        case WIFI_INIT: return "WIFI_INIT";
        case WIFI_CONNECTING: return "WIFI_CONNECTING";
        case WIFI_CONNECTED: return "WIFI_CONNECTED";
        case WIFI_FAILED: return "WIFI_FAILED";
        case AP_ONLY: return "AP_ONLY";
        default: return "UNKNOWN";
    }
}

WakeupReason get_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return WAKE_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            return WAKE_EXT_INT;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            return WAKE_UNKNOWN;
    }
}

void print_system_info(const SystemStatus& status) {
    if (!g_debug_mode) return;
    
    const char* wakeup_str = "UNKNOWN";
    switch (status.wakeup_reason) {
        case WAKE_TIMER: wakeup_str = "TIMER"; break;
        case WAKE_EXT_INT: wakeup_str = "EXT_INT"; break;
        case WAKE_UNKNOWN: wakeup_str = "UNKNOWN"; break;
        case WAKE_FIRST_BOOT: wakeup_str = "FIRST_BOOT"; break;
    }
    
    const char* mqtt_str = "DISCONNECTED";
    switch (status.mqtt_status) {
        case MQTTSTATUS_DISCONNECTED: mqtt_str = "DISCONNECTED"; break;
        case MQTTSTATUS_CONNECTING: mqtt_str = "CONNECTING"; break;
        case MQTTSTATUS_CONNECTED: mqtt_str = "CONNECTED"; break;
        case MQTTSTATUS_ERROR: mqtt_str = "ERROR"; break;
    }
    
    DEBUG_PRINT("=== SYSTEM INFO ===");
    DEBUG_PRINT("Wakeup Reason: %s", wakeup_str);
    DEBUG_PRINT("WiFi Status: %s", wifi_status_to_string(status.wifi_status));
    DEBUG_PRINT("WiFi RSSI: %d dBm", status.wifi_rssi);
    DEBUG_PRINT("MQTT Status: %s", mqtt_str);
    DEBUG_PRINT("Battery: %.2f V", status.battery_voltage);
    DEBUG_PRINT("Uptime: %u sec", status.uptime_seconds);
    DEBUG_PRINT("Data Sent: %u payloads", status.data_sent_count);
    DEBUG_PRINT("====================");
}

SystemStatus get_system_status() {
    SystemStatus status = {};
    status.debug_mode = g_debug_mode;
    status.uptime_seconds = millis() / 1000;
    status.wakeup_reason = get_wakeup_reason();
    status.wifi_status = WIFI_INIT;
    status.wifi_rssi = -100;  // Will be updated by WiFi task
    status.battery_voltage = 0.0f;  // Will be updated by ADC task
    return status;
}

uint64_t millis_to_us(uint32_t ms) {
    return (uint64_t)ms * 1000;
}

uint32_t us_to_ms(uint64_t us) {
    return (uint32_t)(us / 1000);
}
