// [xypher] Action Dispatcher — lets the cane execute real-world actions
// (turn on lights, sound buzzers, send HTTP requests) from voice commands.

#ifndef ACTION_DISPATCHER_H
#define ACTION_DISPATCHER_H

#include <Arduino.h>

// [xypher] The three things the AI can decide a voice command means:
//   gpio  = control a physical pin (relay, buzzer, motor, LED)
//   alert = send an emergency alert notification to the guardian's mobile app
//   time  = tell the user the current local time
//   location = tell the user their current location
//   query = not a device command, just a question — pass to chat AI
enum ActionType
{
    ACTION_NONE,
    ACTION_GPIO,
    ACTION_ALERT_GUARDIAN,
    ACTION_TIME,
    ACTION_LOCATION,
    ACTION_QUERY
};

// [xypher] Holds everything needed to execute one action.
// The AI or local matcher fills this in, then the dispatcher reads it and acts.
struct ActionResult
{
    ActionType type;
    String device;     // Which device: "relay", "buzzer", "motor", "led"
    String state;      // What to do: "on", "off", "toggle"
    String alertMsg;   // [xypher] Help message to send to the guardian's app
    String alertType;  // [xypher] Type of alert: "app" or "sms"
    String feedback;   // What the cane says back, e.g. "Light turned on"
};

// [xypher] The dispatcher that actually flips pins or alerts the guardian's app.
class ActionDispatcher
{
public:
    void begin();
    bool execute(const ActionResult &action);

    // [xypher] Fast local keyword matching for short/partial commands
    bool localMatch(const String &command, ActionResult &result);

private:
    bool executeGPIO(const ActionResult &action);
    bool executeAlertGuardian(const ActionResult &action);
    int resolvePin(const String &device);
};

extern ActionDispatcher actionDispatcher;

#endif
