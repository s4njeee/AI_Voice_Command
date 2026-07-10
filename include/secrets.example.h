#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Free key from https://console.groq.com/keys
#define GROQ_API_KEY "gsk_xxxxxxxx"

#ifndef AI_API_KEY
#define AI_API_KEY GROQ_API_KEY
#endif

#define STT_MODEL  "whisper-large-v3-turbo"
#define CHAT_MODEL "llama-3.1-8b-instant"
#define TTS_MODEL  "canopylabs/orpheus-v1-english"
#define TTS_VOICE  "austin"

#endif
