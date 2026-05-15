#include "mqtt_handler.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "wifi_handler.h"
#include "sensors.h"

extern WiFiHandler g_wifi_handler;
extern MEMSSensor g_mems_sensor;

namespace {
MQTTHandler* g_active_mqtt_handler = nullptr;
constexpr uint8_t kDefaultFFTStepHz = 10;
constexpr uint8_t kAllowedFFTStepsHz[] = {10, 15, 20, 25, 30, 35, 40, 45, 50};
constexpr float kCommandFFTStartHz = 10.0f;
constexpr float kCommandFFTMaxHz = 1200.0f;

bool isAllowedFFTStepHz(uint8_t value) {
    for (uint8_t allowed : kAllowedFFTStepsHz) {
        if (allowed == value) {
            return true;
        }
    }
    return false;
}

uint8_t normalizeFFTStepHz(uint8_t value) {
    return isAllowedFFTStepHz(value) ? value : kDefaultFFTStepHz;
}

void appendJsonFloat2(String& out, float value) {
    char buf[20];
    if (!isfinite(value)) {
        out += "0.00";
        return;
    }

    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
    out += buf;
}

void appendJsonAmplitude2(String& out, float value) {
    char buf[20];
    if (!isfinite(value)) {
        out += "0.00";
        return;
    }

    float clamped = value;
    if (clamped > 99.99f) {
        clamped = 99.99f;
    } else if (clamped < 0.0f) {
        clamped = 0.0f;
    }

    snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(clamped));
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
      publish_summary{},
      pending_fft_axis('\0'),
      pending_fft_rounds(0),
      pending_fft_ready(false),
      clear_retained_command_after_publish(false),
      pending_fft_timestamp_us(0),
      pending_fft_step_hz(kDefaultFFTStepHz),
      pending_processing_ack(false),
      command_publish_settle_until_ms(0),
      connect_failure_count(0),
      mqtt_mutex(xSemaphoreCreateRecursiveMutex()) {
    pending_request_id[0] = '\0';
    last_completed_request_id[0] = '\0';
    last_completed_axis = '\0';
    pending_fft_points = 0;
    pending_fft_snapshot_valid = false;
}

MQTTHandler::~MQTTHandler() {
    disconnect();
    if (mqtt_mutex != nullptr) {
        vSemaphoreDelete(mqtt_mutex);
        mqtt_mutex = nullptr;
    }
}

bool MQTTHandler::begin(WiFiClient* client, const SystemConfig& config) {
    wifi_client = client;
    this->config = config;
    g_active_mqtt_handler = this;
    clearPendingFFTRequest();
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
    if (!lockClient(pdMS_TO_TICKS(2000))) {
        return false;
    }

    if (isPaused()) {
        current_status = MQTTSTATUS_DISCONNECTED;
        unlockClient();
        return false;
    }

    if (mqtt_client.connected()) {
        current_status = MQTTSTATUS_CONNECTED;
        connect_failure_count = 0;
        unlockClient();
        return true;
    }
    
    if (strlen(config.mqtt_broker) == 0) {
        ERROR_PRINT("MQTT broker not configured");
        unlockClient();
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
        connect_failure_count = 0;
        tls_connect_in_progress = false;
        publish_summary.tls_connect_in_progress = false;
        log_heap_state("MQTT_CONNECTED");
        
        // Subscribe to command topic
        if (strlen(config.mqtt_topic_subscribe) > 0) {
            mqtt_client.subscribe(config.mqtt_topic_subscribe);
            DEBUG_MQTT_PRINT("Subscribed to: %s", config.mqtt_topic_subscribe);
        }
        unlockClient();
        return true;
    } else {
        current_status = MQTTSTATUS_ERROR;
        if (connect_failure_count < UINT8_MAX) {
            connect_failure_count++;
        }
        tls_connect_in_progress = false;
        publish_summary.tls_connect_in_progress = false;
        log_heap_state("MQTT_CONNECT_FAIL");
        ERROR_PRINT("MQTT connection failed, rc=%d (attempt %u/%u)",
                    mqtt_client.state(),
                    static_cast<unsigned>(connect_failure_count),
                    static_cast<unsigned>(MQTT_CONNECT_MAX_RETRIES));
        unlockClient();
        return false;
    }
}

void MQTTHandler::disconnect() {
    if (!lockClient(pdMS_TO_TICKS(2000))) {
        return;
    }
    mqtt_client.disconnect();
    if (wifi_client) {
        wifi_client->stop();
    }
    secure_client.stop();
    tls_connect_in_progress = false;
    publish_summary.tls_connect_in_progress = false;
    current_status = MQTTSTATUS_DISCONNECTED;
    clearPendingFFTRequest();
    unlockClient();
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

bool MQTTHandler::hasReachedConnectFailureLimit() const {
    return connect_failure_count >= MQTT_CONNECT_MAX_RETRIES;
}

uint8_t MQTTHandler::getConnectFailureCount() const {
    return connect_failure_count;
}

void MQTTHandler::resetConnectFailureCount() {
    connect_failure_count = 0;
}

bool MQTTHandler::publishVibrationData(const VibrationAnalysis& analysis,
                                        const MQTTPayload& payload) {
    if (!isConnected()) {
        return false;
    }
    if (!lockClient()) {
        return false;
    }
    
    String json_payload;
    if (!buildVibrationJSON(json_payload, analysis, payload)) {
        ERROR_PRINT("Failed to build vibration JSON");
        unlockClient();
        return false;
    }
    
    const char* topic_main = (strlen(config.mqtt_topic_publish) > 0) ? config.mqtt_topic_publish : "viot/vibration";
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
        unlockClient();
        return false;
    }

    updatePublishSummary(true,
                         static_cast<uint16_t>(json_payload.length()),
                         0,
                         0,
                         0);
    DEBUG_MQTT_PRINT("MQTT main payload published (main=%uB)", json_payload.length());
    last_publish_time = millis();

    // Blink MQTT status LED briefly without tying up the CPU for long.
    digitalWrite(GPIO_MQTT_STATUS, HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));
    digitalWrite(GPIO_MQTT_STATUS, LOW);

    unlockClient();
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
    if (!lockClient()) {
        return false;
    }
    const bool ok = mqtt_client.subscribe(topic.c_str());
    unlockClient();
    return ok;
}

void MQTTHandler::loop() {
    if (!mqtt_client.connected()) {
        current_status = MQTTSTATUS_DISCONNECTED;
    } else {
        if (!lockClient()) {
            return;
        }
        mqtt_client.loop();
        unlockClient();
    }
}

MQTTStatus MQTTHandler::getStatus() const {
    return current_status;
}

void MQTTHandler::setPublishInterval(uint16_t seconds) {
    if (seconds >= 1 && seconds <= 3600) {
        publish_interval_s = seconds;
        publish_summary.publish_interval_s = seconds;
        DEBUG_MQTT_PRINT("MQTT publish interval set to %u seconds", seconds);
    }
}

bool MQTTHandler::shouldComputeFFT() const {
    return pending_fft_axis != '\0';
}

bool MQTTHandler::hasPendingFFTWork() const {
    return pending_fft_axis != '\0';
}

void MQTTHandler::noteFFTComputationRound(uint64_t timestamp_us) {
    if (pending_fft_axis == '\0' || pending_fft_ready) {
        return;
    }

    if (pending_fft_rounds < 3) {
        pending_fft_rounds++;
    }
    pending_fft_timestamp_us = timestamp_us;

    if (pending_fft_rounds >= 3) {
        if (capturePendingFFTSnapshot()) {
            pending_fft_ready = true;
            pending_processing_ack = true;
            INFO_PRINT("FFT request for axis %c collected 3 samples and snapshot is ready to publish", pending_fft_axis);
        } else {
            ERROR_PRINT("Failed to capture FFT snapshot for axis %c", pending_fft_axis);
        }
    }
}

bool MQTTHandler::publishPendingFFTIfReady() {
    if (!isConnected()) {
        return false;
    }

    if (pending_processing_ack) {
        pending_processing_ack = false;
        publishCommandAck("processing", pending_fft_axis, "3_rounds_collected");
    }

    if (!pending_fft_ready || pending_fft_axis == '\0') {
        return false;
    }
    if (!lockClient()) {
        return false;
    }

    String fft_json;
    if (!buildFFTAxisJSONFromSnapshot(fft_json, pending_fft_axis, pending_fft_timestamp_us)) {
        ERROR_PRINT("Failed to build queued FFT JSON for axis %c", pending_fft_axis);
        unlockClient();
        publishCommandAck("error", pending_fft_axis, "build_result_failed");
        return false;
    }
    DEBUG_MQTT_PRINT("Queued FFT axis %c payload size=%u bytes step=%uHz points=%u",
                pending_fft_axis,
                static_cast<unsigned>(fft_json.length()),
                static_cast<unsigned>(pending_fft_step_hz),
                static_cast<unsigned>(pending_fft_points));

    char topic_result[320];
    buildCommandResultTopic(topic_result, sizeof(topic_result));

    const char* topic_fft_axis = config.mqtt_topic_fft_x;
    if (pending_fft_axis == 'y') {
        topic_fft_axis = config.mqtt_topic_fft_y;
    } else if (pending_fft_axis == 'z') {
        topic_fft_axis = config.mqtt_topic_fft_z;
    }

    const bool published_result = mqtt_client.publish(topic_result, (uint8_t*)fft_json.c_str(), fft_json.length(), false);
    const bool published_axis = mqtt_client.publish(topic_fft_axis, (uint8_t*)fft_json.c_str(), fft_json.length(), false);
    if (!(published_result && published_axis)) {
        ERROR_PRINT("Failed to publish queued FFT for axis %c (result=%d axis=%d)", pending_fft_axis, published_result ? 1 : 0, published_axis ? 1 : 0);
        unlockClient();
        publishCommandAck("error", pending_fft_axis, "publish_result_failed");
        return false;
    }

    updatePublishSummary(true,
                         publish_summary.main_size,
                         pending_fft_axis == 'x' ? static_cast<uint16_t>(fft_json.length()) : 0,
                         pending_fft_axis == 'y' ? static_cast<uint16_t>(fft_json.length()) : 0,
                         pending_fft_axis == 'z' ? static_cast<uint16_t>(fft_json.length()) : 0);

    if (clear_retained_command_after_publish && strlen(config.mqtt_topic_subscribe) > 0) {
        if (!mqtt_client.publish(config.mqtt_topic_subscribe, "", true)) {
            ERROR_PRINT("Failed to clear retained command on %s", config.mqtt_topic_subscribe);
        }
    }

    unlockClient();
    publishCommandAck("done", pending_fft_axis, "result_published");
    command_publish_settle_until_ms = 0;
    strlcpy(last_completed_request_id, pending_request_id, sizeof(last_completed_request_id));
    last_completed_axis = pending_fft_axis;
    INFO_PRINT("Queued FFT axis %c published successfully to result+axis topics (%u bytes)", pending_fft_axis, fft_json.length());
    clearPendingFFTRequest();
    return true;
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
    DEBUG_MQTT_PRINT("MQTT message received on topic: %s", topic);
    DEBUG_MQTT_PRINT("Payload length: %u bytes", length);
    if (g_active_mqtt_handler) {
        g_active_mqtt_handler->recordSubscribeMessage(length);
        char axis = '\0';
        const char* payload_text = reinterpret_cast<const char*>(payload);
        char request_id[64] = {};
        uint8_t step_hz = kDefaultFFTStepHz;
        if (g_active_mqtt_handler->parseFFTCommand(topic, payload_text, length, axis, step_hz, request_id, sizeof(request_id))) {
            if (request_id[0] != '\0') {
                if (strcmp(g_active_mqtt_handler->pending_request_id, request_id) == 0 &&
                    g_active_mqtt_handler->pending_fft_axis == axis) {
                    g_active_mqtt_handler->publishCommandAck("duplicate_ignored", axis, "request_in_progress");
                    return;
                }

                if (strcmp(g_active_mqtt_handler->last_completed_request_id, request_id) == 0 &&
                    g_active_mqtt_handler->last_completed_axis == axis) {
                    g_active_mqtt_handler->publishCommandAck("already_done", axis, "request_completed");
                    return;
                }
            }
            strlcpy(g_active_mqtt_handler->pending_request_id, request_id, sizeof(g_active_mqtt_handler->pending_request_id));
            g_active_mqtt_handler->queueFFTRequest(axis, step_hz, true);
        }
    }
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
        appendJsonFloat2(json, payload.raw_freq_hz[i]);
    }

    json += "],\"";
    json += amp_key;
    json += "\":[";

    for (uint32_t i = 0; i < points; ++i) {
        if (i > 0) {
            json += ",";
        }
        appendJsonAmplitude2(json, selected[i]);
    }

    json += "]}}";
    return true;
}

bool MQTTHandler::buildFFTAxisJSONFromSnapshot(String& json, char axis, uint64_t timestamp_us) const {
    if (!pending_fft_snapshot_valid || pending_fft_points == 0) {
        return false;
    }

    const char axis_key = (axis == 'y' || axis == 'Y') ? 'y' : ((axis == 'z' || axis == 'Z') ? 'z' : 'x');
    char freq_key[24];
    char amp_key[32];
    snprintf(freq_key, sizeof(freq_key), "%c_freq_hz", axis_key);
    snprintf(amp_key, sizeof(amp_key), "%c_amplitude_mm_s", axis_key);

    json = "";
    json.reserve(900);
    json += "{\"timestamp\":";
    json += static_cast<uint32_t>(timestamp_us / 1000);
    json += ",\"axis\":\"";
    json += axis_key;
    json += "\"";
    if (pending_request_id[0] != '\0') {
        json += ",\"request_id\":\"";
        json += pending_request_id;
        json += "\"";
    }
    json += ",\"step_hz\":";
    json += pending_fft_step_hz;
    json += ",\"data\":{\"";
    json += freq_key;
    json += "\":[";

    for (uint16_t i = 0; i < pending_fft_points; ++i) {
        if (i > 0) {
            json += ",";
        }
        appendJsonFloat2(json, pending_fft_freq_hz[i]);
    }

    json += "],\"";
    json += amp_key;
    json += "\":[";

    for (uint16_t i = 0; i < pending_fft_points; ++i) {
        if (i > 0) {
            json += ",";
        }
        appendJsonAmplitude2(json, pending_fft_amp_mm_s[i]);
    }

    json += "]}}";
    return true;
}

bool MQTTHandler::buildStatusJSON(String& json, const SystemStatus& status) {
    StaticJsonDocument<1024> doc;
    
    doc["wakeup_reason"] = wakeup_reason_to_string(status.wakeup_reason);
    doc["wifi_rssi"] = status.wifi_rssi;
    doc["battery_v"] = status.battery_voltage;
    doc["uptime_sec"] = status.uptime_seconds;
    doc["data_sent_count"] = status.data_sent_count;
    
    serializeJson(doc, json);
    return true;
}

bool MQTTHandler::publishCommandAck(const char* status, char axis, const char* detail) {
    if (!isConnected()) {
        return false;
    }
    if (!lockClient()) {
        return false;
    }

    char topic_ack[320];
    buildCommandAckTopic(topic_ack, sizeof(topic_ack));

    StaticJsonDocument<256> doc;
    doc["status"] = status ? status : "unknown";
    if (axis == 'x' || axis == 'y' || axis == 'z') {
        char axis_value[2] = { axis, '\0' };
        doc["axis"] = axis_value;
    }
    if (pending_request_id[0] != '\0') {
        doc["request_id"] = pending_request_id;
    }
    if (detail != nullptr && detail[0] != '\0') {
        doc["detail"] = detail;
    }
    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);
    const bool ok = mqtt_client.publish(topic_ack, (uint8_t*)json.c_str(), json.length(), false);
    unlockClient();
    return ok;
}

void MQTTHandler::buildCommandAckTopic(char* out, size_t out_size) const {
    if (strlen(config.mqtt_topic_ack) > 0) {
        strlcpy(out, config.mqtt_topic_ack, out_size);
    } else if (strlen(config.mqtt_topic_subscribe) > 0) {
        snprintf(out, out_size, "%s/ack", config.mqtt_topic_subscribe);
    } else {
        strlcpy(out, "viot/cmd/ack", out_size);
    }
}

void MQTTHandler::buildCommandResultTopic(char* out, size_t out_size) const {
    if (strlen(config.mqtt_topic_result) > 0) {
        strlcpy(out, config.mqtt_topic_result, out_size);
    } else if (strlen(config.mqtt_topic_subscribe) > 0) {
        snprintf(out, out_size, "%s/result", config.mqtt_topic_subscribe);
    } else {
        strlcpy(out, "viot/cmd/result", out_size);
    }
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

void MQTTHandler::queueFFTRequest(char axis, uint8_t step_hz, bool clear_after_publish) {
    if (axis != 'x' && axis != 'y' && axis != 'z') {
        return;
    }

    pending_fft_axis = axis;
    pending_fft_step_hz = normalizeFFTStepHz(step_hz);
    pending_fft_rounds = 0;
    pending_fft_ready = false;
    clear_retained_command_after_publish = clear_after_publish;
    pending_fft_timestamp_us = 0;
    INFO_PRINT("Queued FFT request for axis %c (step=%uHz)", axis, pending_fft_step_hz);
    publishCommandAck("accepted", axis, "queued_for_3_rounds");
}

void MQTTHandler::clearPendingFFTRequest() {
    pending_fft_axis = '\0';
    pending_fft_rounds = 0;
    pending_fft_ready = false;
    clear_retained_command_after_publish = false;
    pending_fft_timestamp_us = 0;
    pending_fft_step_hz = kDefaultFFTStepHz;
    pending_request_id[0] = '\0';
    pending_processing_ack = false;
    pending_fft_points = 0;
    pending_fft_snapshot_valid = false;
}

bool MQTTHandler::parseFFTCommand(const char* topic, const char* payload, unsigned int length, char& out_axis, uint8_t& out_step_hz, char* out_request_id, size_t out_request_id_size) const {
    out_axis = '\0';
    out_step_hz = kDefaultFFTStepHz;
    if (out_request_id != nullptr && out_request_id_size > 0) {
        out_request_id[0] = '\0';
    }
    if (topic == nullptr || payload == nullptr || strlen(config.mqtt_topic_subscribe) == 0) {
        return false;
    }

    if (strcmp(topic, config.mqtt_topic_subscribe) != 0 || length == 0) {
        return false;
    }

    String body;
    body.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) {
        body += static_cast<char>(payload[i]);
    }
    body.trim();
    body.toLowerCase();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
        const char* action = "";
        if (doc["action"].is<const char*>()) {
            action = doc["action"];
        } else if (doc["command"].is<const char*>()) {
            action = doc["command"];
        }
        if (out_request_id != nullptr && out_request_id_size > 0 && doc["request_id"].is<const char*>()) {
            strlcpy(out_request_id, doc["request_id"], out_request_id_size);
        }
        if (doc["step_hz"].is<int>()) {
            out_step_hz = normalizeFFTStepHz(static_cast<uint8_t>(doc["step_hz"].as<int>()));
        } else if (doc["resolution_hz"].is<int>()) {
            out_step_hz = normalizeFFTStepHz(static_cast<uint8_t>(doc["resolution_hz"].as<int>()));
        } else if (doc["step_hz"].is<const char*>()) {
            out_step_hz = normalizeFFTStepHz(static_cast<uint8_t>(atoi(doc["step_hz"].as<const char*>())));
        } else if (doc["resolution_hz"].is<const char*>()) {
            out_step_hz = normalizeFFTStepHz(static_cast<uint8_t>(atoi(doc["resolution_hz"].as<const char*>())));
        }
        String cmd = action;
        cmd.toLowerCase();
        if (cmd == "fft_x") out_axis = 'x';
        if (cmd == "fft_y") out_axis = 'y';
        if (cmd == "fft_z") out_axis = 'z';
    } else {
        if (body == "fft_x") out_axis = 'x';
        if (body == "fft_y") out_axis = 'y';
        if (body == "fft_z") out_axis = 'z';
    }

    return out_axis != '\0';
}

bool MQTTHandler::isCommandPublishSettling() const {
    return static_cast<int32_t>(command_publish_settle_until_ms - millis()) > 0;
}

bool MQTTHandler::lockClient(TickType_t wait_ticks) {
    return mqtt_mutex != nullptr && xSemaphoreTakeRecursive(mqtt_mutex, wait_ticks) == pdTRUE;
}

void MQTTHandler::unlockClient() {
    if (mqtt_mutex != nullptr) {
        xSemaphoreGiveRecursive(mqtt_mutex);
    }
}

bool MQTTHandler::capturePendingFFTSnapshot() {
    if (pending_fft_axis == '\0') {
        return false;
    }

    float source_freq_hz[MEMSSensor::FFT_DISPLAY_POINTS];
    float source_amp_mm_s[MEMSSensor::FFT_DISPLAY_POINTS];
    uint16_t source_points = 0;
    if (!g_mems_sensor.getFFTSpectrum(pending_fft_axis,
                                      source_freq_hz,
                                      source_amp_mm_s,
                                      MEMSSensor::FFT_DISPLAY_POINTS,
                                      source_points)) {
        pending_fft_points = 0;
        pending_fft_snapshot_valid = false;
        return false;
    }

    pending_fft_points = 0;
    const float source_max_hz = (source_points > 0) ? source_freq_hz[source_points - 1] : 0.0f;

    for (uint16_t i = 0; i < MQTT_FFT_POINTS; ++i) {
        const float target_hz = kCommandFFTStartHz + (static_cast<float>(i) * pending_fft_step_hz);
        pending_fft_freq_hz[i] = target_hz;
        pending_fft_amp_mm_s[i] = 0.0f;

        if (source_points == 0 || target_hz > kCommandFFTMaxHz || target_hz > source_max_hz) {
            pending_fft_points++;
            continue;
        }

        uint16_t best_index = 0;
        float best_error = fabsf(source_freq_hz[0] - target_hz);
        for (uint16_t j = 1; j < source_points; ++j) {
            const float error = fabsf(source_freq_hz[j] - target_hz);
            if (error < best_error) {
                best_error = error;
                best_index = j;
            }
        }

        pending_fft_amp_mm_s[i] = source_amp_mm_s[best_index];
        pending_fft_points++;
    }

    pending_fft_snapshot_valid = (pending_fft_points > 0);
    return pending_fft_snapshot_valid;
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
            mqtt->resetConnectFailureCount();
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
