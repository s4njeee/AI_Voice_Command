#ifndef CONFIG_H
#define CONFIG_H

//======================================================
// Project Information
//======================================================

#define PROJECT_NAME    "Smartcane AI Voice (Groq)"
#define PROJECT_VERSION "3.1.0-GROQ"

//======================================================
// Wake Word
//======================================================

#define WAKE_WORD "smartcane"

//======================================================
// WiFi
//======================================================

#define WIFI_TIMEOUT 45000

//======================================================
// AI provider (Groq — free tier, OpenAI-compatible)
//======================================================

#define AI_HOST "api.groq.com"
#define AI_PORT 443
#define AI_CHAT_URL "https://api.groq.com/openai/v1/chat/completions"
#define AI_TTS_URL  "https://api.groq.com/openai/v1/audio/speech"

// Locked Groq model IDs (ignore any OpenAI names left in secrets.h)
#define GROQ_STT_MODEL  "whisper-large-v3-turbo"
#define GROQ_CHAT_MODEL "llama-3.1-8b-instant"
#define GROQ_TTS_MODEL  "canopylabs/orpheus-v1-english"
#define GROQ_TTS_VOICE  "austin"

//======================================================
// Audio Configuration
//======================================================

#define SAMPLE_RATE        16000
#define SAMPLE_BITS        16
#define CHANNELS           1

// Short clip used to detect the wake word
#define WAKE_SECONDS       2

// Max length of a user command after wake
#define COMMAND_SECONDS    4

// Stop command recording after this much silence (ms)
#define SILENCE_END_MS     800

// Voice activity threshold (raise if false triggers, lower if misses speech)
#define VAD_THRESHOLD      1800

// After a failed Groq call, wait before trying STT again (saves quota)
#define API_ERROR_COOLDOWN_MS 15000

#define AUDIO_BUFFER_SIZE  1024

//======================================================
// INMP441 Microphone (I2S0)
//======================================================

#define MIC_I2S_PORT       I2S_NUM_0

#define MIC_WS             4
#define MIC_SCK            5
#define MIC_SD             6

//======================================================
// MAX98357A Speaker (I2S1)
//======================================================

#define SPK_I2S_PORT       I2S_NUM_1

#define SPK_BCLK           48
#define SPK_LRC            47
#define SPK_DIN            45

//======================================================
// Status LED
//======================================================

#define LED_PIN            2

//======================================================
// File Names
//======================================================

#define RECORD_FILE  "/record.wav"
#define REPLY_FILE   "/reply.wav"
#define PROMPT_FILE  "/prompt.wav"

#define PROMPT_TEXT  "Hi, I am Smartcane. What do you need?"

//======================================================
// HTTPS
//======================================================

#define HTTPS_TIMEOUT 25000

//======================================================
// Debug
//======================================================

#define ENABLE_SERIAL_DEBUG true

#endif
