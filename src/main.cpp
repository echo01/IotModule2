#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <memory>
#include <new>
#include <cstring>

#include "config.h"
#include "common.h"
#include "sensors.h"
#include "wifi_handler.h"
#include "mqtt_handler.h"
#include "web_server.h"
#include "power_management.h"
#include "storage.h"

/* ========== GLOBAL OBJECTS ========== */
// g_debug_mode is defined in common.cpp; declared extern in common.h

MEMSSensor g_mems_sensor;
BatterySensor g_battery_sensor;
WiFiHandler g_wifi_handler;
MQTTHandler g_mqtt_handler;
WebServer g_web_server;
PowerManager g_power_manager;
Storage g_storage;

SystemConfig g_system_config;
SystemStatus g_system_status;

WiFiClient g_wifi_client;

/* ========== QUEUES ========== */
QueueHandle_t g_mems_data_queue = nullptr;
QueueHandle_t g_mqtt_payload_queue = nullptr;
QueueHandle_t g_websocket_queue = nullptr;
TaskHandle_t g_mems_task_handle = nullptr;
TaskHandle_t g_adc_task_handle = nullptr;
TaskHandle_t g_wifi_task_handle = nullptr;
TaskHandle_t g_mqtt_task_handle = nullptr;
TaskHandle_t g_mqtt_publish_task_handle = nullptr;
TaskHandle_t g_web_task_handle = nullptr;

namespace {
constexpr size_t stack_words(size_t bytes) {
    return (bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t);
}

StaticQueue_t g_mems_queue_struct;
uint8_t g_mems_queue_storage[MEMS_DATA_QUEUE_SIZE * sizeof(VibrationAnalysis)];
StaticQueue_t g_mqtt_payload_queue_struct;
uint8_t g_mqtt_payload_queue_storage[MQTT_PAYLOAD_QUEUE_SIZE * sizeof(MQTTPayload)];
StaticQueue_t g_websocket_queue_struct;
uint8_t g_websocket_queue_storage[WEBSOCKET_QUEUE_SIZE * sizeof(VibrationAnalysis)];
StaticEventGroup_t g_event_group_struct;

StaticTask_t g_adc_task_buffer;
StackType_t g_adc_task_stack[stack_words(ADC_TASK_STACK_SIZE)];
StaticTask_t g_mqtt_publish_task_buffer;
StackType_t g_mqtt_publish_task_stack[stack_words(MQTT_PUBLISH_TASK_STACK_SIZE)];
StaticTask_t g_web_task_buffer;
StackType_t g_web_task_stack[stack_words(WEB_TASK_STACK_SIZE)];
}

/* ========== EVENT GROUP ========== */
EventGroupHandle_t g_event_group = nullptr;

#define EVENT_WIFI_CONNECTED    (1 << 0)
#define EVENT_MQTT_CONNECTED    (1 << 1)
#define EVENT_MEMS_DATA_READY   (1 << 2)
#define EVENT_CONFIG_UPDATED    (1 << 3)

static void print_runtime_config_debug() {
    char wifi_pass_mask[sizeof(g_system_config.wifi_password)];
    char ap_pass_mask[sizeof(g_system_config.ap_password)];
    char mqtt_pass_mask[sizeof(g_system_config.mqtt_password)];
    mask_secret_to_buffer(g_system_config.wifi_password, wifi_pass_mask, sizeof(wifi_pass_mask));
    mask_secret_to_buffer(g_system_config.ap_password, ap_pass_mask, sizeof(ap_pass_mask));
    mask_secret_to_buffer(g_system_config.mqtt_password, mqtt_pass_mask, sizeof(mqtt_pass_mask));

    INFO_PRINT("Network config: STA_SSID='%s' STA_PASS='%s' AP_ENABLED=%s AP_SSID='%s' AP_PASS='%s'",
               g_system_config.wifi_ssid,
               wifi_pass_mask,
               g_system_config.wifi_ap_enabled ? "true" : "false",
               g_system_config.ap_ssid,
               ap_pass_mask);
    INFO_PRINT("STA IP mode: %s IP='%s' GW='%s' MASK='%s' DNS1='%s' DNS2='%s'",
               g_system_config.sta_use_static_ip ? "STATIC" : "DHCP",
               g_system_config.sta_static_ip,
               g_system_config.sta_gateway,
               g_system_config.sta_subnet,
               g_system_config.sta_dns1,
               g_system_config.sta_dns2);
    INFO_PRINT("MQTT config: PROTOCOL='%s' BROKER='%s' PORT=%u CLIENT_ID='%s' USER='%s' PASS='%s' MAIN='%s' FFT_X='%s' FFT_Y='%s' FFT_Z='%s' SUB='%s' INTERVAL=%us",
               g_system_config.mqtt_use_tls ? "MQTTS" : "MQTT",
               g_system_config.mqtt_broker,
               g_system_config.mqtt_port,
               g_system_config.mqtt_client_id,
               g_system_config.mqtt_username,
               mqtt_pass_mask,
               g_system_config.mqtt_topic_publish,
               g_system_config.mqtt_topic_fft_x,
               g_system_config.mqtt_topic_fft_y,
               g_system_config.mqtt_topic_fft_z,
               g_system_config.mqtt_topic_subscribe,
               g_system_config.mqtt_publish_interval_s);
    log_heap_state("SETUP_CONFIG");
}

static void log_stack_watermark(const char* task_name) {
    UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
    if (watermark < STACK_LOW_WATERMARK_WORDS) {
        ERROR_PRINT("%s task stack low: %u words free", task_name, static_cast<unsigned>(watermark));
    }
}

static void log_task_stack_snapshot(const char* task_name, TaskHandle_t handle) {
    if (handle == nullptr) {
        return;
    }

    INFO_PRINT("[STACK] %s watermark=%u words free",
               task_name,
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(handle)));
}

static void log_all_task_stack_snapshots() {
    log_task_stack_snapshot("MEMS", g_mems_task_handle);
    log_task_stack_snapshot("ADC", g_adc_task_handle);
    log_task_stack_snapshot("WiFi", g_wifi_task_handle);
    log_task_stack_snapshot("MQTT", g_mqtt_task_handle);
    log_task_stack_snapshot("MQTT-Pub", g_mqtt_publish_task_handle);
    log_task_stack_snapshot("Web", g_web_task_handle);
}

/* ========== MEMS TASK ========== */

void mems_task(void* parameter) {
    (void)parameter;
    const uint32_t STARTUP_DELAY = MEMS_STARTUP_DELAY_MS;
    uint32_t task_start_time = millis();
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t sample_count = 0;
    
    INFO_PRINT("MEMS task started");
    
    while (1) {
        if (g_mqtt_handler.isTlsConnectInProgress()) {
            DEBUG_PRINT("MEMS task paused during MQTTS connect mode");
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
            continue;
        }

        // Wait for startup delay to allow sensor to settle
        if ((millis() - task_start_time) < STARTUP_DELAY) {
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
            continue;
        }
        
        // Read sensor data
        std::unique_ptr<MEMSData> raw_data(new (std::nothrow) MEMSData());
        if (!raw_data) {
            ERROR_PRINT("Failed to allocate MEMS buffer on heap (largest=%u)", get_largest_free_block());
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
            continue;
        }

        if (g_mems_sensor.readRawData(*raw_data)) {
            raw_data->timestamp_us = micros();
            
            // Process vibration analysis
            VibrationAnalysis analysis;
            if (g_mems_sensor.processVibrationData(*raw_data, analysis)) {
                analysis.timestamp_us = raw_data->timestamp_us;
                sample_count++;
                
                // Send to queue for MQTT processing
                if (g_mems_data_queue) {
                    if (xQueueSend(g_mems_data_queue, &analysis, 0) != pdTRUE) {
                        if (g_debug_mode) {
                            ERROR_PRINT("MEMS data queue full");
                        }
                    }
                }

                // Send to queue for WebSocket/UI updates
                if (g_websocket_queue) {
                    if (xQueueSend(g_websocket_queue, &analysis, 0) != pdTRUE) {
                        if (g_debug_mode) {
                            ERROR_PRINT("WebSocket data queue full");
                        }
                    }
                }
                
                // Signal WebSocket task
                if (g_event_group) {
                    xEventGroupSetBits(g_event_group, EVENT_MEMS_DATA_READY);
                }
                
                if (g_debug_mode && (sample_count % 10 == 0)) {
                    DEBUG_PRINT("MEMS samples: %u", sample_count);
                }
            }
        }

        log_stack_watermark("MEMS");
        
        // Update every 2-3 seconds (sampling period)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(2500));
    }
    
    vTaskDelete(NULL);
}

/* ========== ADC TASK ========== */

void adc_task(void* parameter) {
    (void)parameter;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    INFO_PRINT("ADC task started");
    
    while (1) {
        // Read battery voltage
        g_system_status.battery_voltage = g_battery_sensor.readVoltage();
        
        // Check low battery
        if (g_battery_sensor.isBatteryLow()) {
            if (g_debug_mode) {
                ERROR_PRINT("Low battery warning: %.2f V", g_system_status.battery_voltage);
            }
        }

        log_stack_watermark("ADC");
        
        // Update every 30 seconds
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(30000));
    }
    
    vTaskDelete(NULL);
}

/* ========== MQTT DATA PROCESSOR TASK ========== */

void mqtt_publish_task(void* parameter) {
    (void)parameter;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t last_publish = millis();
    uint16_t publish_interval_s = g_system_config.mqtt_publish_interval_s;
    uint32_t publish_interval_ms = static_cast<uint32_t>(publish_interval_s) * 1000UL;
    g_mqtt_handler.setNextPublishDueMs(last_publish + publish_interval_ms);
    
    VibrationAnalysis latest_analysis = {};
    bool has_valid_analysis = false;
    
    INFO_PRINT("MQTT publish task started");
    
    while (1) {
        // Receive latest MEMS data (drain queue and keep newest sample)
        VibrationAnalysis queued_analysis = {};
        while (g_mems_data_queue && xQueueReceive(g_mems_data_queue, &queued_analysis, 0) == pdTRUE) {
            latest_analysis = queued_analysis;
            has_valid_analysis = (latest_analysis.timestamp_us != 0);
        }
        
        // Publish if connected and interval elapsed
        uint32_t now = millis();
        if (publish_interval_s != g_system_config.mqtt_publish_interval_s) {
            publish_interval_s = g_system_config.mqtt_publish_interval_s;
            publish_interval_ms = static_cast<uint32_t>(publish_interval_s) * 1000UL;
            g_mqtt_handler.setPublishInterval(publish_interval_s);
        }
        g_mqtt_handler.setNextPublishDueMs(last_publish + publish_interval_ms);

        if (has_valid_analysis &&
            g_mqtt_handler.isConnected() && 
            !g_mqtt_handler.isTlsConnectInProgress() &&
            (now - last_publish) >= publish_interval_ms) {
            
            // Build MQTT payload
            MQTTPayload payload = {};
            payload.timestamp_us = latest_analysis.timestamp_us;
            payload.vibration = latest_analysis;
            payload.battery_voltage = g_system_status.battery_voltage;
            payload.wifi_rssi = g_system_status.wifi_rssi;
            payload.uptime_ms = millis();

            // Attach FFT spectrum output (x/y/z) into raw arrays for MQTT payload.
            uint16_t points_x = 0;
            uint16_t points_y = 0;
            uint16_t points_z = 0;
            g_mems_sensor.getFFTSpectrum('x', payload.raw_freq_hz, payload.raw_accel_x, MQTT_FFT_POINTS, points_x);
            g_mems_sensor.getFFTSpectrum('y', payload.raw_freq_hz, payload.raw_accel_y, MQTT_FFT_POINTS, points_y);
            g_mems_sensor.getFFTSpectrum('z', payload.raw_freq_hz, payload.raw_accel_z, MQTT_FFT_POINTS, points_z);
            payload.raw_sample_count = points_x;
            if (points_y < payload.raw_sample_count) payload.raw_sample_count = points_y;
            if (points_z < payload.raw_sample_count) payload.raw_sample_count = points_z;
            
            // Publish
            if (g_mqtt_handler.publishVibrationData(latest_analysis, payload)) {
                last_publish = now;
                g_mqtt_handler.setNextPublishDueMs(last_publish + publish_interval_ms);
                g_system_status.data_sent_count++;
                
                if (g_debug_mode) {
                    DEBUG_PRINT("MQTT published, count: %u", g_system_status.data_sent_count);
                }
            }
        }
        
        // Update WiFi RSSI
        if (g_wifi_handler.isConnected()) {
            g_system_status.wifi_rssi = g_wifi_handler.getRSSI();
            g_system_status.wifi_status = WIFI_CONNECTED;
        } else {
            g_system_status.wifi_status = g_wifi_handler.getStatus();
            g_system_status.wifi_rssi = -100;
        }
        
        // Update MQTT status
        if (g_mqtt_handler.isConnected()) {
            g_system_status.mqtt_status = MQTTSTATUS_CONNECTED;
        } else {
            g_system_status.mqtt_status = MQTTSTATUS_DISCONNECTED;
        }

        log_stack_watermark("MQTT-Pub");
        
        // Update every 5 seconds
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}

/* ========== SETUP FUNCTION ========== */

void setup() {
    // Initialize Serial
    Serial.begin(SERIAL_BAUD);
    delay(100);
    Serial.printf("[INFO] Starting ESP32 IoT Module\n");
    INFO_PRINT("\n\n===== ESP32 IoT Module Starting =====");
    INFO_PRINT("Build time: %s %s", __DATE__, __TIME__);
    
    /* ========== GPIO INITIALIZATION ========== */
    
    // Mode switch & status LEDs
    pinMode(GPIO_MODE_SWITCH, INPUT_PULLUP);
    pinMode(GPIO_LED_STATUS, OUTPUT);
    pinMode(GPIO_MQTT_STATUS, OUTPUT);
    pinMode(GPIO_ADXL345_INT1, INPUT_PULLUP);
    pinMode(GPIO_ADXL345_INT2, INPUT_PULLUP);
    digitalWrite(GPIO_LED_STATUS, HIGH);   // Light LED on wakeup
    digitalWrite(GPIO_MQTT_STATUS, LOW);
    
    // Check debug mode
    delay(100);
    g_debug_mode = (digitalRead(GPIO_MODE_SWITCH) == HIGH);
    if (g_debug_mode) {
        INFO_PRINT("DEBUG MODE ENABLED (GPIO27 = HIGH)");
    } else {
        INFO_PRINT("NORMAL MODE (GPIO27 = LOW)");
    }
    
    /* ========== WAKEUP REASON ========== */
    
    g_system_status.wakeup_reason = get_wakeup_reason();
    const char* wakeup_str = "UNKNOWN";
    switch (g_system_status.wakeup_reason) {
        case WAKE_TIMER: wakeup_str = "TIMER"; break;
        case WAKE_EXT_INT: wakeup_str = "EXT_INT"; break;
        default: g_system_status.wakeup_reason = WAKE_FIRST_BOOT; wakeup_str = "FIRST_BOOT"; break;
    }
    INFO_PRINT("Wakeup reason: %s", wakeup_str);
    
    /* ========== STORAGE INITIALIZATION ========== */
    
    if (!g_storage.begin()) {
        ERROR_PRINT("Storage initialization failed!");
        while (1) delay(1000);
    }
    
    // Load configuration
    if (!g_storage.loadConfig(g_system_config)) {
        ERROR_PRINT("Failed to load config, using defaults");
        g_system_config = Storage::createDefaultConfig();
    }
    g_log_enabled = g_system_config.log_enabled;
    print_runtime_config_debug();
    
    /* ========== SENSOR INITIALIZATION ========== */
    
    if (!g_mems_sensor.begin()) {
        ERROR_PRINT("MEMS sensor initialization failed!");
        // Continue anyway, but in error state
    } else {
        g_mems_sensor.setDataRate(g_system_config.adxl345_rate_hz);
        g_mems_sensor.setRange(g_system_config.adxl345_range_g);
        g_mems_sensor.setOffset(g_system_config.adxl345_offset_x,
                                g_system_config.adxl345_offset_y,
                                g_system_config.adxl345_offset_z);
        g_mems_sensor.setInterruptThreshold(g_system_config.adxl345_int_threshold_mg);
        if (g_system_config.adxl345_int_enabled) {
            g_mems_sensor.setupInterrupt(GPIO_ADXL345_INT1, true);
        } else {
            g_mems_sensor.setupInterrupt(GPIO_ADXL345_INT1, false);
        }
    }
    
    if (!g_battery_sensor.begin()) {
        ERROR_PRINT("Battery sensor initialization failed!");
    }
    
    INFO_PRINT("Sensors initialized");
    
    /* ========== POWER MANAGEMENT ========== */
    
    if (!g_power_manager.begin()) {
        ERROR_PRINT("Power manager initialization failed!");
    }
    
    // Disable sleep in debug mode
    if (g_debug_mode) {
        g_power_manager.disableSleep();
    }
    
    /* ========== FREERTOS QUEUE INITIALIZATION ========== */
    
    g_mems_data_queue = xQueueCreateStatic(
        MEMS_DATA_QUEUE_SIZE,
        sizeof(VibrationAnalysis),
        g_mems_queue_storage,
        &g_mems_queue_struct);
    g_mqtt_payload_queue = xQueueCreateStatic(
        MQTT_PAYLOAD_QUEUE_SIZE,
        sizeof(MQTTPayload),
        g_mqtt_payload_queue_storage,
        &g_mqtt_payload_queue_struct);
    g_websocket_queue = xQueueCreateStatic(
        WEBSOCKET_QUEUE_SIZE,
        sizeof(VibrationAnalysis),
        g_websocket_queue_storage,
        &g_websocket_queue_struct);
    g_event_group = xEventGroupCreateStatic(&g_event_group_struct);
    
    if (!g_mems_data_queue || !g_websocket_queue || !g_event_group) {
        ERROR_PRINT("Failed to create queues/event group");
        while (1) delay(1000);
    }
    
    /* ========== WIFI INITIALIZATION ========== */
    
    if (!g_wifi_handler.begin(g_system_config)) {
        ERROR_PRINT("WiFi initialization failed!");
    }
    
    /* ========== MQTT INITIALIZATION ========== */
    
    if (!g_mqtt_handler.begin(&g_wifi_client, g_system_config)) {
        ERROR_PRINT("MQTT initialization failed!");
    }
    
    /* ========== WEB SERVER INITIALIZATION ========== */
    
    g_web_server.setupFileHandlers();
    if (!g_web_server.begin(WEB_SERVER_PORT)) {
        ERROR_PRINT("Web server initialization failed!");
    }
    
    INFO_PRINT("Web server available at http://%s", g_wifi_handler.getIPAddress().c_str());
    
    /* ========== FREERTOS TASKS ========== */
    
    // MEMS sensor task (high priority, core 0)
    xTaskCreatePinnedToCore(
        mems_task,
        "MEMS",
        MEMS_TASK_STACK_SIZE,
        nullptr,
        MEMS_TASK_PRIORITY,
        &g_mems_task_handle,
        MEMS_TASK_CORE
    );
    
    // ADC battery monitor task
    g_adc_task_handle = xTaskCreateStaticPinnedToCore(
        adc_task,
        "ADC",
        ADC_TASK_STACK_SIZE,
        nullptr,
        ADC_TASK_PRIORITY,
        g_adc_task_stack,
        &g_adc_task_buffer,
        ADC_TASK_CORE
    );
    
    // WiFi handler task
    xTaskCreatePinnedToCore(
        wifi_task,
        "WiFi",
        WIFI_TASK_STACK_SIZE,
        &g_wifi_handler,
        WIFI_TASK_PRIORITY,
        &g_wifi_task_handle,
        WIFI_TASK_CORE
    );
    
    // MQTT handler task
    xTaskCreatePinnedToCore(
        mqtt_task,
        "MQTT",
        MQTT_TASK_STACK_SIZE,
        &g_mqtt_handler,
        MQTT_TASK_PRIORITY,
        &g_mqtt_task_handle,
        MQTT_TASK_CORE
    );
    
    // MQTT publish task
    g_mqtt_publish_task_handle = xTaskCreateStaticPinnedToCore(
        mqtt_publish_task,
        "MQTT-Pub",
        MQTT_PUBLISH_TASK_STACK_SIZE,
        nullptr,
        MQTT_TASK_PRIORITY + 1,
        g_mqtt_publish_task_stack,
        &g_mqtt_publish_task_buffer,
        MQTT_TASK_CORE
    );
    
    // Web server task
    g_web_task_handle = xTaskCreateStaticPinnedToCore(
        web_task,
        "Web",
        WEB_TASK_STACK_SIZE,
        &g_web_server,
        WEB_TASK_PRIORITY,
        g_web_task_stack,
        &g_web_task_buffer,
        WEB_TASK_CORE
    );
    
    INFO_PRINT("All tasks created");
    log_heap_state("TASKS_CREATED");
    INFO_PRINT("===== Setup Complete =====\n");
    
    // Print system info if debug
    print_system_info(g_system_status);
}

/* ========== MAIN LOOP ========== */

void loop() {
    // Read GPIO mode switch
    static uint32_t last_mode_check = 0;
    static uint32_t last_sent_count = 0;
    static uint32_t last_stack_log = 0;
    uint32_t now = millis();
    
    // Check mode switch periodically (every 5 seconds)
    if ((now - last_mode_check) > 5000) {
        last_mode_check = now;
        
        bool new_debug_mode = (digitalRead(GPIO_MODE_SWITCH) == HIGH);
        if (new_debug_mode != g_debug_mode) {
            g_debug_mode = new_debug_mode;
            if (g_debug_mode) {
                INFO_PRINT("=== ENTERING DEBUG MODE ===");
                g_power_manager.disableSleep();
            } else {
                INFO_PRINT("=== EXITING DEBUG MODE ===");
                g_power_manager.enableSleep();
            }
        }
    }

    // ADXL345 interrupt outputs are active LOW; read INT_SOURCE (0x30) to clear latched signals.
    if (g_system_config.adxl345_int_enabled &&
        (digitalRead(GPIO_ADXL345_INT1) == LOW || digitalRead(GPIO_ADXL345_INT2) == LOW)) {
        uint8_t int_source = 0;
        if (g_mems_sensor.clearInterruptSource(&int_source)) {
            DEBUG_PRINT("ADXL345 interrupt cleared from INT_SOURCE=0x%02X", int_source);
        }
    }
    
    // Process WebSocket updates if data available
    if (xEventGroupGetBits(g_event_group) & EVENT_MEMS_DATA_READY) {
        VibrationAnalysis analysis = {};
        if (uxQueueMessagesWaiting(g_websocket_queue) > 0) {
            xQueueReceive(g_websocket_queue, &analysis, 0);
            
            // Update WebSocket clients
            g_web_server.updateRealtimeData(analysis, g_system_status);
        }
        xEventGroupClearBits(g_event_group, EVENT_MEMS_DATA_READY);
    }
    
    // Enter deep sleep after at least one successful publish in normal mode.
    if (!g_debug_mode && g_power_manager.isSleepEnabled()) {
        if (g_system_status.data_sent_count > last_sent_count) {
            last_sent_count = g_system_status.data_sent_count;
            INFO_PRINT("Publish completed, arming deep sleep with ADXL345 wake interrupt");
            g_power_manager.enterDeepSleep(g_system_config.sleep_interval_sec);
        }
    }

    if ((now - last_stack_log) > 60000) {
        last_stack_log = now;
        log_all_task_stack_snapshots();
    }
    
    delay(100);  // Yield to other tasks
}
