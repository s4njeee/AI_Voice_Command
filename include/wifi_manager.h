#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

class WiFiManagerESP {
public:
    void begin();
    void loop();
    bool connected();
    String ipAddress();
};

extern WiFiManagerESP wifiManager;

#endif