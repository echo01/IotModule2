#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include "config.h"
#include "common.h"

class WebServer {
public:
    WebServer();
    ~WebServer();
    
    // Initialize web server
    bool begin(uint16_t port = WEB_SERVER_PORT);
    
    // Stop server
    void stop();
    
    // Handle configuration update
    bool updateConfig(const String& json_config);
    
    // Update real-time data for WebSocket
    void updateRealtimeData(const VibrationAnalysis& analysis,
                            const SystemStatus& status);
    
    // Handle file upload (for config, certificates, etc.)
    void setupFileHandlers();
    
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    bool is_running;
    
    // Handler functions
    void handleRoot(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleSetConfig(AsyncWebServerRequest* request);
    void handleSetMQTTConfig(AsyncWebServerRequest* request);
    void handleSetNetworkConfig(AsyncWebServerRequest* request);
    void handleScanSSID(AsyncWebServerRequest* request);
    void handleSetMEMSConfig(AsyncWebServerRequest* request);
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetFFTSpectrum(AsyncWebServerRequest* request);
    void handleGetLastMQTTPayload(AsyncWebServerRequest* request);
    void handleWiFiConfigPage(AsyncWebServerRequest* request);
    void handleAPConfigPage(AsyncWebServerRequest* request);
    void handleFactoryResetPage(AsyncWebServerRequest* request);
    void handleSetAPConfig(AsyncWebServerRequest* request);
    void handleReboot(AsyncWebServerRequest* request);
    void handleFactoryReset(AsyncWebServerRequest* request);
    void handleSetFactoryReset(AsyncWebServerRequest* request);
    
    // WebSocket
    void onWsEvent(AsyncWebSocket* server,
                   AsyncWebSocketClient* client,
                   AwsEventType type,
                   void* arg,
                   uint8_t* data,
                   size_t len);
    String renderWiFiConfigPage() const;
    String renderAPConfigPage() const;
    String renderFactoryResetPage() const;
};

// Task function
void web_task(void* parameter);

#endif // WEB_SERVER_H
