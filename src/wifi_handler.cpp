#include "wifi_handler.h"
#include <esp_heap_caps.h>

WiFiHandler::WiFiHandler()
    : current_status(WIFI_INIT),
      state_mutex(xSemaphoreCreateMutex()),
      connect_attempt_started_at(0),
      last_retry_started_at(0),
      retry_count(0),
      sta_attempt_requested(false),
      ap_started(false),
      last_sta_reason(WL_IDLE_STATUS) {
}

WiFiHandler::~WiFiHandler() {
    disconnect();
    if (state_mutex != nullptr) {
        vSemaphoreDelete(state_mutex);
        state_mutex = nullptr;
    }
}

bool WiFiHandler::begin(const SystemConfig& cfg) {
    config = cfg;
    INFO_PRINT("WiFi Handler initializing...");

    WiFi.persistent(false);
    // WiFi.setSleep(WIFI_PS_MIN_MODEM);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_STA);
    ensureAPRunning();

    if (shouldAttemptSTA()) {
        requestReconnect();
    } else {
        setStatus(shouldRunAPMode() ? AP_ONLY : WIFI_INIT);
    }

    return true;
}

void WiFiHandler::loop() {
    const uint32_t now = millis();
    ensureAPRunning();

    switch (getStatus()) {
        case WIFI_INIT:
            if (sta_attempt_requested && shouldAttemptSTA()) {
                beginSTAConnectAttempt(now);
            } else {
                setStatus(AP_ONLY);
            }
            break;

        case WIFI_CONNECTING:
            handleConnectingState(now);
            break;

        case WIFI_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                last_sta_reason = WiFi.status();
                if (g_debug_mode) {
                    DEBUG_PRINT("WiFi status changed: %s", reasonToString(last_sta_reason));
                }
                setStatus(AP_ONLY);
            }
            break;

        case WIFI_FAILED:
            break;

        case AP_ONLY:
            if (sta_attempt_requested && shouldAttemptSTA()) {
                beginSTAConnectAttempt(now);
            }
            break;
    }
}

void WiFiHandler::requestReconnect() {
    sta_attempt_requested = true;
    retry_count = 0;
    connect_attempt_started_at = 0;
    last_retry_started_at = 0;
    last_sta_reason = WL_IDLE_STATUS;

    if (WiFi.status() != WL_CONNECTED) {
        setStatus(WIFI_INIT);
    }
}

bool WiFiHandler::applyConfig(const SystemConfig& new_config, bool reconnect_sta) {
    config = new_config;
    ensureAPRunning();

    if (reconnect_sta) {
        WiFi.disconnect(false, true);
        requestReconnect();
    } else if (!shouldAttemptSTA()) {
        WiFi.disconnect(false, true);
        sta_attempt_requested = false;
        setStatus(shouldRunAPMode() ? AP_ONLY : WIFI_INIT);
    }

    return true;
}

bool WiFiHandler::startAPMode(const String& ap_ssid, const String& ap_password) {
    String resolved_ssid = ap_ssid;
    if (resolved_ssid.isEmpty()) {
        resolved_ssid = buildDefaultAPSSID();
    }

    String resolved_password = ap_password;
    if (resolved_password.length() < 8) {
        resolved_password = "12345678";
    }

    WiFi.softAPdisconnect(true);
    delay(100);

    bool ok = WiFi.softAP(resolved_ssid.c_str(), resolved_password.c_str());
    if (!ok) {
        ERROR_PRINT("Failed to start AP mode");
        ap_started = false;
        return false;
    }

    ap_started = true;
    INFO_PRINT("AP started: %s", resolved_ssid.c_str());
    INFO_PRINT("AP Password: %s", resolved_password.c_str());
    INFO_PRINT("AP IP: %s", WiFi.softAPIP().toString().c_str());
    return true;
}

WiFiStatus WiFiHandler::getStatus() const {
    if (state_mutex != nullptr && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        WiFiStatus status = current_status;
        xSemaphoreGive(state_mutex);
        return status;
    }
    return current_status;
}

int8_t WiFiHandler::getRSSI() const {
    return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100;
}

bool WiFiHandler::isConnected() const {
    return getStatus() == WIFI_CONNECTED && WiFi.status() == WL_CONNECTED;
}

void WiFiHandler::reconnect() {
    requestReconnect();
}

void WiFiHandler::disconnect() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    ap_started = false;
    setStatus(AP_ONLY);
}

String WiFiHandler::getIPAddress() const {
    return isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

String WiFiHandler::getAPIPAddress() const {
    return WiFi.softAPIP().toString();
}

String WiFiHandler::getMACAddress() const {
    return WiFi.macAddress();
}

void WiFiHandler::setMode(WiFiMode_t mode) {
    WiFi.mode(mode);
}

String WiFiHandler::getAPSSID() const {
    if (strlen(config.ap_ssid) > 0) {
        return String(config.ap_ssid);
    }
    return buildDefaultAPSSID();
}

bool WiFiHandler::shouldAttemptSTA() const {
    return strlen(config.wifi_ssid) > 0;
}

void WiFiHandler::setStatus(WiFiStatus status) {
    if (state_mutex != nullptr && xSemaphoreTake(state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        current_status = status;
        xSemaphoreGive(state_mutex);
    } else {
        current_status = status;
    }

    if (g_debug_mode) {
        DEBUG_PRINT("System state: %s", wifi_status_to_string(status));
    }
}

void WiFiHandler::beginSTAConnectAttempt(uint32_t now) {
    if (!shouldAttemptSTA()) {
        sta_attempt_requested = false;
        setStatus(AP_ONLY);
        return;
    }

    if (retry_count >= WIFI_CONNECT_MAX_RETRIES) {
        setStatus(WIFI_FAILED);
        handleFailedState();
        return;
    }

    retry_count++;
    connect_attempt_started_at = now;
    last_retry_started_at = now;
    last_sta_reason = WL_IDLE_STATUS;
    setStatus(WIFI_CONNECTING);

    WiFi.disconnect(false, true);
    stopAPMode();
    WiFi.mode(WIFI_STA);
    applySTAIPConfig();
    WiFi.begin(config.wifi_ssid, config.wifi_password);

    if (g_debug_mode) {
        DEBUG_PRINT("Attempt %u/%u...", retry_count, WIFI_CONNECT_MAX_RETRIES);
    }
    INFO_PRINT("Connecting to %s", config.wifi_ssid);
}

void WiFiHandler::applySTAIPConfig() {
    IPAddress dhcp_ip(0, 0, 0, 0);
    if (!config.sta_use_static_ip) {
        WiFi.config(dhcp_ip, dhcp_ip, dhcp_ip);
        return;
    }

    IPAddress ip, gateway, subnet, dns1, dns2;
    bool ok = ip.fromString(config.sta_static_ip) &&
              gateway.fromString(config.sta_gateway) &&
              subnet.fromString(config.sta_subnet);

    if (!ok) {
        ERROR_PRINT("Invalid static IP config, fallback to DHCP");
        WiFi.config(dhcp_ip, dhcp_ip, dhcp_ip);
        return;
    }

    bool has_dns1 = dns1.fromString(config.sta_dns1);
    bool has_dns2 = dns2.fromString(config.sta_dns2);
    bool cfg_ok = false;

    if (has_dns1 && has_dns2) {
        cfg_ok = WiFi.config(ip, gateway, subnet, dns1, dns2);
    } else if (has_dns1) {
        cfg_ok = WiFi.config(ip, gateway, subnet, dns1);
    } else {
        cfg_ok = WiFi.config(ip, gateway, subnet);
    }

    if (cfg_ok) {
        INFO_PRINT("STA static IP enabled: IP=%s GW=%s MASK=%s",
                   config.sta_static_ip,
                   config.sta_gateway,
                   config.sta_subnet);
    } else {
        ERROR_PRINT("Failed to apply static IP config, fallback to DHCP");
        WiFi.config(dhcp_ip, dhcp_ip, dhcp_ip);
    }
}

void WiFiHandler::handleConnectingState(uint32_t now) {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        sta_attempt_requested = false;
        retry_count = 0;
        setStatus(WIFI_CONNECTED);
        stopAPMode();
        WiFi.mode(WIFI_STA);
        INFO_PRINT("WiFi connected: %s", WiFi.localIP().toString().c_str());
        INFO_PRINT("SoftAP disabled while STA is connected (test mode)");
        log_heap_state("WIFI_STA_CONNECTED");
        return;
    }

    if (status != WL_IDLE_STATUS && status != WL_DISCONNECTED) {
        last_sta_reason = status;
    }

    if ((now - connect_attempt_started_at) < WIFI_CONNECT_TIMEOUT_MS) {
        return;
    }

    last_sta_reason = status;
    WiFi.disconnect(false, true);

    if (g_debug_mode) {
        DEBUG_PRINT("WiFi STA failure reason: %s", reasonToString(last_sta_reason));
    }

    if (retry_count >= WIFI_CONNECT_MAX_RETRIES) {
        setStatus(WIFI_FAILED);
        handleFailedState();
        return;
    }

    if ((now - last_retry_started_at) >= WIFI_RETRY_DELAY_MS) {
        beginSTAConnectAttempt(now);
    }
}

void WiFiHandler::handleFailedState() {
    sta_attempt_requested = false;
    retry_count = WIFI_CONNECT_MAX_RETRIES;
    ERROR_PRINT("WiFi STA connection failed");
    ensureAPRunning();
    INFO_PRINT("Switching to %s mode",
               shouldRunAPMode() ? "AP-only (debug fallback)" : "STA-only fallback");
}

void WiFiHandler::ensureAPRunning() {
    if (!shouldRunAPMode()) {
        stopAPMode();
        if (WiFi.getMode() != WIFI_STA) {
            WiFi.mode(WIFI_STA);
        }
        return;
    }

    if (!ap_started || (WiFi.getMode() != WIFI_AP_STA && WiFi.getMode() != WIFI_AP)) {
        WiFi.mode(WIFI_AP_STA);
        startAPMode(config.ap_ssid, config.ap_password);
    }
}

void WiFiHandler::stopAPMode() {
    if (ap_started) {
        WiFi.softAPdisconnect(true);
        ap_started = false;
        INFO_PRINT("SoftAP stopped");
    }
}

bool WiFiHandler::shouldRunAPMode() const {
    if (!config.wifi_ap_enabled) {
        return false;
    }

    if (isConnected()) {
        return false;
    }

    if (g_debug_mode) {
        return true;
    }

    return false;
}

String WiFiHandler::buildDefaultAPSSID() const {
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "VIOT_%02X%02X", mac[4], mac[5]);
    return String(ssid);
}

const char* WiFiHandler::reasonToString(wl_status_t status) const {
    switch (status) {
        case WL_NO_SSID_AVAIL: return "NO_AP_FOUND";
        case WL_CONNECT_FAILED: return "AUTH_FAIL";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        case WL_IDLE_STATUS: return "IDLE";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        default: return "UNKNOWN";
    }
}

void wifi_task(void* parameter) {
    WiFiHandler* wifi = static_cast<WiFiHandler*>(parameter);
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1) {
        wifi->loop();

        UBaseType_t watermark = uxTaskGetStackHighWaterMark(nullptr);
        if (watermark < STACK_LOW_WATERMARK_WORDS) {
            ERROR_PRINT("WiFi task stack low: %u words free", static_cast<unsigned>(watermark));
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(250));
    }
}
