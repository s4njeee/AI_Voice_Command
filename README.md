# Smartcane AI Voice

Hands-free ESP32-S3 voice assistant. Say **Smartcane** — it asks what you need, then answers with OpenAI speech + chat.

## How to run (step by step)

### 1. Hardware
Wire an **ESP32-S3 DevKitC-1** (16MB + PSRAM):

| Module | Pin |
|--------|-----|
| INMP441 WS | GPIO 4 |
| INMP441 SCK | GPIO 5 |
| INMP441 SD | GPIO 6 |
| INMP441 L/R | GND (left channel) |
| INMP441 VDD | 3.3V |
| INMP441 GND | GND |
| MAX98357A BCLK | GPIO 48 |
| MAX98357A LRC | GPIO 47 |
| MAX98357A DIN | GPIO 45 |
| MAX98357A VIN | 5V |
| MAX98357A GND | GND |
| Status LED (optional) | GPIO 2 |

### 2. Secrets
1. Copy `include/secrets.example.h` → `include/secrets.h`
2. Set your Wi-Fi SSID and password
3. Set your OpenAI API key from https://platform.openai.com/api-keys  
   (`secrets.h` is gitignored — never commit it)

### 3. Install PlatformIO
- Install **VS Code** or **Cursor**
- Install the **PlatformIO IDE** extension
- Open this folder as the project root

### 4. Build and upload
1. Connect the ESP32-S3 by USB
2. In PlatformIO: **Build** then **Upload**
3. Open **Monitor** at **115200** baud

Or in a terminal:

```bash
pio run -t upload
pio device monitor
```

### 5. Use it
1. Wait until the serial log shows `System Ready` and `Say "Smartcane" to begin.`
2. Say **Smartcane** clearly
3. It plays: *“Hi, I am Smartcane. What do you need?”*
4. Speak your request
5. Smartcane replies by voice

You can also say the request in one breath, e.g. **“Smartcane what time is it”** — it skips the prompt and answers directly.

No BOOT button is required.

## Notes on accuracy and speed
- Wake detection uses on-device voice activity + OpenAI Whisper, with matching for common mis-hearings of “Smartcane”
- Speech recognition is never literally 100% accurate; quiet rooms and clear speech work best
- If it false-triggers, raise `VAD_THRESHOLD` in `include/config.h` (default `600`)
- If it misses your voice, lower `VAD_THRESHOLD`
- Greeting audio is cached on first boot so the “what do you need?” prompt is fast

## Auto-push to GitHub
After you change code, from this folder:

```bash
git add -A
git commit -m "Describe your change"
git push
```

A `post-commit` hook can push automatically after each commit (see `.githooks/post-commit`).
