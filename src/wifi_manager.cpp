#include "wifi_manager.h"
#include "config.h"
#include "secrets.h"
#include "microphone.h"
#include "speaker.h"

#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

WiFiManagerESP wifiManager;

static const char *wifiStatusText(wl_status_t status)
{
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID (name not found / wrong band)";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED (often wrong password)";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static const char *authModeText(wifi_auth_mode_t mode)
{
    switch (mode)
    {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "OTHER";
    }
}

void WiFiManagerESP::printStatus()
{
    Serial.print("WiFi status: ");
    Serial.print(wifiStatusText(WiFi.status()));
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("  RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.print(" dBm  IP: ");
        Serial.print(WiFi.localIP());
    }
    Serial.println();
}

void WiFiManagerESP::hardResetRadio()
{
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname("smartcane");
}

void WiFiManagerESP::pauseAudioForWifi()
{
    if (audioPaused_)
    {
        return;
    }

    // I2S DMA often knocks ESP32 WiFi offline — stop it while reconnecting
    microphone.end();
    speaker.end();
    audioPaused_ = true;
    delay(50);
}

void WiFiManagerESP::resumeAudioAfterWifi()
{
    if (!audioPaused_)
    {
        return;
    }

    microphone.begin();
    speaker.begin();
    audioPaused_ = false;
}

void WiFiManagerESP::scanAndDiagnose()
{
    Serial.println();
    Serial.println("Scanning for WiFi networks...");
    const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (n <= 0)
    {
        Serial.println("No networks found. Check antenna / 2.4 GHz.");
        return;
    }

    bool foundExact = false;
    int bestRssi = -999;
    int bestIndex = -1;

    Serial.printf("Found %d networks:\n", n);
    for (int i = 0; i < n; i++)
    {
        const String ssid = WiFi.SSID(i);
        const bool match = ssid.equals(WIFI_SSID);
        Serial.printf("  %s ch=%d RSSI=%d auth=%s%s\n",
                      ssid.c_str(),
                      WiFi.channel(i),
                      WiFi.RSSI(i),
                      authModeText(WiFi.encryptionType(i)),
                      match ? "  <== TARGET" : "");

        if (match)
        {
            foundExact = true;
            if (WiFi.RSSI(i) > bestRssi)
            {
                bestRssi = WiFi.RSSI(i);
                bestIndex = i;
            }
        }
    }

    if (!foundExact)
    {
        Serial.println();
        Serial.print("SSID not found exactly as: [");
        Serial.print(WIFI_SSID);
        Serial.println("]");
        Serial.println("ESP32 only sees 2.4 GHz. Enable 2.4 GHz or use a 2.4 GHz SSID.");
    }
    else if (bestIndex >= 0)
    {
        const wifi_auth_mode_t auth = WiFi.encryptionType(bestIndex);
        Serial.printf("Best match RSSI=%d dBm auth=%s\n", bestRssi, authModeText(auth));
        if (auth == WIFI_AUTH_WPA3_PSK)
        {
            Serial.println("WARNING: AP is WPA3-only. Set router to WPA2/WPA3 or WPA2.");
        }
    }

    WiFi.scanDelete();
}

bool WiFiManagerESP::connectToBestMatch(uint32_t timeoutMs)
{
    // Scan and connect to the strongest BSSID with our SSID (avoids flaky dual-AP setups)
    const int n = WiFi.scanNetworks(false, true);
    int bestIndex = -1;
    int bestRssi = -999;

    for (int i = 0; i < n; i++)
    {
        if (WiFi.SSID(i).equals(WIFI_SSID) && WiFi.RSSI(i) > bestRssi)
        {
            bestRssi = WiFi.RSSI(i);
            bestIndex = i;
        }
    }

    if (bestIndex < 0)
    {
        Serial.println("Target SSID missing in scan — trying WiFi.begin() anyway...");
        WiFi.scanDelete();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    else
    {
        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(bestIndex), 6);
        const int channel = WiFi.channel(bestIndex);
        Serial.printf("Connecting to BSSID %02X:%02X:%02X:%02X:%02X:%02X ch=%d RSSI=%d\n",
                      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                      channel, bestRssi);
        WiFi.scanDelete();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, channel, bssid);
    }

    const unsigned long start = millis();
    wl_status_t last = WL_IDLE_STATUS;
    while (millis() - start < timeoutMs)
    {
        const wl_status_t st = WiFi.status();
        if (st != last)
        {
            last = st;
            Serial.print("  -> ");
            Serial.println(wifiStatusText(st));
        }

        if (st == WL_CONNECTED)
        {
            return true;
        }

        if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)
        {
            delay(500);
            // one more plain begin attempt
            WiFi.disconnect(false, false);
            delay(100);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }

        delay(200);
        Serial.print(".");
    }

    Serial.println();
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManagerESP::connectOnce(uint32_t timeoutMs)
{
    hardResetRadio();
    scanAndDiagnose();
    return connectToBestMatch(timeoutMs);
}

void WiFiManagerESP::onConnected()
{
    wasConnected_ = true;
    disconnectSince_ = 0;

    // Disable modem sleep AFTER association (doing it too early can break connect)
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(78); // ~19.5 dBm

    Serial.println();
    Serial.println("================================");
    Serial.println("WiFi Connected!");
    Serial.println("================================");
    Serial.print("SSID       : ");
    Serial.println(WiFi.SSID());
    Serial.print("IP Address : ");
    Serial.println(WiFi.localIP());
    Serial.print("Gateway    : ");
    Serial.println(WiFi.gatewayIP());
    Serial.print("DNS Server : ");
    Serial.println(WiFi.dnsIP());
    Serial.print("RSSI       : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    IPAddress ip;
    if (WiFi.hostByName("api.groq.com", ip))
    {
        Serial.print("api.groq.com -> ");
        Serial.println(ip);
    }
    else
    {
        Serial.println("DNS lookup for api.groq.com failed");
    }
    Serial.println("================================");
    
    fetchLocationAndTime(); // [xypher] Retrieve location and synchronize NTP on connection
}

void WiFiManagerESP::begin()
{
    // [xypher] Set default offline/local location values on boot
    city_ = LOCAL_CITY;
    region_ = LOCAL_REGION;
    country_ = LOCAL_COUNTRY;
    hasLocation_ = true;

    Serial.println();
    Serial.println("================================");
    Serial.println("Connecting to WiFi...");
    Serial.print("SSID: [");
    Serial.print(WIFI_SSID);
    Serial.println("]");
    Serial.println("NOTE: ESP32 needs 2.4 GHz WiFi (not 5 GHz)");
    Serial.println("================================");

    // Do NOT call WiFi.config() before begin — it breaks DHCP on many ESP32 cores.

    if (connectOnce(WIFI_TIMEOUT))
    {
        onConnected();
        return;
    }

    Serial.println("First attempt failed — full retry...");
    delay(1000);
    if (connectOnce(WIFI_TIMEOUT))
    {
        onConnected();
        return;
    }

    Serial.println("================================");
    Serial.println("WiFi connection FAILED");
    printStatus();
    Serial.println("Fix checklist:");
    Serial.println(" 1. Password must be exact in include/secrets.h");
    Serial.println(" 2. Router must allow 2.4 GHz (or create a 2.4 GHz SSID)");
    Serial.println(" 3. Use WPA2 or WPA2/WPA3 mixed (not WPA3-only)");
    Serial.println(" 4. Disable AP/client isolation / MAC filter for this board");
    Serial.println("================================");
}

void WiFiManagerESP::loop()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        if (!wasConnected_)
        {
            onConnected();
            resumeAudioAfterWifi();
        }
        else if (audioPaused_)
        {
            resumeAudioAfterWifi();
        }
        wasConnected_ = true;
        disconnectSince_ = 0;
        return;
    }

    if (disconnectSince_ == 0)
    {
        disconnectSince_ = millis();
        Serial.print("WiFi lost (");
        Serial.print(wifiStatusText(WiFi.status()));
        Serial.println(") — confirming...");
        return;
    }

    if (millis() - disconnectSince_ < 2500)
    {
        return;
    }

    if (millis() - lastAttempt_ < 12000)
    {
        return;
    }

    lastAttempt_ = millis();
    wasConnected_ = false;

    Serial.println();
    Serial.println("WiFi still down — reconnecting (pausing mic/speaker)...");
    pauseAudioForWifi();
    printStatus();

    hardResetRadio();
    const bool ok = connectToBestMatch(20000);
    if (ok)
    {
        onConnected();
        resumeAudioAfterWifi();
        return;
    }

    Serial.println("Reconnect failed — will try again later.");
    printStatus();
    // Leave audio paused until WiFi returns so radio can recover
}

bool WiFiManagerESP::connected()
{
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManagerESP::ipAddress()
{
    if (!connected())
    {
        return "0.0.0.0";
    }

    return WiFi.localIP().toString();
}

// [xypher] Fetches location via IP Geolocation (ip-api.com) and synchronizes
// the ESP32 internal clock to local time using NTP.
void WiFiManagerESP::fetchLocationAndTime()
{
    Serial.println("[xypher] Fetching location and local time timezone offset...");
    HTTPClient http;
    http.setTimeout(10000);
    if (http.begin("http://ip-api.com/json"))
    {
        int httpCode = http.GET();
        if (httpCode == 200)
        {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error && doc["status"].as<String>() == "success")
            {
                city_ = doc["city"].as<String>();
                region_ = doc["regionName"].as<String>();
                country_ = doc["country"].as<String>();
                long offset = doc["offset"].as<long>();
                hasLocation_ = true;

                Serial.printf("[xypher] Location detected: %s, %s, %s (Offset: %ld seconds)\n",
                              city_.c_str(), region_.c_str(), country_.c_str(), offset);

                configTime(offset, 0, "pool.ntp.org", "time.nist.gov");
            }
            else
            {
                Serial.println("[xypher] Failed to parse geolocation data");
            }
        }
        else
        {
            Serial.printf("[xypher] Geolocation HTTP request failed: %d\n", httpCode);
        }
        http.end();
    }
}
