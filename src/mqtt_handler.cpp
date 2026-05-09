#include "mqtt_handler.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "wifi_handler.h"

extern WiFiHandler g_wifi_handler;

namespace {
MQTTHandler* g_active_mqtt_handler = nullptr;

void appendJsonFloat3(String& out, float value) {
    char buf[20];
    if (!isfinite(value)) {
        out += "0.000";
        return;
    }

    snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(value));
    out += buf;
}
}

MQTTHandler::MQTTHandler() 
    : wifi_client(nullptr),
      current_status(MQTTSTATUS_DISCONNECTED),
      last_publish_time(0),
      pause_until_ms(0),
      publish_interval_s(MQTT_PUBLISH_INTERVAL_S),
      tls_connect_in_progress(false),
      publish_summary{} {
}

MQTTHandler::~MQTTHandler() {
    disconnect();
}

bool MQTTHandler::begin(WiFiClient* client, const SystemConfig& config) {
    wifi_client = client;
    this->config = config;
    g_active_mqtt_handler = this;
    publish_interval_s = config.mqtt_publish_interval_s;
    publish_summary.publish_interval_s = publish_interval_s;
    publish_summary.next_publish_due_ms = millis() + (static_cast<uint32_t>(publish_interval_s) * 1000UL);

    if (config.mqtt_use_tls) {
        // Allow MQTTS without requiring a CA bundle upload yet.
        secure_client.setInsecure();
        mqtt_client.setClient(secure_client);
    } else {
        mqtt_client.setClient(*client);
    }

    mqtt_client.setServer(config.mqtt_broker, config.mqtt_port);
    mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
    mqtt_client.setCallback(mqtt_callback);
    
    INFO_PRINT("MQTT Handler initialized for %s:%u using %s",
               config.mqtt_broker,
               config.mqtt_port,
               config.mqtt_use_tls ? "MQTTS" : "MQTT");
    
    return true;
}

bool MQTTHandler::reconfigure(WiFiClient* client, const SystemConfig& new_config) {
    INFO_PRINT("MQTT reconfiguring to %s:%u using %s",
               new_config.mqtt_broker,
               new_config.mqtt_port,
               new_config.mqtt_use_tls ? "MQTTS" : "MQTT");

    disconnect();
    pause_until_ms = 0;
    publish_summary = {};
    last_publish_time = 0;

    return begin(client, new_config);
}

bool MQTTHandler::connect() {
    if (isPaused()) {
        current_status = MQTTSTATUS_DISCONNECTED;
        return false;
    }

    if (mqtt_client.connected()) {
        current_status = MQTTSTATUS_CONNECTED;
        return true;
    }
    
    if (strlen(config.mqtt_broker) == 0) {
        ERROR_PRINT("MQTT broker not configured");
        return false;
    }
    
    INFO_PRINT("Connecting to %s broker %s:%u...",
               config.mqtt_use_tls ? "MQTTS" : "MQTT",
               config.mqtt_broker,
               config.mqtt_port);
    tls_connect_in_progress = config.mqtt_use_tls;
    publish_summary.tls_connect_in_progress = tls_connect_in_progress;
    publish_summary.last_attempt_ms = millis();
    log_heap_state("MQTT_CONNECT_START");

    if (config.mqtt_use_tls) {
        INFO_PRINT("MQTTS connect mode: quiescing MEMS/FFT for %u ms before TLS handshake", MQTTS_CONNECT_QUIESCE_MS);
        vTaskDelay(pdMS_TO_TICKS(MQTTS_CONNECT_QUIESCE_MS));
        log_heap_state("MQTT_TLS_QUIESCED");
    }
    
    current_status = MQTTSTATUS_CONNECTING;
    
    // Get client ID if not set
    char client_id[64];
    strlcpy(client_id, config.mqtt_client_id, sizeof(client_id));
    if (client_id[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(client_id, sizeof(client_id), "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);
    }
    
    bool connected = false;
    
    if (strlen(config.mqtt_username) > 0) {
        connected = mqtt_client.connect(
            client_id,
            config.mqtt_username,
            config.mqtt_password
        );
    } else {
        connected = mqtt_client.connect(client_id);
    }
    
    if (connected) {
        current_status = MQTTSTATUS_CONNECTED;
        tls_connect_in_progress = false;
        publish_summary.tls_connect_in_progress = false;
        log_heap_state("MQTT_CONNECTED");
        
        // Subscribe to command topic
        if (strlen(config.mqtt_topic_subscribe) > 0) {
            mqtt_client.subscribe(config.mqtt_topic_subscribe);
            DEBUG_PRINT("Subscribed to: %s", config.mqtt_topic_subscribe);
        }
        
        return true;
    } else {
        current_status = MQTTSTATUS_ERROR;
        tls_connect_in_progress = false;
        publish_summary.tls_connect_in_progress = false;
        log_heap_state("MQTT_CONNECT_FAIL");
        ERROR_PRINT("MQTT connection failed, rc=%d", mqtt_client.state());
        return false;
    }
}

void MQTTHandler::disconnect() {
    mqtt_client.disconnect();
    if (wifi_client) {
        wifi_client->stop();
    }
    secure_client.stop();
    tls_connect_in_progress = false;
    publish_summary.tls_connect_in_progress = false;
    current_status = MQTTSTATUS_DISCONNECTED;
}

bool MQTTHandler::isConnected() const {
    return current_status == MQTTSTATUS_CONNECTED;
}

void MQTTHandler::pauseForWebAccess(uint32_t duration_ms) {
    const uint32_t until = millis() + duration_ms;
    if ((pause_until_ms - until) > 0x7FFFFFFFul) {
        pause_until_ms = until;
    } else if (until > pause_until_ms) {
        pause_until_ms = until;
    }
}

bool MQTTHandler::isPaused() const {
    return static_cast<int32_t>(pause_until_ms - millis()) > 0;
}

bool MQTTHandler::isTlsConnectInProgress() const {
    return tls_connect_in_progress;
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
        updatePublishSummary(false,
                             static_cast<uint16_t>(json_payload.length()),
                             0,
                             0,
                             0);
        return false;
    }

    // Publish FFT split payloads to reduce single-message size.
    String fft_x_json, fft_y_json, fft_z_json;
    if (!buildFFTAxisJSON(fft_x_json, payload, 'x') ||
        !buildFFTAxisJSON(fft_y_json, payload, 'y') ||
        !buildFFTAxisJSON(fft_z_json, payload, 'z')) {
        ERROR_PRINT("Failed to build FFT split JSON");
        updatePublishSummary(false,
                             static_cast<uint16_t>(json_payload.length()),
                             0,
                             0,
                             0);
        return false;
    }

    bool ok_x = mqtt_client.publish(topic_fft_x, (uint8_t*)fft_x_json.c_str(), fft_x_json.length(), false);
    bool ok_y = mqtt_client.publish(topic_fft_y, (uint8_t*)fft_y_json.c_str(), fft_y_json.length(), false);
    bool ok_z = mqtt_client.publish(topic_fft_z, (uint8_t*)fft_z_json.c_str(), fft_z_json.length(), false);

    if (!(ok_x && ok_y && ok_z)) {
        ERROR_PRINT("MQTT FFT split publish failed (x=%d y=%d z=%d)", ok_x ? 1 : 0, ok_y ? 1 : 0, ok_z ? 1 : 0);
        updatePublishSummary(false,
                             static_cast<uint16_t>(json_payload.length()),
                             static_cast<uint16_t>(fft_x_json.length()),
                             static_cast<uint16_t>(fft_y_json.length()),
                             static_cast<uint16_t>(fft_z_json.length()));
        return false;
    }

    updatePublishSummary(true,
                         static_cast<uint16_t>(json_payload.length()),
                         static_cast<uint16_t>(fft_x_json.length()),
                         static_cast<uint16_t>(fft_y_json.length()),
                         static_cast<uint16_t>(fft_z_json.length()));
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
    
    char status_topic[sizeof(config.mqtt_topic_publish) + 8];
    snprintf(status_topic, sizeof(status_topic), "%s/status", config.mqtt_topic_publish);
    
    return mqtt_client.publish(
        status_topic,
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
        publish_summary.publish_interval_s = seconds;
        DEBUG_PRINT("MQTT publish interval set to %u seconds", seconds);
    }
}

void MQTTHandler::setNextPublishDueMs(uint32_t due_ms) {
    publish_summary.next_publish_due_ms = due_ms;
    publish_summary.publish_interval_s = publish_interval_s;
}

MQTTPublishSummary MQTTHandler::getPublishSummary() const {
    MQTTPublishSummary summary = publish_summary;
    summary.tls_connect_in_progress = tls_connect_in_progress;
    summary.publish_interval_s = publish_interval_s;

    if (!isConnected() || summary.next_publish_due_ms == 0) {
        summary.seconds_until_next_publish = -1;
    } else {
        const int32_t ms_left = static_cast<int32_t>(summary.next_publish_due_ms - millis());
        summary.seconds_until_next_publish = (ms_left <= 0) ? 0 : ((ms_left + 999) / 1000);
    }

    return summary;
}

void MQTTHandler::mqtt_callback(char* topic, byte* payload, unsigned int length) {
    DEBUG_PRINT("MQTT message received on topic: %s", topic);
    DEBUG_PRINT("Payload length: %u bytes", length);
    if (g_active_mqtt_handler) {
        g_active_mqtt_handler->recordSubscribeMessage(length);
    }
    
    // Handle incoming commands
    // TODO: Parse and execute commands
}

bool MQTTHandler::buildVibrationJSON(String& json, const VibrationAnalysis& analysis,
                                      const MQTTPayload& payload) {
    StaticJsonDocument<2048> doc;
    
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

    data["displacement_x_um"] = analysis.displacement_x_um;
    data["displacement_y_um"] = analysis.displacement_y_um;
    data["displacement_z_um"] = analysis.displacement_z_um;

    data["pitch_deg"] = analysis.pitch;
    data["roll_deg"] = analysis.roll;
    data["yaw_deg"] = analysis.yaw;

    data["battery_v"] = payload.battery_voltage;
    data["wifi_rssi"] = payload.wifi_rssi;
    data["uptime_ms"] = payload.uptime_ms;

    // Serialize to string
    serializeJson(doc, json);
    return true;
}

bool MQTTHandler::buildFFTAxisJSON(String& json, const MQTTPayload& payload, char axis) {
    const char axis_key = (axis == 'y' || axis == 'Y') ? 'y' : ((axis == 'z' || axis == 'Z') ? 'z' : 'x');
    char freq_key[24];
    char amp_key[32];
    snprintf(freq_key, sizeof(freq_key), "%c_freq_hz", axis_key);
    snprintf(amp_key, sizeof(amp_key), "%c_amplitude_mm_s", axis_key);

    const float* selected = payload.raw_accel_x;
    if (axis == 'y' || axis == 'Y') {
        selected = payload.raw_accel_y;
    } else if (axis == 'z' || axis == 'Z') {
        selected = payload.raw_accel_z;
    }

    const uint32_t points = (payload.raw_sample_count < MQTT_FFT_POINTS) ? payload.raw_sample_count : MQTT_FFT_POINTS;
    json = "";
    json.reserve(220 + (points * 16));

    json += "{\"timestamp\":";
    json += static_cast<uint32_t>(payload.timestamp_us / 1000);
    json += ",\"axis\":\"";
    json += axis_key;
    json += "\",\"data\":{\"";
    json += freq_key;
    json += "\":[";

    for (uint32_t i = 0; i < points; ++i) {
        if (i > 0) {
            json += ",";
        }
        appendJsonFloat3(json, payload.raw_freq_hz[i]);
    }

    json += "],\"";
    json += amp_key;
    json += "\":[";

    for (uint32_t i = 0; i < points; ++i) {
        if (i > 0) {
            json += ",";
        }
        appendJsonFloat3(json, selected[i]);
    }

    json += "]}}";
    return true;
}

bool MQTTHandler::buildStatusJSON(String& json, const SystemStatus& status) {
    StaticJsonDocument<1024> doc;
    
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

void MQTTHandler::updatePublishSummary(bool success,
                                       uint16_t main_size,
                                       uint16_t fft_x_size,
                                       uint16_t fft_y_size,
                                       uint16_t fft_z_size) {
    publish_summary.has_publish = true;
    publish_summary.success = success;
    publish_summary.last_attempt_ms = millis();
    publish_summary.main_size = main_size;
    publish_summary.fft_x_size = fft_x_size;
    publish_summary.fft_y_size = fft_y_size;
    publish_summary.fft_z_size = fft_z_size;
    publish_summary.tls_connect_in_progress = tls_connect_in_progress;

    if (success) {
        publish_summary.last_success_ms = publish_summary.last_attempt_ms;
        publish_summary.publish_count++;
    }
}

void MQTTHandler::recordSubscribeMessage(unsigned int length) {
    publish_summary.subscribe_receive_count++;
    publish_summary.last_subscribe_ms = millis();
    publish_summary.last_subscribe_size = static_cast<uint16_t>(length > UINT16_MAX ? UINT16_MAX : length);
}

/* ========== MQTT TASK ========== */

void mqtt_task(void* parameter) {
    MQTTHandler* mqtt = (MQTTHandler*)parameter;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (1) {
        if (mqtt->isPaused()) {
            if (mqtt->isConnected()) {
                INFO_PRINT("MQTT paused for STA web access, disconnecting temporarily");
                mqtt->disconnect();
            }
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000));
            continue;
        }

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
