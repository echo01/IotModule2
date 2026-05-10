#ifndef MQTT_HANDLER_H
#define MQTT_HANDLER_H

#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>
#include "config.h"
#include "common.h"
#include "sensors.h"

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
    bool shouldComputeFFT() const;
    bool hasPendingFFTWork() const;
    void noteFFTComputationRound(uint64_t timestamp_us);
    bool publishPendingFFTIfReady();
    bool isCommandPublishSettling() const;
    
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
    char pending_fft_axis;
    uint8_t pending_fft_rounds;
    bool pending_fft_ready;
    bool clear_retained_command_after_publish;
    uint64_t pending_fft_timestamp_us;
    char pending_request_id[64];
    char last_completed_request_id[64];
    char last_completed_axis;
    uint8_t pending_fft_step_hz;
    bool pending_processing_ack;
    uint32_t command_publish_settle_until_ms;
    float pending_fft_freq_hz[MQTT_FFT_POINTS];
    float pending_fft_amp_mm_s[MQTT_FFT_POINTS];
    uint16_t pending_fft_points;
    bool pending_fft_snapshot_valid;
    SemaphoreHandle_t mqtt_mutex;
    
    // Callbacks
    static void mqtt_callback(char* topic, byte* payload, unsigned int length);
    
    // Helper functions
    bool buildVibrationJSON(String& json, const VibrationAnalysis& analysis,
                           const MQTTPayload& payload);
    bool buildFFTAxisJSON(String& json, const MQTTPayload& payload, char axis);
    bool buildFFTAxisJSONFromSnapshot(String& json, char axis, uint64_t timestamp_us) const;
    bool buildStatusJSON(String& json, const SystemStatus& status);
    bool publishCommandAck(const char* status, char axis, const char* detail = nullptr);
    void buildCommandAckTopic(char* out, size_t out_size) const;
    void buildCommandResultTopic(char* out, size_t out_size) const;
    void updatePublishSummary(bool success,
                              uint16_t main_size,
                              uint16_t fft_x_size,
                              uint16_t fft_y_size,
                              uint16_t fft_z_size);
    void recordSubscribeMessage(unsigned int length);
    void queueFFTRequest(char axis, uint8_t step_hz, bool clear_after_publish);
    void clearPendingFFTRequest();
    bool parseFFTCommand(const char* topic, const char* payload, unsigned int length, char& out_axis, uint8_t& out_step_hz, char* out_request_id, size_t out_request_id_size) const;
    bool lockClient(TickType_t wait_ticks = pdMS_TO_TICKS(1000));
    void unlockClient();
    bool capturePendingFFTSnapshot();
};

// Task function
void mqtt_task(void* parameter);

#endif // MQTT_HANDLER_H
