#ifndef SECRETS_H
#define SECRETS_H

//======================================================
// Wi-Fi Credentials
//======================================================

#define WIFI_SSID      "ALAKDAN 23"
#define WIFI_PASSWORD  "VENCE12345678"

//======================================================
// OpenAI API Key
//======================================================
// Get your API key from:
// https://platform.openai.com/api-keys

#define OPENAI_API_KEY "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"

//======================================================
// OpenAI Models
//======================================================

// Speech-to-Text (whisper-1 is fast and reliable for wake/commands)
#define STT_MODEL "whisper-1"

// Chat (short answers, temperature 0 in code)
#define CHAT_MODEL "gpt-4o-mini"

// Text-to-Speech (tts-1 is faster than gpt-4o-mini-tts)
#define TTS_MODEL "tts-1"

// Voice
#define TTS_VOICE "alloy"

#endif