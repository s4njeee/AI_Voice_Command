#include "wifi_manager.h"
#include "config.h"
#include "secrets.h"

WiFiManagerESP wifiManager;

void WiFiManagerESP::begin()
{
    Serial.println();
    Serial.println("================================");
    Serial.println("Connecting to WiFi...");
    Serial.println("================================");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(1000);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - start > WIFI_TIMEOUT)
        {
            Serial.println();
            Serial.println("================================");
            Serial.println("WiFi connection TIMEOUT!");
            Serial.println("Check SSID and PASSWORD.");
            Serial.println("================================");
            return;
        }
    }

    Serial.println();
    Serial.println("================================");
    Serial.println("WiFi Connected!");
    Serial.println("================================");

    Serial.print("SSID       : ");
    Serial.println(WIFI_SSID);

    Serial.print("IP Address : ");
    Serial.println(WiFi.localIP());

    Serial.print("Gateway    : ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("Subnet     : ");
    Serial.println(WiFi.subnetMask());

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
    static unsigned long lastReconnect = 0;

    if (WiFi.status() == WL_CONNECTED)
        return;

    if (millis() - lastReconnect < 5000)
        return;

    lastReconnect = millis();

    Serial.println();
    Serial.println("WiFi disconnected!");
    Serial.println("Reconnecting...");

    WiFi.disconnect(true, true);
    delay(1000);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");

        if (millis() - start > 15000)
        {
            Serial.println();
            Serial.println("Reconnect failed.");
            return;
        }
    }

    Serial.println();
    Serial.println("WiFi Reconnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

bool WiFiManagerESP::connected()
{
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManagerESP::ipAddress()
{
    if (!connected())
        return "0.0.0.0";

    return WiFi.localIP().toString();
}