#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Free key: https://console.groq.com/keys
#define GROQ_API_KEY "gsk_xxxxxxxx"

#ifndef AI_API_KEY
#define AI_API_KEY GROQ_API_KEY
#endif

#endif
