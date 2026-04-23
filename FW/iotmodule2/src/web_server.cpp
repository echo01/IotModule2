#include "web_server.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <cstring>
#include <math.h>
#include <esp_wifi.h>
#include "storage.h"
#include "wifi_handler.h"
#include "sensors.h"
#include "mqtt_handler.h"

extern Storage g_storage;
extern WiFiHandler g_wifi_handler;
extern MEMSSensor g_mems_sensor;
extern MQTTHandler g_mqtt_handler;
extern SystemConfig g_system_config;
extern SystemStatus g_system_status;

namespace {
const char* mqttStatusToString(MQTTStatus status) {
    switch (status) {
        case MQTTSTATUS_DISCONNECTED: return "DISCONNECTED";
        case MQTTSTATUS_CONNECTING: return "CONNECTING";
        case MQTTSTATUS_CONNECTED: return "CONNECTED";
        case MQTTSTATUS_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

String maskSecret(const char* value) {
    if (value == nullptr) {
        return "";
    }
    size_t len = strlen(value);
    if (len == 0) {
        return "";
    }
    if (len <= 2) {
        return String("**");
    }
    String masked;
    masked.reserve(len);
    masked += value[0];
    for (size_t i = 0; i < (len - 2); ++i) {
        masked += '*';
    }
    masked += value[len - 1];
    return masked;
}

void logConfigSummary(const char* context) {
    INFO_PRINT("[%s] Network config: STA_SSID='%s' AP_ENABLED=%s AP_SSID='%s' AP_PASS='%s'",
               context,
               g_system_config.wifi_ssid,
               g_system_config.wifi_ap_enabled ? "true" : "false",
               g_system_config.ap_ssid,
               maskSecret(g_system_config.ap_password).c_str());
    INFO_PRINT("[%s] STA IP mode: %s IP='%s' GW='%s' MASK='%s' DNS1='%s' DNS2='%s'",
               context,
               g_system_config.sta_use_static_ip ? "STATIC" : "DHCP",
               g_system_config.sta_static_ip,
               g_system_config.sta_gateway,
               g_system_config.sta_subnet,
               g_system_config.sta_dns1,
               g_system_config.sta_dns2);
    INFO_PRINT("[%s] MQTT config: BROKER='%s' PORT=%u CLIENT_ID='%s' USER='%s' PASS='%s' MAIN='%s' FFT_X='%s' FFT_Y='%s' FFT_Z='%s' SUB='%s'",
               context,
               g_system_config.mqtt_broker,
               g_system_config.mqtt_port,
               g_system_config.mqtt_client_id,
               g_system_config.mqtt_username,
               maskSecret(g_system_config.mqtt_password).c_str(),
               g_system_config.mqtt_topic_publish,
               g_system_config.mqtt_topic_fft_x,
               g_system_config.mqtt_topic_fft_y,
               g_system_config.mqtt_topic_fft_z,
               g_system_config.mqtt_topic_subscribe);
}

void sendSPIFFSFileWithLog(AsyncWebServerRequest* request, const char* spiffs_path, const char* content_type) {
    INFO_PRINT("SPIFFS open requested: URL='%s' FILE='%s'",
               request->url().c_str(),
               spiffs_path);
    if (SPIFFS.exists(spiffs_path)) {
        request->send(SPIFFS, spiffs_path, content_type);
        return;
    }

    ERROR_PRINT("SPIFFS file not found: URL='%s' FILE='%s'",
                request->url().c_str(),
                spiffs_path);
    request->send(404, "text/plain", "Not Found");
}

String htmlShell(const String& title, const String& body) {
    String html;
    html.reserve(4096);
    html += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>");
    html += title;
    html += F("</title><style>"
              "body{font-family:Segoe UI,Arial,sans-serif;background:#eef3f8;color:#14213d;margin:0;padding:24px;}"
              ".wrap{max-width:720px;margin:0 auto;background:#fff;border-radius:16px;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,.08);}"
              "h1{margin:0 0 12px;font-size:28px;}p{line-height:1.5;color:#445;}label{display:block;font-weight:600;margin:16px 0 6px;}"
              "input,select{width:100%;padding:12px;border:1px solid #c8d3df;border-radius:10px;font-size:16px;box-sizing:border-box;}"
              "button,.btn{display:inline-block;margin-top:18px;padding:12px 16px;background:#0f766e;color:#fff;border:0;border-radius:10px;font-size:15px;text-decoration:none;cursor:pointer;}"
              ".danger{background:#b91c1c}.muted{background:#64748b}.nav a{margin-right:12px;color:#0f766e;text-decoration:none;font-weight:600}.hint{font-size:13px;color:#667}.row{display:grid;grid-template-columns:1fr 1fr;gap:16px}"
              "@media(max-width:680px){.row{grid-template-columns:1fr}}" 
              "</style></head><body><div class='wrap'>");
    html += body;
    html += F("</div></body></html>");
    return html;
}
}

WebServer::WebServer()
    : server(WEB_SERVER_PORT),
      ws("/ws"),
      is_running(false) {
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::begin(uint16_t port) {
    INFO_PRINT("Web Server initializing on port %u...", port);

    ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });

    server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) { handleRoot(request); });
    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* request) { sendSPIFFSFileWithLog(request, "/www/index.html", "text/html"); });
    server.on("/mqtt_setting.html", HTTP_GET, [](AsyncWebServerRequest* request) { sendSPIFFSFileWithLog(request, "/www/mqtt_setting.html", "text/html"); });
    server.on("/network_setting.html", HTTP_GET, [](AsyncWebServerRequest* request) { sendSPIFFSFileWithLog(request, "/www/network_setting.html", "text/html"); });
    server.on("/mems_setting.html", HTTP_GET, [](AsyncWebServerRequest* request) { sendSPIFFSFileWithLog(request, "/www/mems_setting.html", "text/html"); });
    server.on("/system_setting.html", HTTP_GET, [](AsyncWebServerRequest* request) { sendSPIFFSFileWithLog(request, "/www/system_setting.html", "text/html"); });
    server.on("/wifi_config", HTTP_GET, [this](AsyncWebServerRequest* request) { handleWiFiConfigPage(request); });
    server.on("/wifi_ap_config", HTTP_GET, [this](AsyncWebServerRequest* request) { handleAPConfigPage(request); });
    server.on("/factory_reset", HTTP_GET, [this](AsyncWebServerRequest* request) { handleFactoryResetPage(request); });

    server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetConfig(request); });
    server.on("/api/config", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetConfig(request); });
    server.on("/api/mqtt_config", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetMQTTConfig(request); });
    server.on("/api/network_config", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetNetworkConfig(request); });
    server.on("/api/mems_config", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetMEMSConfig(request); });
    server.on("/api/ap_config", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetAPConfig(request); });
    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetStatus(request); });
    server.on("/api/fft_spectrum", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetFFTSpectrum(request); });
    server.on("/api/last_mqtt_payload", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetLastMQTTPayload(request); });
    server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) { handleReboot(request); });
    server.on("/api/reset", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetFactoryReset(request); });

    server.addHandler(&ws);
    server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");
    server.onNotFound([](AsyncWebServerRequest* request) {
        if (request->method() != HTTP_GET) {
            request->send(404, "text/plain", "Not Found");
            return;
        }

        String spiffs_file = "/www" + request->url();
        if (spiffs_file.endsWith("/")) {
            spiffs_file += "index.html";
        }

        INFO_PRINT("SPIFFS open requested: URL='%s' FILE='%s'",
                   request->url().c_str(),
                   spiffs_file.c_str());

        if (SPIFFS.exists(spiffs_file)) {
            request->send(SPIFFS, spiffs_file);
            return;
        }

        ERROR_PRINT("SPIFFS file not found: URL='%s' FILE='%s'",
                    request->url().c_str(),
                    spiffs_file.c_str());
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    is_running = true;
    return true;
}

void WebServer::stop() {
    if (is_running) {
        ws.cleanupClients();
        is_running = false;
    }
}

bool WebServer::updateConfig(const String& json_config) {
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, json_config);
    if (error) {
        ERROR_PRINT("Config JSON parse failed: %s", error.c_str());
        return false;
    }

    if (doc["wifi"]["ssid"].is<const char*>()) {
        strlcpy(g_system_config.wifi_ssid, doc["wifi"]["ssid"], sizeof(g_system_config.wifi_ssid));
    }
    if (doc["wifi"]["password"].is<const char*>()) {
        strlcpy(g_system_config.wifi_password, doc["wifi"]["password"], sizeof(g_system_config.wifi_password));
    }
    if (doc["wifi"]["ap_ssid"].is<const char*>()) {
        strlcpy(g_system_config.ap_ssid, doc["wifi"]["ap_ssid"], sizeof(g_system_config.ap_ssid));
    }
    if (doc["wifi"]["ap_password"].is<const char*>()) {
        strlcpy(g_system_config.ap_password, doc["wifi"]["ap_password"], sizeof(g_system_config.ap_password));
    }

    bool saved = g_storage.saveConfig(g_system_config);
    bool applied = g_wifi_handler.applyConfig(g_system_config, true);
    return saved && applied;
}

void WebServer::updateRealtimeData(const VibrationAnalysis& analysis, const SystemStatus& status) {
    if (!ws.count()) {
        return;
    }

    DynamicJsonDocument doc(2048);
    doc["ts"] = analysis.timestamp_us / 1000;
    doc["accel"]["x"] = analysis.rms_accel_x;
    doc["accel"]["y"] = analysis.rms_accel_y;
    doc["accel"]["z"] = analysis.rms_accel_z;
    doc["velocity"]["x"] = analysis.rms_velocity_x;
    doc["velocity"]["y"] = analysis.rms_velocity_y;
    doc["velocity"]["z"] = analysis.rms_velocity_z;
    doc["vibration_freq"]["x"] = analysis.vibration_freq_x;
    doc["vibration_freq"]["y"] = analysis.vibration_freq_y;
    doc["vibration_freq"]["z"] = analysis.vibration_freq_z;
    doc["fft"]["x"] = analysis.fft_peak_freq_x;
    doc["fft"]["y"] = analysis.fft_peak_freq_y;
    doc["fft"]["z"] = analysis.fft_peak_freq_z;
    doc["orientation"]["pitch"] = analysis.pitch;
    doc["orientation"]["roll"] = analysis.roll;
    doc["orientation"]["inclination"] = sqrt((analysis.pitch * analysis.pitch) + (analysis.roll * analysis.roll));
    doc["battery"] = status.battery_voltage;
    doc["rssi"] = status.wifi_rssi;
    doc["wifi_state"] = wifi_status_to_string(status.wifi_status);
    doc["mqtt"]["broker"] = g_system_config.mqtt_broker;
    doc["mqtt"]["status"] = mqttStatusToString(status.mqtt_status);
    doc["ap_ip"] = g_wifi_handler.getAPIPAddress();

    String json_str;
    serializeJson(doc, json_str);
    ws.textAll(json_str);
}

void WebServer::setupFileHandlers() {
    if (!SPIFFS.exists("/www")) {
        SPIFFS.mkdir("/www");
    }
}

void WebServer::handleRoot(AsyncWebServerRequest* request) {
    if (SPIFFS.exists("/www/index.html")) {
        sendSPIFFSFileWithLog(request, "/www/index.html", "text/html");
        return;
    }

    String body;
    body.reserve(1024);
    body += F("<div class='nav'><a href='/'>Dashboard</a><a href='/wifi_config'>WiFi Config</a><a href='/wifi_ap_config'>AP Config</a><a href='/factory_reset'>Factory Reset</a></div>");
    body += F("<h1>VIOT Dashboard</h1><p>Real-time vibration monitor with AP + STA WiFi management.</p>");
    body += F("<p>AP SSID: ");
    body += g_wifi_handler.getAPSSID();
    body += F("<br>AP IP: ");
    body += g_wifi_handler.getAPIPAddress();
    body += F("<br>WiFi state: ");
    body += wifi_status_to_string(g_wifi_handler.getStatus());
    body += F("</p>");
    request->send(200, "text/html", htmlShell("Dashboard", body));
}

void WebServer::handleGetConfig(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(2048);
    doc["wifi"]["ssid"] = g_system_config.wifi_ssid;
    doc["wifi"]["password"] = g_system_config.wifi_password;
    doc["wifi"]["ap_enabled"] = g_system_config.wifi_ap_enabled;
    doc["wifi"]["ap_ssid"] = g_system_config.ap_ssid;
    doc["wifi"]["ap_ssid_effective"] = g_wifi_handler.getAPSSID();
    doc["wifi"]["ap_password"] = g_system_config.ap_password;
    doc["wifi"]["sta_use_static_ip"] = g_system_config.sta_use_static_ip;
    doc["wifi"]["sta_static_ip"] = g_system_config.sta_static_ip;
    doc["wifi"]["sta_gateway"] = g_system_config.sta_gateway;
    doc["wifi"]["sta_subnet"] = g_system_config.sta_subnet;
    doc["wifi"]["sta_dns1"] = g_system_config.sta_dns1;
    doc["wifi"]["sta_dns2"] = g_system_config.sta_dns2;
    doc["wifi"]["state"] = wifi_status_to_string(g_wifi_handler.getStatus());
    doc["mqtt"]["broker"] = g_system_config.mqtt_broker;
    doc["mqtt"]["port"] = g_system_config.mqtt_port;
    doc["mqtt"]["client_id"] = g_system_config.mqtt_client_id;
    doc["mqtt"]["username"] = g_system_config.mqtt_username;
    doc["mqtt"]["password"] = g_system_config.mqtt_password;
    doc["mqtt"]["topic_publish"] = g_system_config.mqtt_topic_publish;
    doc["mqtt"]["topic_fft_x"] = g_system_config.mqtt_topic_fft_x;
    doc["mqtt"]["topic_fft_y"] = g_system_config.mqtt_topic_fft_y;
    doc["mqtt"]["topic_fft_z"] = g_system_config.mqtt_topic_fft_z;
    doc["mqtt"]["topic_subscribe"] = g_system_config.mqtt_topic_subscribe;
    doc["mqtt"]["publish_interval_s"] = g_system_config.mqtt_publish_interval_s;
    doc["mqtt"]["status"] = mqttStatusToString(g_system_status.mqtt_status);
    doc["adxl345"]["rate_hz"] = g_system_config.adxl345_rate_hz;
    doc["adxl345"]["range_g"] = g_system_config.adxl345_range_g;
    doc["adxl345"]["offset_x"] = g_system_config.adxl345_offset_x;
    doc["adxl345"]["offset_y"] = g_system_config.adxl345_offset_y;
    doc["adxl345"]["offset_z"] = g_system_config.adxl345_offset_z;
    doc["adxl345"]["int_threshold_mg"] = g_system_config.adxl345_int_threshold_mg;
    doc["adxl345"]["int_enabled"] = g_system_config.adxl345_int_enabled;
    
    doc["vibration"]["min_rms_g"] = g_system_config.vibration_min_rms_g;
    doc["vibration"]["min_peak_g"] = g_system_config.vibration_min_peak_g;
    doc["vibration"]["noise_floor_db"] = g_system_config.vibration_noise_floor_db;
    doc["vibration"]["deadband_g"] = g_system_config.vibration_deadband_g;
    doc["vibration"]["min_freq_hz"] = g_system_config.vibration_min_freq_hz;
    doc["vibration"]["max_freq_hz"] = g_system_config.vibration_max_freq_hz;
    
    doc["power"]["sleep_enabled"] = g_system_config.sleep_enabled;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServer::handleSetConfig(AsyncWebServerRequest* request) {
    if (!request->hasParam("ssid", true)) {
        request->send(400, "application/json", "{\"error\":\"ssid required\"}");
        return;
    }

    const String ssid = request->getParam("ssid", true)->value();
    const String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";

    strlcpy(g_system_config.wifi_ssid, ssid.c_str(), sizeof(g_system_config.wifi_ssid));
    strlcpy(g_system_config.wifi_password, password.c_str(), sizeof(g_system_config.wifi_password));

    if (!g_storage.saveConfig(g_system_config)) {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    g_wifi_handler.applyConfig(g_system_config, true);
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"WiFi config saved. STA reconnect scheduled.\"}");
}

void WebServer::handleSetMQTTConfig(AsyncWebServerRequest* request) {
    String broker = request->hasParam("broker", true) ? request->getParam("broker", true)->value() : "";
    uint16_t port = request->hasParam("port", true) ? static_cast<uint16_t>(request->getParam("port", true)->value().toInt()) : 1883;
    String client_id = request->hasParam("client_id", true) ? request->getParam("client_id", true)->value() : "";
    String username = request->hasParam("username", true) ? request->getParam("username", true)->value() : "";
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    String topic_publish = request->hasParam("topic_publish", true) ? request->getParam("topic_publish", true)->value() : "";
    String topic_fft_x = request->hasParam("topic_fft_x", true) ? request->getParam("topic_fft_x", true)->value() : "";
    String topic_fft_y = request->hasParam("topic_fft_y", true) ? request->getParam("topic_fft_y", true)->value() : "";
    String topic_fft_z = request->hasParam("topic_fft_z", true) ? request->getParam("topic_fft_z", true)->value() : "";
    String topic_subscribe = request->hasParam("topic_subscribe", true) ? request->getParam("topic_subscribe", true)->value() : "";
    uint16_t publish_interval_s = request->hasParam("publish_interval_s", true) ? static_cast<uint16_t>(request->getParam("publish_interval_s", true)->value().toInt()) : g_system_config.mqtt_publish_interval_s;

    if (broker.isEmpty()) {
        request->send(400, "application/json", "{\"error\":\"broker required\"}");
        return;
    }
    if (port == 0) {
        request->send(400, "application/json", "{\"error\":\"invalid port\"}");
        return;
    }

    strlcpy(g_system_config.mqtt_broker, broker.c_str(), sizeof(g_system_config.mqtt_broker));
    g_system_config.mqtt_port = port;
    strlcpy(g_system_config.mqtt_client_id, client_id.c_str(), sizeof(g_system_config.mqtt_client_id));
    strlcpy(g_system_config.mqtt_username, username.c_str(), sizeof(g_system_config.mqtt_username));
    strlcpy(g_system_config.mqtt_password, password.c_str(), sizeof(g_system_config.mqtt_password));
    strlcpy(g_system_config.mqtt_topic_publish, topic_publish.c_str(), sizeof(g_system_config.mqtt_topic_publish));
    strlcpy(g_system_config.mqtt_topic_fft_x, topic_fft_x.c_str(), sizeof(g_system_config.mqtt_topic_fft_x));
    strlcpy(g_system_config.mqtt_topic_fft_y, topic_fft_y.c_str(), sizeof(g_system_config.mqtt_topic_fft_y));
    strlcpy(g_system_config.mqtt_topic_fft_z, topic_fft_z.c_str(), sizeof(g_system_config.mqtt_topic_fft_z));
    strlcpy(g_system_config.mqtt_topic_subscribe, topic_subscribe.c_str(), sizeof(g_system_config.mqtt_topic_subscribe));
    if (strlen(g_system_config.mqtt_topic_publish) == 0) {
        strlcpy(g_system_config.mqtt_topic_publish, "viot/vibration", sizeof(g_system_config.mqtt_topic_publish));
    }
    if (strlen(g_system_config.mqtt_topic_fft_x) == 0) {
        strlcpy(g_system_config.mqtt_topic_fft_x, "viot/vibration/fft/x", sizeof(g_system_config.mqtt_topic_fft_x));
    }
    if (strlen(g_system_config.mqtt_topic_fft_y) == 0) {
        strlcpy(g_system_config.mqtt_topic_fft_y, "viot/vibration/fft/y", sizeof(g_system_config.mqtt_topic_fft_y));
    }
    if (strlen(g_system_config.mqtt_topic_fft_z) == 0) {
        strlcpy(g_system_config.mqtt_topic_fft_z, "viot/vibration/fft/z", sizeof(g_system_config.mqtt_topic_fft_z));
    }
    if (publish_interval_s < 1) {
        publish_interval_s = 1;
    } else if (publish_interval_s > 3600) {
        publish_interval_s = 3600;
    }
    g_system_config.mqtt_publish_interval_s = publish_interval_s;

    if (!g_storage.saveConfig(g_system_config)) {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    logConfigSummary("MQTT_UPDATE");
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"MQTT config saved. Reboot to reconnect with new settings.\"}");
}

void WebServer::handleSetNetworkConfig(AsyncWebServerRequest* request) {
    String ssid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
    String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    String ap_ssid = request->hasParam("ap_ssid", true) ? request->getParam("ap_ssid", true)->value() : "";
    String ap_password = request->hasParam("ap_password", true) ? request->getParam("ap_password", true)->value() : "";
    bool ap_enabled = !request->hasParam("ap_enabled", true) || request->getParam("ap_enabled", true)->value() == "1";
    bool sta_use_static_ip = request->hasParam("sta_use_static_ip", true) && request->getParam("sta_use_static_ip", true)->value() == "1";
    String sta_static_ip = request->hasParam("sta_static_ip", true) ? request->getParam("sta_static_ip", true)->value() : "";
    String sta_gateway = request->hasParam("sta_gateway", true) ? request->getParam("sta_gateway", true)->value() : "";
    String sta_subnet = request->hasParam("sta_subnet", true) ? request->getParam("sta_subnet", true)->value() : "";
    String sta_dns1 = request->hasParam("sta_dns1", true) ? request->getParam("sta_dns1", true)->value() : "";
    String sta_dns2 = request->hasParam("sta_dns2", true) ? request->getParam("sta_dns2", true)->value() : "";

    if (!ap_password.isEmpty() && ap_password.length() < 8) {
        request->send(400, "application/json", "{\"error\":\"AP password must be at least 8 characters\"}");
        return;
    }
    if (sta_use_static_ip && (sta_static_ip.isEmpty() || sta_gateway.isEmpty() || sta_subnet.isEmpty())) {
        request->send(400, "application/json", "{\"error\":\"static IP mode requires ip, gateway, subnet\"}");
        return;
    }

    strlcpy(g_system_config.wifi_ssid, ssid.c_str(), sizeof(g_system_config.wifi_ssid));
    strlcpy(g_system_config.wifi_password, password.c_str(), sizeof(g_system_config.wifi_password));
    strlcpy(g_system_config.ap_ssid, ap_ssid.c_str(), sizeof(g_system_config.ap_ssid));
    strlcpy(g_system_config.ap_password, ap_password.c_str(), sizeof(g_system_config.ap_password));
    g_system_config.wifi_ap_enabled = ap_enabled;
    g_system_config.sta_use_static_ip = sta_use_static_ip;
    strlcpy(g_system_config.sta_static_ip, sta_static_ip.c_str(), sizeof(g_system_config.sta_static_ip));
    strlcpy(g_system_config.sta_gateway, sta_gateway.c_str(), sizeof(g_system_config.sta_gateway));
    strlcpy(g_system_config.sta_subnet, sta_subnet.c_str(), sizeof(g_system_config.sta_subnet));
    strlcpy(g_system_config.sta_dns1, sta_dns1.c_str(), sizeof(g_system_config.sta_dns1));
    strlcpy(g_system_config.sta_dns2, sta_dns2.c_str(), sizeof(g_system_config.sta_dns2));

    if (!g_storage.saveConfig(g_system_config)) {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    if (!g_wifi_handler.applyConfig(g_system_config, true)) {
        request->send(500, "application/json", "{\"error\":\"failed to apply network config\"}");
        return;
    }

    logConfigSummary("NETWORK_UPDATE");
    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Network config applied.\"}");
}

void WebServer::handleSetMEMSConfig(AsyncWebServerRequest* request) {
    uint16_t rate_hz = request->hasParam("rate_hz", true) ? static_cast<uint16_t>(request->getParam("rate_hz", true)->value().toInt()) : g_system_config.adxl345_rate_hz;
    uint8_t range_g = request->hasParam("range_g", true) ? static_cast<uint8_t>(request->getParam("range_g", true)->value().toInt()) : g_system_config.adxl345_range_g;
    float offset_x = request->hasParam("offset_x", true) ? request->getParam("offset_x", true)->value().toFloat() : g_system_config.adxl345_offset_x;
    float offset_y = request->hasParam("offset_y", true) ? request->getParam("offset_y", true)->value().toFloat() : g_system_config.adxl345_offset_y;
    float offset_z = request->hasParam("offset_z", true) ? request->getParam("offset_z", true)->value().toFloat() : g_system_config.adxl345_offset_z;
    uint16_t int_threshold_mg = request->hasParam("int_threshold_mg", true) ? static_cast<uint16_t>(request->getParam("int_threshold_mg", true)->value().toInt()) : g_system_config.adxl345_int_threshold_mg;
    bool int_enabled = !request->hasParam("int_enabled", true) || request->getParam("int_enabled", true)->value() == "1";
    
    // Vibration parameters
    float min_rms_g = request->hasParam("min_rms_g", true) ? request->getParam("min_rms_g", true)->value().toFloat() : g_system_config.vibration_min_rms_g;
    float min_peak_g = request->hasParam("min_peak_g", true) ? request->getParam("min_peak_g", true)->value().toFloat() : g_system_config.vibration_min_peak_g;
    float noise_floor_db = request->hasParam("noise_floor_db", true) ? request->getParam("noise_floor_db", true)->value().toFloat() : g_system_config.vibration_noise_floor_db;
    float deadband_g = request->hasParam("deadband_g", true) ? request->getParam("deadband_g", true)->value().toFloat() : g_system_config.vibration_deadband_g;
    float min_freq_hz = request->hasParam("min_freq_hz", true) ? request->getParam("min_freq_hz", true)->value().toFloat() : g_system_config.vibration_min_freq_hz;
    float max_freq_hz = request->hasParam("max_freq_hz", true) ? request->getParam("max_freq_hz", true)->value().toFloat() : g_system_config.vibration_max_freq_hz;

    g_system_config.adxl345_rate_hz = rate_hz;
    g_system_config.adxl345_range_g = range_g;
    g_system_config.adxl345_offset_x = offset_x;
    g_system_config.adxl345_offset_y = offset_y;
    g_system_config.adxl345_offset_z = offset_z;
    g_system_config.adxl345_int_threshold_mg = int_threshold_mg;
    g_system_config.adxl345_int_enabled = int_enabled;
    
    g_system_config.vibration_min_rms_g = min_rms_g;
    g_system_config.vibration_min_peak_g = min_peak_g;
    g_system_config.vibration_noise_floor_db = noise_floor_db;
    g_system_config.vibration_deadband_g = deadband_g;
    g_system_config.vibration_min_freq_hz = min_freq_hz;
    g_system_config.vibration_max_freq_hz = max_freq_hz;

    if (!g_storage.saveConfig(g_system_config)) {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    g_mems_sensor.setDataRate(rate_hz);
    g_mems_sensor.setRange(range_g);
    g_mems_sensor.setOffset(offset_x, offset_y, offset_z);
    g_mems_sensor.setInterruptThreshold(int_threshold_mg);
    if (int_enabled) {
        g_mems_sensor.setupInterrupt(GPIO_ADXL345_INT1, true);
    } else {
        g_mems_sensor.setupInterrupt(GPIO_ADXL345_INT1, false);
    }

    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"MEMS config applied.\"}");
}

void WebServer::handleGetStatus(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(768);
    doc["debug_mode"] = g_debug_mode;
    doc["uptime_sec"] = millis() / 1000;
    doc["battery_v"] = g_system_status.battery_voltage;
    doc["wifi_rssi"] = g_system_status.wifi_rssi;
    doc["wifi_state"] = wifi_status_to_string(g_wifi_handler.getStatus());
    doc["ap_ip"] = g_wifi_handler.getAPIPAddress();
    doc["sta_ip"] = g_wifi_handler.isConnected() ? g_wifi_handler.getIPAddress() : "";
    doc["mqtt_connected"] = (g_system_status.mqtt_status == MQTTSTATUS_CONNECTED);
    doc["mqtt_status"] = mqttStatusToString(g_system_status.mqtt_status);
    doc["mqtt_broker"] = g_system_config.mqtt_broker;

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServer::handleGetFFTSpectrum(AsyncWebServerRequest* request) {
    char axis = 'x';
    if (request->hasParam("axis")) {
        String axis_str = request->getParam("axis")->value();
        if (axis_str.length() > 0) {
            axis = axis_str[0];
        }
    }

    float freq_hz[MEMSSensor::FFT_DISPLAY_POINTS];
    float amp_mm_s[MEMSSensor::FFT_DISPLAY_POINTS];
    uint16_t points = 0;
    if (!g_mems_sensor.getFFTSpectrum(axis, freq_hz, amp_mm_s, MEMSSensor::FFT_DISPLAY_POINTS, points)) {
        request->send(500, "application/json", "{\"error\":\"fft spectrum unavailable\"}");
        return;
    }

    DynamicJsonDocument doc(8192);
    doc["axis"] = String(axis);
    JsonArray freq = doc.createNestedArray("freq_hz");
    JsonArray amp = doc.createNestedArray("amp_mm_s");
    for (uint16_t i = 0; i < points; ++i) {
        freq.add(freq_hz[i]);
        amp.add(amp_mm_s[i]);
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void WebServer::handleGetLastMQTTPayload(AsyncWebServerRequest* request) {
    String payload = g_mqtt_handler.getLastPayload();
    if (payload.length() == 0) {
        payload = "{}";
    }
    request->send(200, "application/json", payload);
}

void WebServer::handleWiFiConfigPage(AsyncWebServerRequest* request) {
    request->send(200, "text/html", renderWiFiConfigPage());
}

void WebServer::handleAPConfigPage(AsyncWebServerRequest* request) {
    request->send(200, "text/html", renderAPConfigPage());
}

void WebServer::handleFactoryResetPage(AsyncWebServerRequest* request) {
    request->send(200, "text/html", renderFactoryResetPage());
}

void WebServer::handleSetAPConfig(AsyncWebServerRequest* request) {
    String ap_ssid = request->hasParam("ap_ssid", true) ? request->getParam("ap_ssid", true)->value() : "";
    String ap_password = request->hasParam("ap_password", true) ? request->getParam("ap_password", true)->value() : "";

    if (!ap_password.isEmpty() && ap_password.length() < 8) {
        request->send(400, "application/json", "{\"error\":\"AP password must be at least 8 characters\"}");
        return;
    }

    strlcpy(g_system_config.ap_ssid, ap_ssid.c_str(), sizeof(g_system_config.ap_ssid));
    strlcpy(g_system_config.ap_password, ap_password.c_str(), sizeof(g_system_config.ap_password));

    if (!g_storage.saveConfig(g_system_config)) {
        request->send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }

    if (!g_wifi_handler.applyConfig(g_system_config, false)) {
        request->send(500, "application/json", "{\"error\":\"failed to restart AP\"}");
        return;
    }

    request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"AP config applied immediately\"}");
}

void WebServer::handleReboot(AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"status\":\"rebooting\"}");
    delay(250);
    ESP.restart();
}

void WebServer::handleFactoryReset(AsyncWebServerRequest* request) {
    handleSetFactoryReset(request);
}

void WebServer::handleSetFactoryReset(AsyncWebServerRequest* request) {
    bool clear_spiffs = request->hasParam("clear_spiffs", true) && request->getParam("clear_spiffs", true)->value() == "true";

    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_restore();

    if (!g_storage.factoryReset(clear_spiffs)) {
        request->send(500, "application/json", "{\"error\":\"factory reset failed\"}");
        return;
    }

    request->send(200, "application/json", "{\"status\":\"resetting\"}");
    delay(300);
    ESP.restart();
}

void WebServer::onWsEvent(AsyncWebSocket* server,
                          AsyncWebSocketClient* client,
                          AwsEventType type,
                          void* arg,
                          uint8_t* data,
                          size_t len) {
    (void)server;
    (void)arg;
    (void)data;
    (void)len;

    switch (type) {
        case WS_EVT_CONNECT:
            DEBUG_PRINT("WebSocket client %u connected", client->id());
            break;
        case WS_EVT_DISCONNECT:
            DEBUG_PRINT("WebSocket client %u disconnected", client->id());
            break;
        default:
            break;
    }
}

String WebServer::renderWiFiConfigPage() const {
    String body;
    body.reserve(2200);
    body += F("<div class='nav'><a href='/'>Dashboard</a><a href='/wifi_ap_config'>AP Config</a><a href='/factory_reset'>Factory Reset</a></div>");
    body += F("<h1>WiFi STA Config</h1><p>STA retries are capped at 5 attempts, 5 seconds each. If connection fails the unit stays in AP-only service mode until you update the config or the next wake cycle.</p>");
    body += F("<form method='POST' action='/api/config'>");
    body += F("<label>WiFi SSID</label><input name='ssid' maxlength='63' value='");
    body += g_system_config.wifi_ssid;
    body += F("'>");
    body += F("<label>WiFi Password</label><input type='password' name='password' maxlength='63' value='");
    body += g_system_config.wifi_password;
    body += F("'>");
    body += F("<div class='hint'>Current state: ");
    body += wifi_status_to_string(g_wifi_handler.getStatus());
    body += F("</div><button type='submit'>Save and Retry STA</button></form>");
    return htmlShell("WiFi Config", body);
}

String WebServer::renderAPConfigPage() const {
    String body;
    body.reserve(2200);
    body += F("<div class='nav'><a href='/'>Dashboard</a><a href='/wifi_config'>WiFi Config</a><a href='/factory_reset'>Factory Reset</a></div>");
    body += F("<h1>AP Config</h1><p>AP mode is always active while the device is awake. Updating this page restarts the AP immediately without rebooting the ESP32.</p>");
    body += F("<form method='POST' action='/api/ap_config'>");
    body += F("<label>AP SSID</label><input name='ap_ssid' maxlength='31' placeholder='Default: VIOT_XXXX' value='");
    body += g_system_config.ap_ssid;
    body += F("'>");
    body += F("<label>AP Password</label><input type='password' name='ap_password' minlength='8' maxlength='63' value='");
    body += g_system_config.ap_password;
    body += F("'>");
    body += F("<div class='hint'>Leave SSID blank to use ");
    body += g_wifi_handler.getAPSSID();
    body += F(". Password must be at least 8 characters.</div><button type='submit'>Apply AP Settings</button></form>");
    return htmlShell("AP Config", body);
}

String WebServer::renderFactoryResetPage() const {
    String body;
    body.reserve(2200);
    body += F("<div class='nav'><a href='/'>Dashboard</a><a href='/wifi_config'>WiFi Config</a><a href='/wifi_ap_config'>AP Config</a></div>");
    body += F("<h1>Factory Reset</h1><p>This removes the saved JSON config, clears stored WiFi credentials, and restarts the device.</p>");
    body += F("<form method='POST' action='/api/reset' onsubmit=\"return confirm('Reset to default settings and reboot now?');\">");
    body += F("<label><input type='checkbox' name='clear_spiffs' value='true' style='width:auto;margin-right:8px'>Also format SPIFFS</label>");
    body += F("<button type='submit' class='danger'>Reset to Default</button></form>");
    return htmlShell("Factory Reset", body);
}

void web_task(void* parameter) {
    (void)parameter;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
        if (watermark < STACK_LOW_WATERMARK_WORDS) {
            ERROR_PRINT("Web task stack low: %u words free", static_cast<unsigned>(watermark));
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    }
}
