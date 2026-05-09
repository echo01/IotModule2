#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "common.h"

class MQTTHandler {
public:
    MQTTHandler();
    ~MQTTHandler();
    
    // Initialize with WiFi client and config
    bool begin(WiFiClient* client, const SystemConfig& config);
    bool reconfigure(WiFiClient* client, const SystemConfig& config);
    
    // Connect to broker
    bool connect();
    
    // Disconnect from broker
    void disconnect();
    
    // Check if connected
    bool isConnected() const;
    
    // Publish vibration data
    bool publishVibrationData(const VibrationAnalysis& analysis,
                             const MQTTPayload& payload);
    
    // Publish system status
    bool publishSystemStatus(const SystemStatus& status);
    
    // Subscribe to commands
    bool subscribe(const String& topic);
    
    // Process incoming messages
    void loop();
    
    // Get MQTT status
    MQTTStatus getStatus() const;
    
    // Update publish interval
    void setPublishInterval(uint16_t seconds);
    void setNextPublishDueMs(uint32_t due_ms);

    // Publish summary for UI/debug
    MQTTPublishSummary getPublishSummary() const;

    // Test helper: pause MQTT activity temporarily during STA web access.
    void pauseForWebAccess(uint32_t duration_ms);
    bool isPaused() const;
    bool isTlsConnectInProgress() const;

private:
    PubSubClient mqtt_client;
    WiFiClient* wifi_client;
    WiFiClientSecure secure_client;
    MQTTStatus current_status;
    SystemConfig config;
    uint32_t last_publish_time;
    uint32_t pause_until_ms;
    uint16_t publish_interval_s;
    volatile bool tls_connect_in_progress;
    MQTTPublishSummary publish_summary;
    
    // Callbacks
    static void mqtt_callback(char* topic, byte* payload, unsigned int length);
    
    // Helper functions
    bool buildVibrationJSON(String& json, const VibrationAnalysis& analysis,
                           const MQTTPayload& payload);
    bool buildFFTAxisJSON(String& json, const MQTTPayload& payload, char axis);
    bool buildStatusJSON(String& json, const SystemStatus& status);
    void updatePublishSummary(bool success,
                              uint16_t main_size,
                              uint16_t fft_x_size,
                              uint16_t fft_y_size,
                              uint16_t fft_z_size);
    void recordSubscribeMessage(unsigned int length);
};

// Task function
void mqtt_task(void* parameter);

#endif // MQTT_HANDLER_H
