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

    // Last published payload (for UI/debug)
    String getLastPayload() const;

private:
    PubSubClient mqtt_client;
    MQTTStatus current_status;
    SystemConfig config;
    uint32_t last_publish_time;
    uint16_t publish_interval_s;
    String last_payload_json;
    
    // Callbacks
    static void mqtt_callback(char* topic, byte* payload, unsigned int length);
    
    // Helper functions
    bool buildVibrationJSON(String& json, const VibrationAnalysis& analysis,
                           const MQTTPayload& payload);
    bool buildFFTAxisJSON(String& json, const MQTTPayload& payload, char axis);
    bool buildStatusJSON(String& json, const SystemStatus& status);
};

// Task function
void mqtt_task(void* parameter);

#endif // MQTT_HANDLER_H
