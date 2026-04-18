#include "mqtt_handler.h"
#include <ArduinoJson.h>
#include "wifi_handler.h"

extern WiFiHandler g_wifi_handler;

MQTTHandler::MQTTHandler() 
    : current_status(MQTTSTATUS_DISCONNECTED),
      last_publish_time(0),
      publish_interval_s(MQTT_PUBLISH_INTERVAL_S) {
}

MQTTHandler::~MQTTHandler() {
    disconnect();
}

bool MQTTHandler::begin(WiFiClient* client, const SystemConfig& config) {
    this->config = config;
    
    // Initialize MQTT client
    mqtt_client.setClient(*client);
    mqtt_client.setServer(config.mqtt_broker, config.mqtt_port);
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setCallback(mqtt_callback);
    
    INFO_PRINT("MQTT Handler initialized for %s:%u", 
               config.mqtt_broker, config.mqtt_port);
    
    return true;
}

bool MQTTHandler::connect() {
    if (mqtt_client.connected()) {
        current_status = MQTTSTATUS_CONNECTED;
        return true;
    }
    
    if (strlen(config.mqtt_broker) == 0) {
        ERROR_PRINT("MQTT broker not configured");
        return false;
    }
    
    INFO_PRINT("Connecting to MQTT broker %s:%u...", 
               config.mqtt_broker, config.mqtt_port);
    
    current_status = MQTTSTATUS_CONNECTING;
    
    // Get client ID if not set
    String client_id = config.mqtt_client_id;
    if (client_id.length() == 0) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        client_id = String("ESP32_") + String(mac[3], HEX) + 
                   String(mac[4], HEX) + String(mac[5], HEX);
    }
    
    bool connected = false;
    
    if (strlen(config.mqtt_username) > 0) {
        connected = mqtt_client.connect(
            client_id.c_str(),
            config.mqtt_username,
            config.mqtt_password
        );
    } else {
        connected = mqtt_client.connect(client_id.c_str());
    }
    
    if (connected) {
current_status = MQTTSTATUS_CONNECTED;
        INFO_PRINT("MQTT connected!");
        
        // Subscribe to command topic
        if (strlen(config.mqtt_topic_subscribe) > 0) {
            mqtt_client.subscribe(config.mqtt_topic_subscribe);
            DEBUG_PRINT("Subscribed to: %s", config.mqtt_topic_subscribe);
        }
        
        return true;
    } else {
        current_status = MQTTSTATUS_ERROR;
        ERROR_PRINT("MQTT connection failed, rc=%d", mqtt_client.state());
        return false;
    }
}

void MQTTHandler::disconnect() {
    mqtt_client.disconnect();
    current_status = MQTTSTATUS_DISCONNECTED;
}

bool MQTTHandler::isConnected() const {
    return current_status == MQTTSTATUS_CONNECTED;
}

bool MQTTHandler::publishVibrationData(const VibrationAnalysis& analysis,
                                        const MQTTPayload& payload) {
    if (!isConnected()) {
        return false;
    }
    
    String json_payload;
    if (!buildVibrationJSON(json_payload, analysis, payload)) {
        ERROR_PRINT("Failed to build vibration JSON");
        return false;
    }
    
    const char* topic_main = (strlen(config.mqtt_topic_publish) > 0) ? config.mqtt_topic_publish : "viot/vibration";
    const char* topic_fft_x = (strlen(config.mqtt_topic_fft_x) > 0) ? config.mqtt_topic_fft_x : "viot/vibration/fft/x";
    const char* topic_fft_y = (strlen(config.mqtt_topic_fft_y) > 0) ? config.mqtt_topic_fft_y : "viot/vibration/fft/y";
    const char* topic_fft_z = (strlen(config.mqtt_topic_fft_z) > 0) ? config.mqtt_topic_fft_z : "viot/vibration/fft/z";

    bool published = mqtt_client.publish(
        topic_main,
        (uint8_t*)json_payload.c_str(),
        json_payload.length(),
        false  // not retained
    );
    
    if (!published) {
        ERROR_PRINT("MQTT publish failed");
        return false;
    }

    // Publish FFT split payloads to reduce single-message size.
    String fft_x_json, fft_y_json, fft_z_json;
    if (!buildFFTAxisJSON(fft_x_json, payload, 'x') ||
        !buildFFTAxisJSON(fft_y_json, payload, 'y') ||
        !buildFFTAxisJSON(fft_z_json, payload, 'z')) {
        ERROR_PRINT("Failed to build FFT split JSON");
        return false;
    }

    bool ok_x = mqtt_client.publish(topic_fft_x, (uint8_t*)fft_x_json.c_str(), fft_x_json.length(), false);
    bool ok_y = mqtt_client.publish(topic_fft_y, (uint8_t*)fft_y_json.c_str(), fft_y_json.length(), false);
    bool ok_z = mqtt_client.publish(topic_fft_z, (uint8_t*)fft_z_json.c_str(), fft_z_json.length(), false);

    if (!(ok_x && ok_y && ok_z)) {
        ERROR_PRINT("MQTT FFT split publish failed (x=%d y=%d z=%d)", ok_x ? 1 : 0, ok_y ? 1 : 0, ok_z ? 1 : 0);
        return false;
    }

    last_payload_json = json_payload;
    DEBUG_PRINT("MQTT main+FFT payloads published (main=%uB, x=%uB, y=%uB, z=%uB)",
                json_payload.length(), fft_x_json.length(), fft_y_json.length(), fft_z_json.length());
    last_publish_time = millis();

    // Blink MQTT status LED briefly without tying up the CPU for long.
    digitalWrite(GPIO_MQTT_STATUS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
    digitalWrite(GPIO_MQTT_STATUS, LOW);

    return true;
}

bool MQTTHandler::publishSystemStatus(const SystemStatus& status) {
    if (!isConnected()) {
        return false;
    }
    
    String json_payload;
    if (!buildStatusJSON(json_payload, status)) {
        return false;
    }
    
    String status_topic = String(config.mqtt_topic_publish) + "/status";
    
    return mqtt_client.publish(
        status_topic.c_str(),
        (uint8_t*)json_payload.c_str(),
        json_payload.length(),
        false
    );
}

bool MQTTHandler::subscribe(const String& topic) {
    return mqtt_client.subscribe(topic.c_str());
}

void MQTTHandler::loop() {
    if (!mqtt_client.connected()) {
        current_status = MQTTSTATUS_DISCONNECTED;
    } else {
        mqtt_client.loop();
    }
}

MQTTStatus MQTTHandler::getStatus() const {
    return current_status;
}

void MQTTHandler::setPublishInterval(uint16_t seconds) {
    if (seconds >= 30 && seconds <= 3600) {
        publish_interval_s = seconds;
        DEBUG_PRINT("MQTT publish interval set to %u seconds", seconds);
    }
}

String MQTTHandler::getLastPayload() const {
    return last_payload_json;
}

void MQTTHandler::mqtt_callback(char* topic, byte* payload, unsigned int length) {
    DEBUG_PRINT("MQTT message received on topic: %s", topic);
    DEBUG_PRINT("Payload length: %u bytes", length);
    
    // Handle incoming commands
    // TODO: Parse and execute commands
}

bool MQTTHandler::buildVibrationJSON(String& json, const VibrationAnalysis& analysis,
                                      const MQTTPayload& payload) {
    DynamicJsonDocument doc(2048);
    
    // Top-level timestamp
    doc["timestamp"] = analysis.timestamp_us / 1000;

    // Flatten fields into "data" object
    JsonObject data = doc.createNestedObject("data");
    data["accel_x_rms"] = analysis.rms_accel_x;
    data["accel_y_rms"] = analysis.rms_accel_y;
    data["accel_z_rms"] = analysis.rms_accel_z;

    data["vibration_x_rms_mm_s"] = analysis.rms_velocity_x;
    data["vibration_y_rms_mm_s"] = analysis.rms_velocity_y;
    data["vibration_z_rms_mm_s"] = analysis.rms_velocity_z;
    data["vibration_freq_x_hz"] = analysis.vibration_freq_x;
    data["vibration_freq_y_hz"] = analysis.vibration_freq_y;
    data["vibration_freq_z_hz"] = analysis.vibration_freq_z;

    data["fft_peak_freq_x_hz"] = analysis.fft_peak_freq_x;
    data["fft_peak_freq_y_hz"] = analysis.fft_peak_freq_y;
    data["fft_peak_freq_z_hz"] = analysis.fft_peak_freq_z;
    data["fft_power_x_db"] = analysis.fft_power_x;
    data["fft_power_y_db"] = analysis.fft_power_y;
    data["fft_power_z_db"] = analysis.fft_power_z;


    data["pitch_deg"] = analysis.pitch;
    data["roll_deg"] = analysis.roll;

    data["battery_v"] = payload.battery_voltage;
    data["wifi_rssi"] = payload.wifi_rssi;
    data["uptime_ms"] = payload.uptime_ms;

    // Serialize to string
    serializeJson(doc, json);
    return true;
}

bool MQTTHandler::buildFFTAxisJSON(String& json, const MQTTPayload& payload, char axis) {
    DynamicJsonDocument doc(4096);
    doc["timestamp"] = payload.timestamp_us / 1000;
    doc["axis"] = String(axis);

    JsonObject data = doc.createNestedObject("data");
    const char axis_key = (axis == 'y' || axis == 'Y') ? 'y' : ((axis == 'z' || axis == 'Z') ? 'z' : 'x');
    String freq_key = String(axis_key) + "_freq_hz";
    String amp_key = String(axis_key) + "_amplitude_mm_s";
    JsonArray freq_hz = data.createNestedArray(freq_key);
    JsonArray amplitude_mm_s = data.createNestedArray(amp_key);

    const float* selected = payload.raw_accel_x;
    if (axis == 'y' || axis == 'Y') {
        selected = payload.raw_accel_y;
    } else if (axis == 'z' || axis == 'Z') {
        selected = payload.raw_accel_z;
    }

    for (int i = 0; i < (int)payload.raw_sample_count && i < 100; ++i) {
        freq_hz.add(payload.raw_freq_hz[i]);
        amplitude_mm_s.add(selected[i]);
    }

    serializeJson(doc, json);
    return true;
}

bool MQTTHandler::buildStatusJSON(String& json, const SystemStatus& status) {
    DynamicJsonDocument doc(1024);
    
    const char* wakeup_str = "UNKNOWN";
    switch (status.wakeup_reason) {
        case WAKE_TIMER: wakeup_str = "TIMER"; break;
        case WAKE_EXT_INT: wakeup_str = "EXT_INT"; break;
        default: break;
    }
    
    doc["wakeup_reason"] = wakeup_str;
    doc["wifi_rssi"] = status.wifi_rssi;
    doc["battery_v"] = status.battery_voltage;
    doc["uptime_sec"] = status.uptime_seconds;
    doc["data_sent_count"] = status.data_sent_count;
    
    serializeJson(doc, json);
    return true;
}

/* ========== MQTT TASK ========== */

void mqtt_task(void* parameter) {
    MQTTHandler* mqtt = (MQTTHandler*)parameter;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (1) {
        // Maintain MQTT connection
        if (!g_wifi_handler.isConnected()) {
            if (mqtt->isConnected()) {
                mqtt->disconnect();
            }
        } else if (!mqtt->isConnected()) {
            mqtt->connect();
        } else {
            mqtt->loop();
        }

        UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
        if (watermark < STACK_LOW_WATERMARK_WORDS) {
            ERROR_PRINT("MQTT task stack low: %u words free", static_cast<unsigned>(watermark));
        }

        // Update every 5 seconds
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}
