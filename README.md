# Smartcane AI Voice (Groq)

ESP32-S3 voice assistant using **Groq only** (not OpenAI).

## Setup
1. Get a free key: https://console.groq.com/keys (`gsk_...`)
2. Edit `include/secrets.h`:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `GROQ_API_KEY`
3. PlatformIO: **Build** then **Upload**
4. Serial must show:
   ```
   Smartcane AI Voice (Groq)
   3.0.0-GROQ
   Provider: Groq (api.groq.com)
   Groq AI client ready (api.groq.com)
   ```
   If you still see `OpenAI` or `api.openai.com`, you uploaded the wrong/old project.

## Models
- STT: `whisper-large-v3-turbo`
- Chat: `llama-3.1-8b-instant`
- TTS: `canopylabs/orpheus-v1-english`
