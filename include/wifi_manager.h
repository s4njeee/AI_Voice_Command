#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>

class WiFiManagerESP {
public:
    void begin();
    void loop();
    bool connected();
    String ipAddress();

    // [xypher] Getters for local timezone/location fields
    String getCity() { return city_; }
    String getRegion() { return region_; }
    String getCountry() { return country_; }
    bool hasLocation() { return hasLocation_; }
    void fetchLocationAndTime();

private:
    bool connectOnce(uint32_t timeoutMs);
    void hardResetRadio();
    void scanAndDiagnose();
    bool connectToBestMatch(uint32_t timeoutMs);
    void onConnected();
    void pauseAudioForWifi();
    void resumeAudioAfterWifi();
    void printStatus();

    unsigned long disconnectSince_ = 0;
    unsigned long lastAttempt_ = 0;
    bool wasConnected_ = false;
    bool audioPaused_ = false;

    // [xypher] Geolocation storage
    String city_ = "";
    String region_ = "";
    String country_ = "";
    bool hasLocation_ = false;
};

extern WiFiManagerESP wifiManager;

#endif
