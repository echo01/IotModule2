#include "discovery_service.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <cstring>

#include "wifi_handler.h"

namespace {
constexpr const char* DISCOVERY_REQUEST_TOKEN = "VIOT_DISCOVER";
constexpr const char* DISCOVERY_REQUEST_TOKEN_ALT = "DISCOVER_VIOT";

String sanitizeHostnamePart(const char* input, const char* fallback) {
    String raw = (input != nullptr && input[0] != '\0') ? String(input) : String(fallback);
    raw.toLowerCase();

    String out;
    out.reserve(raw.length() + 8);
    bool last_dash = false;

    for (size_t i = 0; i < raw.length(); ++i) {
        const char c = raw[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
            last_dash = false;
            continue;
        }

        if (!last_dash && out.length() > 0) {
            out += '-';
            last_dash = true;
        }
    }

    while (out.endsWith("-")) {
        out.remove(out.length() - 1);
    }

    if (out.isEmpty()) {
        out = fallback;
    }

    if (out.length() > 48) {
        out.remove(48);
    }

    return out;
}

String buildDeviceSuffix() {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(suffix);
}
}

DiscoveryService::DiscoveryService()
    : config{},
      udp_started(false),
      mdns_started(false),
      last_sta_connected(false),
      last_ap_available(false),
      last_sta_ip(),
      last_ap_ip(),
      hostname{},
      instance_name{} {
}

bool DiscoveryService::begin(const SystemConfig& new_config) {
    config = new_config;
    updateHostnames();
    return true;
}

void DiscoveryService::applyConfig(const SystemConfig& new_config) {
    config = new_config;
    updateHostnames();
}

void DiscoveryService::loop(const WiFiHandler& wifi) {
    syncServices(wifi);
    pollUdp(wifi);
}

void DiscoveryService::stop() {
    stopMdns();
    stopUdp();
    last_sta_connected = false;
    last_ap_available = false;
    last_sta_ip = IPAddress();
    last_ap_ip = IPAddress();
}

String DiscoveryService::buildDiscoveryJson(const WiFiHandler& wifi) const {
    JsonDocument doc;
    const bool sta_connected = wifi.isConnected();
    const String sta_ip = sta_connected ? wifi.getIPAddress() : "";
    const String ap_ip = wifi.getAPIPAddress();
    const String host = getHostname();

    doc["protocol"] = DISCOVERY_PROTOCOL_ID;
    doc["device_type"] = "viot-sensor-node";
    doc["device_name"] = host;
    doc["device_id"] = host;
    doc["hostname"] = host;
    doc["mdns_host"] = host + ".local";
    doc["mdns_service"] = "_" DISCOVERY_SERVICE_NAME "._tcp.local";
    doc["web_port"] = WEB_SERVER_PORT;
    doc["udp_port"] = DISCOVERY_UDP_PORT;
    doc["discover_path"] = "/api/discover";
    doc["status_path"] = "/api/status";
    doc["wifi_state"] = wifi_status_to_string(wifi.getStatus());
    doc["sta_connected"] = sta_connected;
    doc["sta_ip"] = sta_ip;
    doc["ap_ip"] = ap_ip;
    doc["ap_ssid"] = wifi.getAPSSID();
    doc["mac"] = wifi.getMACAddress();
    doc["mqtt_client_id"] = g_system_config.mqtt_client_id;
    doc["rssi"] = wifi.getRSSI();
    doc["uptime_sec"] = millis() / 1000;
    doc["mdns_ready"] = mdns_started;
    doc["udp_ready"] = udp_started;
    doc["fw_build_date"] = __DATE__;
    doc["fw_build_time"] = __TIME__;

    String json;
    json.reserve(measureJson(doc) + 1);
    serializeJson(doc, json);
    return json;
}

String DiscoveryService::getHostname() const {
    return String(hostname);
}

bool DiscoveryService::isMdnsRunning() const {
    return mdns_started;
}

bool DiscoveryService::isUdpRunning() const {
    return udp_started;
}

void DiscoveryService::updateHostnames() {
    String base = sanitizeHostnamePart(config.mqtt_client_id, "viot-sensor");
    if (base == "viot" || base == "esp32" || base == "esp32-viot") {
        base += "-";
        base += buildDeviceSuffix();
    }

    snprintf(hostname, sizeof(hostname), "%s", base.c_str());
    snprintf(instance_name, sizeof(instance_name), "VIOT Sensor %s", buildDeviceSuffix().c_str());
}

void DiscoveryService::syncServices(const WiFiHandler& wifi) {
    const bool sta_connected = wifi.isConnected();
    const String ap_ip_text = wifi.getAPIPAddress();
    const bool ap_available = !ap_ip_text.isEmpty() && ap_ip_text != "0.0.0.0";
    const IPAddress sta_ip = sta_connected ? WiFi.localIP() : IPAddress();
    const IPAddress ap_ip = WiFi.softAPIP();

    if (!udp_started && (sta_connected || ap_available)) {
        startUdp();
    } else if (udp_started && !sta_connected && !ap_available) {
        stopUdp();
    }

    if (sta_connected) {
        if (!mdns_started || !last_sta_connected || sta_ip != last_sta_ip) {
            stopMdns();
            startMdns();
        }
    } else if (mdns_started) {
        stopMdns();
    }

    last_sta_connected = sta_connected;
    last_ap_available = ap_available;
    last_sta_ip = sta_ip;
    last_ap_ip = ap_ip;
}

void DiscoveryService::startUdp() {
    if (udp_started) {
        return;
    }

    if (!udp.begin(DISCOVERY_UDP_PORT)) {
        ERROR_PRINT("UDP discovery failed to bind port %u", DISCOVERY_UDP_PORT);
        return;
    }

    udp_started = true;
    INFO_PRINT("UDP discovery listening on port %u", DISCOVERY_UDP_PORT);
}

void DiscoveryService::stopUdp() {
    if (!udp_started) {
        return;
    }

    udp.stop();
    udp_started = false;
    INFO_PRINT("UDP discovery stopped");
}

void DiscoveryService::startMdns() {
    if (mdns_started) {
        return;
    }

    if (!MDNS.begin(hostname)) {
        ERROR_PRINT("mDNS start failed for host %s", hostname);
        return;
    }

    MDNS.setInstanceName(instance_name);
    MDNS.addService(DISCOVERY_SERVICE_NAME, "tcp", WEB_SERVER_PORT);
    MDNS.addServiceTxt(DISCOVERY_SERVICE_NAME, "tcp", "path", "/api/discover");
    MDNS.addServiceTxt(DISCOVERY_SERVICE_NAME, "tcp", "protocol", DISCOVERY_PROTOCOL_ID);
    MDNS.addServiceTxt(DISCOVERY_SERVICE_NAME, "tcp", "id", static_cast<const char*>(hostname));
    const String mac = WiFi.macAddress();
    MDNS.addServiceTxt(DISCOVERY_SERVICE_NAME, "tcp", "mac", mac.c_str());
    mdns_started = true;
    INFO_PRINT("mDNS ready: http://%s.local:%u", hostname, WEB_SERVER_PORT);
}

void DiscoveryService::stopMdns() {
    if (!mdns_started) {
        return;
    }

    MDNS.end();
    mdns_started = false;
    INFO_PRINT("mDNS stopped");
}

void DiscoveryService::pollUdp(const WiFiHandler& wifi) {
    if (!udp_started) {
        return;
    }

    const int packet_size = udp.parsePacket();
    if (packet_size <= 0) {
        return;
    }

    char buffer[128];
    const int read_len = udp.read(buffer, sizeof(buffer) - 1);
    if (read_len <= 0) {
        return;
    }

    buffer[read_len] = '\0';
    const String payload(buffer);
    if (!shouldRespondToPacket(payload)) {
        return;
    }

    const String response = buildDiscoveryJson(wifi);
    if (!udp.beginPacket(udp.remoteIP(), udp.remotePort())) {
        return;
    }
    udp.write(reinterpret_cast<const uint8_t*>(response.c_str()), response.length());
    udp.endPacket();

    INFO_PRINT("UDP discovery response sent to %s:%u",
               udp.remoteIP().toString().c_str(),
               udp.remotePort());
}

bool DiscoveryService::shouldRespondToPacket(const String& payload) const {
    if (payload.equalsIgnoreCase(DISCOVERY_REQUEST_TOKEN) ||
        payload.equalsIgnoreCase(DISCOVERY_REQUEST_TOKEN_ALT)) {
        return true;
    }

    if (payload.indexOf("\"type\":\"discover\"") >= 0 ||
        payload.indexOf("\"protocol\":\"" DISCOVERY_PROTOCOL_ID "\"") >= 0) {
        return true;
    }

    return false;
}
