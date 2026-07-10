#ifndef CONFIG_H
#define CONFIG_H

//======================================================
// Project Information
//======================================================

#define PROJECT_NAME    "Smartcane AI Voice (Groq)"
#define PROJECT_VERSION "3.6.1-GROQ"

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

// Locked Groq model IDs — Orpheus playground:
// https://console.groq.com/playground?model=canopylabs/orpheus-v1-english
#define GROQ_STT_MODEL  "whisper-large-v3-turbo"
#define GROQ_CHAT_MODEL "llama-3.1-8b-instant"
#define GROQ_TTS_MODEL  "canopylabs/orpheus-v1-english"

// Orpheus English voices: autumn, diana, hannah, austin, daniel, troy
// Change this to the voice you selected in the Groq playground.
#define GROQ_TTS_VOICE  "troy"

// Orpheus input limit is 200 characters
#define GROQ_TTS_MAX_CHARS 200

//======================================================
// Audio Configuration
//======================================================

#define SAMPLE_RATE        16000
#define SAMPLE_BITS        16
#define CHANNELS           1

// Short clip used to detect the wake word
#define WAKE_SECONDS       3

// Max length of a user command after wake
#define COMMAND_SECONDS    6

// Stop command recording after this much silence (ms)
#define SILENCE_END_MS     1000

// Voice activity threshold (raise if false triggers from TV/YouTube)
#define VAD_THRESHOLD      2500

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
// MAX98357A Speaker
//======================================================
// Uses I2S0 for TX (mic is stopped while speaking).
// Wiring: BCLK->48, LRC->47, DIN->45, VIN->5V, GND->GND
// SD on amp: FLOATING (not GND). Speaker only on amp + and -.

#define SPK_I2S_PORT       I2S_NUM_0

#define SPK_BCLK           48
#define SPK_LRC            47
#define SPK_DIN            45

//======================================================
// Status LED
//======================================================

#define LED_PIN            2

//======================================================
// WAV files on LittleFS (created automatically by firmware)
//======================================================
// RECORD_FILE  = mic recording sent to Groq Whisper (STT)
// REPLY_FILE   = Orpheus TTS of the AI answer (played on speaker)
// PROMPT_FILE  = Orpheus TTS greeting "what do you need?" (played on wake)
// You do NOT upload these files — Groq generates prompt/reply as .wav

#define RECORD_FILE  "/record.wav"
#define REPLY_FILE   "/reply.wav"
#define PROMPT_FILE  "/prompt.wav"

// Orpheus-style prompt with vocal direction (keep short — Orpheus TPD is tiny)
#define PROMPT_TEXT \
    "[cheerful] Hi, I am Smartcane. What do you need?"

// Plain text for Google TTS (no Orpheus tags)
#define PROMPT_TEXT_PLAIN \
    "Hi, I am Smartcane. What do you need?"

//======================================================
// HTTPS
//======================================================

#define HTTPS_TIMEOUT 25000

//======================================================
// Debug
//======================================================

#define ENABLE_SERIAL_DEBUG true

#endif
