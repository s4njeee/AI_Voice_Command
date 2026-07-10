#ifndef CONFIG_H
#define CONFIG_H

//======================================================
// Project Information
//======================================================

#define PROJECT_NAME    "Smartcane AI Voice"
#define PROJECT_VERSION "2.0.0"

//======================================================
// Wake Word
//======================================================

#define WAKE_WORD "smartcane"

//======================================================
// WiFi
//======================================================

#define WIFI_TIMEOUT 20000

//======================================================
// OpenAI
//======================================================

#define OPENAI_HOST "api.openai.com"
#define OPENAI_PORT 443

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
#define VAD_THRESHOLD      600

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
