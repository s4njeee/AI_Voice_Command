# Xypher Codebase Updates — Voice-Activated Smartcane Assistant

All changes and additions introduced by **xypher** to the Smartcane firmware.

---

## Summary of Major Features Added

### 1. Fast Local Keyword Matcher (0ms Offline Latency)

- Added a parser in `ActionDispatcher::localMatch()` that runs locally on the ESP32.
- If the user speaks short or partial command words (e.g. _"light"_, _"alarm"_, _"vibrate"_, _"time"_, _"where"_, _"help"_, or _"text"_), the system resolves the request in **0 milliseconds** locally without calling any cloud APIs.
- Saves API quota, improves battery efficiency, and works offline for physical GPIO control and local status feedback.

### 2. Time & Geolocation Voice Commands (Hybrid Local/Online)

- **Local Fallback (Offline Mode):** Configured default offline variables (`LOCAL_CITY`, `LOCAL_REGION`, `LOCAL_COUNTRY`, `LOCAL_UTC_OFFSET`) inside `config.h`. On boot, the cane initializes these variables locally, meaning the user always has a valid location and timezone offset immediately offline without requiring the internet.
- **NTP & Geolocation Sync (Online Mode):** If Wi-Fi is connected and internet is available, the cane queries `ip-api.com/json` to dynamically update the location telemetry and applies the true local UTC offset to the system clock via NTP (`pool.ntp.org`).
- **Instant Voice Queries:** The user can ask:
  - _"What time is it?"_ -> Cane replies locally: _"The current time is 09:45 AM."_ (using NTP synced clock or local fallback offset).
  - _"Where am I?"_ -> Cane replies locally: _"You are in Manila, Metro Manila."_ (using dynamically fetched city/region or local fallback defaults).

### 3. Guardian Mobile App SOS Alerts (Dual-Channel)

- Integrates the cane directly with a companion mobile app for the blind user's guardian (replacing generic network client variables).
- Sends emergency SOS alerts via HTTP POST to the guardian's app endpoint.
- Supports **two notification channels** based on the user's voice command:
  - **App Alert:** (e.g. _"help"_, _"emergency"_) -> Sends `notificationType: "app"`. Speaks confirmation: _"Alerting your guardian on the app."_
  - **SMS Alert:** (e.g. _"text my helper"_, _"send sms"_) -> Sends `notificationType: "sms"`. Speaks confirmation: _"Sending SMS text alert to your guardian."_
- Every SOS notification automatically attaches telemetry:
  - Alert trigger message
  - User's current location (city, region, country from geolocation cache)
  - Precise timestamp

### 4. Local Peripheral GPIO Control

- Integrated physical outputs into the voice command flow:
  - **Relay Module (pin 38):** Controls lights, fans, etc. Supports active-low inversion.
  - **Piezo Buzzer (pin 39):** Sounds beeps/alarms for alerts.
  - **DC Vibration Motor (pin 40):** Active MOSFET drive for physical haptic feedback.
  - **Auxiliary LED (pin 41):** Extra indicator lights.

### 5. LLaMA Classifier Optimizations

- Implemented `classifyAction()` in the Groq chat client to parse voice commands into structured JSON objects.
- Updated the system instructions to automatically **infer logical default states** for ambiguous/short voice commands (e.g., mapping _"vibrate"_ to motor `on`, and _"light"_ to relay `toggle`).

---

## File Changes Trace

### 📂 Created Files

- **[action_dispatcher.h](file:///c:/Users/CLIET/AI_Voice_Command/include/action_dispatcher.h)** — Declares the `ActionType` enum, the `ActionResult` struct (with dynamic SMS/App alert properties), and the `ActionDispatcher` class.
- **[action_dispatcher.cpp](file:///c:/Users/CLIET/AI_Voice_Command/src/action_dispatcher.cpp)** — Implements the GPIO pin drivers, the `executeAlertGuardian` JSON payload POST client, and the local keyword parser.
- **[secrets.h](file:///c:/Users/CLIET/AI_Voice_Command/include/secrets.h)** _(Created from template)_ — Stores Wi-Fi credentials and the Groq API key securely.

### 📂 Modified Files

- **[config.h](file:///c:/Users/CLIET/AI_Voice_Command/include/config.h)** — Configured GPIO pin mappings, relay active polarity, and target configurations for the guardian mobile app (`GUARDIAN_APP_IP`, `GUARDIAN_APP_PORT`, `GUARDIAN_APP_PATH`).
- **[groq_client.h](file:///c:/Users/CLIET/AI_Voice_Command/include/groq_client.h)** / **[groq_client.cpp](file:///c:/Users/CLIET/AI_Voice_Command/src/groq_client.cpp)** — Implemented the `classifyAction()` method. Programmed LLaMA classification prompts for GPIO commands, time queries, location requests, and guardian notification type selection (SMS vs. App).
- **[wifi_manager.h](file:///c:/Users/CLIET/AI_Voice_Command/include/wifi_manager.h)** / **[wifi_manager.cpp](file:///c:/Users/CLIET/AI_Voice_Command/src/wifi_manager.cpp)** — Triggered `fetchLocationAndTime()` on association. Geolocation payload is parsed via `ArduinoJson` to initialize local RTC clock offsets.
- **[main.cpp](file:///c:/Users/CLIET/AI_Voice_Command/src/main.cpp)** — Refactored the transcription dispatcher `handleCommand()` to prioritize local keyword matching before falling back to LLaMA action classification. Included `<time.h>` and handled custom feedback generation for Time, Geolocation, and Guardian alert states.
