#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

class WiFiManagerESP {
public:
    void begin();
    void loop();
    bool connected();
    String ipAddress();

private:
    void connectBlocking(uint32_t timeoutMs);
    void printStatus();
    unsigned long disconnectSince_ = 0;
    unsigned long lastAttempt_ = 0;
    bool wasConnected_ = false;
};

extern WiFiManagerESP wifiManager;

#endif
