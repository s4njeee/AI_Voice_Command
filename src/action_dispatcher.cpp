// [xypher] Action Dispatcher — executes real-world actions from voice commands.
// Handles two kinds of actions: GPIO (flip a pin) and Guardian Alert (POST request to the mobile app).

#include "action_dispatcher.h"
#include "config.h"
#include "wifi_manager.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

ActionDispatcher actionDispatcher;

// [xypher] Sets up all the controllable pins as outputs and turns them off.
// Called once during setup() so the pins are ready before any voice command.
void ActionDispatcher::begin()
{
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(MOTOR_PIN, OUTPUT);
    pinMode(AUX_LED_PIN, OUTPUT);

    digitalWrite(RELAY_PIN, RELAY_ACTIVE_LOW ? HIGH : LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(MOTOR_PIN, LOW);
    digitalWrite(AUX_LED_PIN, LOW);

    Serial.println("Action Dispatcher ready");
    Serial.printf("  RELAY=%d  BUZZER=%d  MOTOR=%d  LED=%d\n",
                  RELAY_PIN, BUZZER_PIN, MOTOR_PIN, AUX_LED_PIN);
}

// [xypher] Looks at the action type and sends it to the right handler.
// Returns true if the action was executed successfully.
bool ActionDispatcher::execute(const ActionResult &action)
{
    switch (action.type)
    {
    case ACTION_GPIO:
        return executeGPIO(action);
    case ACTION_ALERT_GUARDIAN:
        return executeAlertGuardian(action);
    default:
        Serial.println("ActionDispatcher: nothing to execute");
        return false;
    }
}

// [xypher] Converts a device name (like "light" or "buzzer") to the
// matching GPIO pin number defined in config.h.
int ActionDispatcher::resolvePin(const String &device)
{
    String d = device;
    d.toLowerCase();

    if (d == "relay" || d == "light" || d == "lamp" || d == "fan")
    {
        return RELAY_PIN;
    }
    if (d == "buzzer" || d == "alarm" || d == "beep")
    {
        return BUZZER_PIN;
    }
    if (d == "motor" || d == "vibrate")
    {
        return MOTOR_PIN;
    }
    if (d == "led" || d == "indicator")
    {
        return AUX_LED_PIN;
    }

    Serial.printf("Unknown device: %s\n", device.c_str());
    return -1;
}

// [xypher] Turns a GPIO pin on, off, or toggles it based on the AI's decision.
// Handles active-low relay modules (where LOW = ON) automatically.
bool ActionDispatcher::executeGPIO(const ActionResult &action)
{
    const int pin = resolvePin(action.device);
    if (pin < 0)
    {
        return false;
    }

    String s = action.state;
    s.toLowerCase();

    bool targetHigh;

    if (s == "on")
    {
        targetHigh = true;
    }
    else if (s == "off")
    {
        targetHigh = false;
    }
    else if (s == "toggle")
    {
        targetHigh = (digitalRead(pin) == LOW);
    }
    else
    {
        Serial.printf("Unknown state: %s\n", action.state.c_str());
        return false;
    }

    // [xypher] Flip the signal for active-low relays (LOW = ON)
    if (pin == RELAY_PIN && RELAY_ACTIVE_LOW)
    {
        targetHigh = !targetHigh;
    }

    digitalWrite(pin, targetHigh ? HIGH : LOW);
    Serial.printf("GPIO %s (pin %d) -> %s\n",
                  action.device.c_str(), pin,
                  targetHigh ? "HIGH" : "LOW");
    return true;
}

// [xypher] Sends an SOS emergency alert to the guardian's mobile app via HTTP POST.
// The request payload is sent as JSON, including the alert message, coordinates/location
// of the blind user (if geolocation was retrieved), and current timestamp.
bool ActionDispatcher::executeAlertGuardian(const ActionResult &action)
{
    // Build target URL from config constants
    String url = String("http://") + GUARDIAN_APP_IP + ":" +
                 String(GUARDIAN_APP_PORT) + GUARDIAN_APP_PATH;

    Serial.printf("[xypher] Sending Alert to Guardian App -> %s\n", url.c_str());

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(url))
    {
        Serial.println("[xypher] HTTP connection to guardian app failed");
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // [xypher] Create the structured alert JSON payload
    JsonDocument doc;
    doc["event"] = "SOS_ALERT";
    doc["notificationType"] = action.alertType.length() > 0 ? action.alertType : "app";
    doc["alertMessage"] = action.alertMsg.length() > 0 ? action.alertMsg : "User requested emergency help!";
    
    // Include IP geolocation if available
    if (wifiManager.hasLocation())
    {
        JsonObject loc = doc["location"].to<JsonObject>();
        loc["city"] = wifiManager.getCity();
        loc["region"] = wifiManager.getRegion();
        loc["country"] = wifiManager.getCountry();
    }
    else
    {
        doc["location"] = "Unknown Location";
    }

    // Include local timestamp if synced
    time_t now = time(nullptr);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        doc["timestamp"] = timeStr;
    }

    String body;
    serializeJson(doc, body);

    int httpCode = http.POST(body);
    if (httpCode > 0)
    {
        Serial.printf("[xypher] Guardian App response: %d\n", httpCode);
        if (httpCode == 200)
        {
            String response = http.getString();
            if (response.length() > 0 && response.length() < 200)
            {
                Serial.printf("[xypher] Guardian App body: %s\n", response.c_str());
            }
        }
    }
    else
    {
        Serial.printf("[xypher] Alert delivery failed error: %s\n",
                      http.errorToString(httpCode).c_str());
    }

    http.end();
    return httpCode >= 200 && httpCode < 300;
}

// [xypher] Performs fast local keyword matching on command inputs.
// If the user says partial/short words (e.g. "light", "alarm", "time", "where", "help"),
// this function maps it directly to the corresponding action without calling the AI.
bool ActionDispatcher::localMatch(const String &command, ActionResult &result)
{
    String cmd = command;
    cmd.toLowerCase();
    cmd.trim();

    // Check for Help / SOS / Emergency queries to notify the guardian
    if (cmd.indexOf("help") >= 0 || cmd.indexOf("emergency") >= 0 || cmd.indexOf("sos") >= 0 || 
        cmd.indexOf("guardian") >= 0 || cmd.indexOf("text") >= 0 || cmd.indexOf("sms") >= 0 ||
        cmd.indexOf("message") >= 0)
    {
        result.type = ACTION_ALERT_GUARDIAN;
        result.alertMsg = "User requested emergency assistance via voice command!";
        
        // [xypher] Check if the user specifically asked for an SMS alert or text message
        if (cmd.indexOf("sms") >= 0 || cmd.indexOf("text") >= 0 || cmd.indexOf("message") >= 0)
        {
            result.alertType = "sms";
            result.feedback = "Sending SMS text alert to your guardian.";
        }
        else
        {
            result.alertType = "app";
            result.feedback = "Alerting your guardian on the app. Help is on the way.";
        }
        return true;
    }

    if (cmd.indexOf("time") >= 0 || cmd.indexOf("clock") >= 0 || cmd.indexOf("hour") >= 0 ||
        cmd.indexOf("date") >= 0 || cmd.indexOf("day") >= 0 || cmd.indexOf("today") >= 0 || cmd.indexOf("calendar") >= 0)
    {
        result.type = ACTION_TIME;
        result.feedback = ""; 
        return true;
    }

    if (cmd.indexOf("where") >= 0 || cmd.indexOf("location") >= 0 || cmd.indexOf("gps") >= 0 || cmd.indexOf("city") >= 0 ||
        cmd.indexOf("region") >= 0 || cmd.indexOf("country") >= 0 || cmd.indexOf("state") >= 0 || cmd.indexOf("address") >= 0)
    {
        result.type = ACTION_LOCATION;
        result.feedback = ""; 
        return true;
    }

    // Helper function to extract state keyword
    auto detectState = [](const String &c) -> String {
        if (c.indexOf("off") >= 0 || c.indexOf("stop") >= 0 || c.indexOf("deactivate") >= 0) {
            return "off";
        }
        if (c.indexOf("on") >= 0 || c.indexOf("start") >= 0 || c.indexOf("activate") >= 0) {
            return "on";
        }
        return "toggle";
    };

    // Check for Relay/Light commands
    if (cmd.indexOf("light") >= 0 || cmd.indexOf("lamp") >= 0 || cmd.indexOf("fan") >= 0 || cmd.indexOf("relay") >= 0)
    {
        result.type = ACTION_GPIO;
        result.device = "relay";
        result.state = detectState(cmd);
        result.feedback = (result.state == "on") ? "Light turned on." : 
                          ((result.state == "off") ? "Light turned off." : "Toggling the light.");
        return true;
    }

    // Check for Buzzer/Alarm commands
    if (cmd.indexOf("buzzer") >= 0 || cmd.indexOf("alarm") >= 0 || cmd.indexOf("beep") >= 0)
    {
        result.type = ACTION_GPIO;
        result.device = "buzzer";
        result.state = detectState(cmd);
        if (result.state == "toggle") result.state = "on"; // Default buzzer to ON
        result.feedback = (result.state == "on") ? "Alarm activated." : "Alarm off.";
        return true;
    }

    // Check for Motor/Vibration commands
    if (cmd.indexOf("motor") >= 0 || cmd.indexOf("vibrate") >= 0 || cmd.indexOf("shake") >= 0)
    {
        result.type = ACTION_GPIO;
        result.device = "motor";
        result.state = detectState(cmd);
        if (result.state == "toggle") result.state = "on"; // Default motor to ON
        result.feedback = (result.state == "on") ? "Vibration started." : "Vibration stopped.";
        return true;
    }

    // Check for LED commands
    if (cmd.indexOf("led") >= 0 || cmd.indexOf("indicator") >= 0)
    {
        result.type = ACTION_GPIO;
        result.device = "led";
        result.state = detectState(cmd);
        result.feedback = (result.state == "on") ? "LED on." : 
                          ((result.state == "off") ? "LED off." : "LED toggled.");
        return true;
    }

    return false;
}
