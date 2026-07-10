#include "wifi_manager.h"
#include "config.h"
#include "secrets.h"

#include <esp_wifi.h>

WiFiManagerESP wifiManager;

static const char *wifiStatusText(wl_status_t status)
{
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

void WiFiManagerESP::printStatus()
{
    Serial.print("WiFi status: ");
    Serial.print(wifiStatusText(WiFi.status()));
    Serial.print("  RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
}

void WiFiManagerESP::connectBlocking(uint32_t timeoutMs)
{
    // Soft reconnect first — do NOT erase stored credentials
    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.disconnect(false, false);
        delay(100);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");

        if (millis() - start > timeoutMs)
        {
            Serial.println();
            Serial.println("WiFi connect timeout.");
            printStatus();
            return;
        }
    }

    Serial.println();
    wasConnected_ = true;
    disconnectSince_ = 0;
}

void WiFiManagerESP::begin()
{
    Serial.println();
    Serial.println("================================");
    Serial.println("Connecting to WiFi...");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);
    Serial.println("================================");

    // ESP32 only uses 2.4 GHz. Modem sleep often causes false disconnects
    // while I2S mic/speaker are running — disable it.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname("smartcane");

    // Clear only the current session, keep it gentle
    WiFi.disconnect(false, false);
    delay(200);

    // Prefer reliable public DNS while still using DHCP for IP
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE,
                IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    connectBlocking(WIFI_TIMEOUT);

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("================================");
        Serial.println("WiFi connection FAILED");
        Serial.println("Check:");
        Serial.println(" 1. SSID/password in secrets.h");
        Serial.println(" 2. Router is 2.4 GHz (ESP32 cannot use 5 GHz)");
        Serial.println(" 3. ESP32 is close to the router");
        Serial.println("================================");
        return;
    }

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
    Serial.println();
    Serial.println("Testing DNS...");

    if (WiFi.hostByName("google.com", ip))
    {
        Serial.print("google.com -> ");
        Serial.println(ip);
    }
    else
    {
        Serial.println("DNS FAILED for google.com");
    }

    if (WiFi.hostByName("api.openai.com", ip))
    {
        Serial.print("api.openai.com -> ");
        Serial.println(ip);
    }
    else
    {
        Serial.println("DNS FAILED for api.openai.com");
    }

    Serial.println("================================");
}

void WiFiManagerESP::loop()
{
    const wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED)
    {
        if (!wasConnected_)
        {
            Serial.println("WiFi stable again.");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            printStatus();
        }
        wasConnected_ = true;
        disconnectSince_ = 0;
        return;
    }

    // Debounce: ignore brief status blips (very common on ESP32)
    if (disconnectSince_ == 0)
    {
        disconnectSince_ = millis();
        Serial.print("WiFi blip (");
        Serial.print(wifiStatusText(status));
        Serial.println(") — waiting before reconnect...");
        return;
    }

    // Must stay down for 3 seconds before we treat it as real
    if (millis() - disconnectSince_ < 3000)
    {
        return;
    }

    // Don't hammer the AP
    if (millis() - lastAttempt_ < 10000)
    {
        return;
    }

    lastAttempt_ = millis();
    wasConnected_ = false;

    Serial.println();
    Serial.println("WiFi still down — reconnecting...");
    printStatus();

    // Let the stack auto-reconnect first
    WiFi.reconnect();

    const unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000)
    {
        delay(200);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        wasConnected_ = true;
        disconnectSince_ = 0;
        Serial.println("WiFi Reconnected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        return;
    }

    // Fallback: gentle begin() without wiping NVS
    Serial.println("Auto-reconnect failed, retrying begin()...");
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long start2 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start2 < 10000)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        wasConnected_ = true;
        disconnectSince_ = 0;
        Serial.println("WiFi Reconnected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("Reconnect failed — will try again later.");
        printStatus();
    }
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
