#include "common.h"
#include <esp_sleep.h>
#include <esp_heap_caps.h>

/* Global debug mode flag - set based on GPIO27 */
bool g_debug_mode = false;
bool g_log_enabled = true;

const char* wakeup_reason_to_string(WakeupReason reason) {
    switch (reason) {
        case WAKE_TIMER: return "TIMER";
        case WAKE_EXT_INT: return "EXT_INT";
        case WAKE_EXT_INT_MOTION: return "EXT_INT_MOTION";
        case WAKE_MODE_SWITCH: return "MODE_SWITCH";
        case WAKE_UNKNOWN: return "UNKNOWN";
        case WAKE_FIRST_BOOT: return "FIRST_BOOT";
        default: return "UNKNOWN";
    }
}

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
            return WAKE_MODE_SWITCH;
        case ESP_SLEEP_WAKEUP_EXT1:
            return WAKE_EXT_INT_MOTION;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            return WAKE_UNKNOWN;
    }
}

void print_system_info(const SystemStatus& status) {
    if (!g_debug_mode) return;
    
    const char* mqtt_str = "DISCONNECTED";
    switch (status.mqtt_status) {
        case MQTTSTATUS_DISCONNECTED: mqtt_str = "DISCONNECTED"; break;
        case MQTTSTATUS_CONNECTING: mqtt_str = "CONNECTING"; break;
        case MQTTSTATUS_CONNECTED: mqtt_str = "CONNECTED"; break;
        case MQTTSTATUS_ERROR: mqtt_str = "ERROR"; break;
    }
    
    DEBUG_PRINT("=== SYSTEM INFO ===");
    DEBUG_PRINT("Wakeup Reason: %s", wakeup_reason_to_string(status.wakeup_reason));
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

uint32_t get_largest_free_block() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void log_heap_state(const char* context) {
    INFO_PRINT("[%s] Heap free=%u largest=%u minimum=%u",
               context,
               esp_get_free_heap_size(),
               get_largest_free_block(),
               heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

void mask_secret_to_buffer(const char* value, char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (value == nullptr) {
        return;
    }

    const size_t len = strlen(value);
    if (len == 0) {
        return;
    }

    if (len <= 2) {
        strlcpy(out, "**", out_size);
        return;
    }

    out[0] = value[0];
    size_t pos = 1;
    while (pos < (len - 1) && pos < (out_size - 2)) {
        out[pos++] = '*';
    }
    if (pos < out_size - 1) {
        out[pos++] = value[len - 1];
    }
    out[pos] = '\0';
}
