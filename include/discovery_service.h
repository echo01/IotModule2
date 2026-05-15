#ifndef DISCOVERY_SERVICE_H
#define DISCOVERY_SERVICE_H

#include <WiFiUdp.h>
#include "config.h"
#include "common.h"

class WiFiHandler;

class DiscoveryService {
public:
    DiscoveryService();

    bool begin(const SystemConfig& config);
    void loop(const WiFiHandler& wifi);
    void applyConfig(const SystemConfig& config);
    void stop();

    String buildDiscoveryJson(const WiFiHandler& wifi) const;
    String getHostname() const;
    bool isMdnsRunning() const;
    bool isUdpRunning() const;

private:
    SystemConfig config;
    WiFiUDP udp;
    bool udp_started;
    bool mdns_started;
    bool last_sta_connected;
    bool last_ap_available;
    IPAddress last_sta_ip;
    IPAddress last_ap_ip;
    char hostname[64];
    char instance_name[96];

    void updateHostnames();
    void syncServices(const WiFiHandler& wifi);
    void startUdp();
    void stopUdp();
    void startMdns();
    void stopMdns();
    void pollUdp(const WiFiHandler& wifi);
    bool shouldRespondToPacket(const String& payload) const;
};

#endif // DISCOVERY_SERVICE_H
