#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <WiFi.h>
#include <freertos/semphr.h>
#include "config.h"
#include "common.h"

class WiFiHandler {
public:
    WiFiHandler();
    ~WiFiHandler();
    
    // Initialize WiFi with stored config
    bool begin(const SystemConfig& config);
    
    // Trigger or maintain the non-blocking STA state machine
    void loop();

    // Request a new STA connection attempt sequence
    void requestReconnect();

    // Apply updated runtime config
    bool applyConfig(const SystemConfig& new_config, bool reconnect_sta);

    // Start AP mode
    bool startAPMode(const String& ap_ssid, const String& ap_password);

    // Get current status
    WiFiStatus getStatus() const;
    
    // Get RSSI
    int8_t getRSSI() const;
    
    // Check if connected
    bool isConnected() const;
    
    // Reconnect if disconnected
    void reconnect();
    
    // Disconnect
    void disconnect();
    
    // Get IP address
    String getIPAddress() const;
    String getAPIPAddress() const;

    // Get MAC address
    String getMACAddress() const;
    
    // Force mode update
    void setMode(WiFiMode_t mode);

    String getAPSSID() const;
    bool shouldAttemptSTA() const;
    bool hasReachedFailureLimit() const;
    uint8_t getRetryCount() const;
    void setSTASuppressed(bool suppressed);
    bool isSTASuppressed() const;

private:
    WiFiStatus current_status;
    SystemConfig config;
    mutable SemaphoreHandle_t state_mutex;
    uint32_t connect_attempt_started_at;
    uint32_t last_retry_started_at;
    uint8_t retry_count;
    bool sta_attempt_requested;
    bool ap_started;
    bool sta_suppressed;
    wl_status_t last_sta_reason;

    void setStatus(WiFiStatus status);
    void beginSTAConnectAttempt(uint32_t now);
    void handleConnectingState(uint32_t now);
    void handleFailedState();
    void ensureAPRunning();
    void stopAPMode();
    bool shouldRunAPMode() const;
    void applySTAIPConfig();
    String buildDefaultAPSSID() const;
    const char* reasonToString(wl_status_t status) const;
};

// Task function (will run on FreeRTOS)
void wifi_task(void* parameter);

#endif // WIFI_HANDLER_H
