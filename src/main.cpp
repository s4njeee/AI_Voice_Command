#include <Arduino.h>

#include "config.h"
#include "secrets.h"
#include "wifi_manager.h"
#include "microphone.h"
#include "speaker.h"
#include "groq_client.h"
#include "wav.h"

int16_t audioBuffer[512];

static void setBusy(bool busy)
{
    digitalWrite(LED_PIN, busy ? HIGH : LOW);
}

static String normalizeText(String text)
{
    text.toLowerCase();
    String out;
    out.reserve(text.length());

    for (size_t i = 0; i < text.length(); i++)
    {
        const char c = text[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ')
        {
            if (c == ' ' && out.endsWith(" "))
            {
                continue;
            }
            out += c;
        }
        else if (c == '-' || c == '_' || c == '\'')
        {
            // drop separators so "smart-cane" -> "smartcane"
        }
        else
        {
            out += ' ';
        }
    }

    out.trim();
    out.replace(" ", "");
    return out;
}

static bool containsWakeWord(const String &text)
{
    const String norm = normalizeText(text);

    const char *variants[] = {
        "smartcane",
        "smartcan",
        "smartkane",
        "smartcain",
        "smartgain",
        "smartgame",
        "mygame",
        "mattcain",
        "matcain",
        "marcane",
        "sparkcane",
        nullptr};

    for (int i = 0; variants[i] != nullptr; i++)
    {
        if (norm.indexOf(variants[i]) >= 0)
        {
            return true;
        }
    }

    return false;
}

static String stripWakeWord(String text)
{
    String lower = text;
    lower.toLowerCase();

    const char *phrases[] = {
        "hey smartcane",
        "ok smartcane",
        "okay smartcane",
        "hi smartcane",
        "hello smartcane",
        "smart cane",
        "smartcane",
        "smart-cane",
        nullptr};

    for (int i = 0; phrases[i] != nullptr; i++)
    {
        int idx = lower.indexOf(phrases[i]);
        if (idx >= 0)
        {
            text.remove(idx, strlen(phrases[i]));
            break;
        }
    }

    text.trim();
    while (text.startsWith(",") || text.startsWith(".") || text.startsWith("?"))
    {
        text.remove(0, 1);
        text.trim();
    }

    return text;
}

static bool recordSeconds(uint32_t seconds, bool stopOnSilence)
{
    const size_t totalSamples = SAMPLE_RATE * seconds;
    size_t recordedSamples = 0;
    size_t samplesRead = 0;
    bool heardSpeech = false;
    uint32_t silenceStarted = 0;

    if (!wav.create(RECORD_FILE))
    {
        Serial.println("Failed to create recording file.");
        return false;
    }

    while (recordedSamples < totalSamples)
    {
        if (!microphone.readSamples(audioBuffer, 512, samplesRead) || samplesRead == 0)
        {
            break;
        }

        wav.appendSamples(audioBuffer, samplesRead);
        recordedSamples += samplesRead;

        if (!stopOnSilence)
        {
            continue;
        }

        uint64_t sum = 0;
        for (size_t i = 0; i < samplesRead; i++)
        {
            int32_t sample = audioBuffer[i];
            if (sample < 0)
            {
                sample = -sample;
            }
            sum += (uint32_t)sample;
        }
        const uint32_t level = (uint32_t)(sum / samplesRead);

        if (level >= VAD_THRESHOLD)
        {
            heardSpeech = true;
            silenceStarted = 0;
        }
        else if (heardSpeech)
        {
            if (silenceStarted == 0)
            {
                silenceStarted = millis();
            }
            else if (millis() - silenceStarted >= SILENCE_END_MS)
            {
                break;
            }
        }
    }

    wav.close();
    Serial.printf("Recorded samples: %u\n", (unsigned)recordedSamples);
    return recordedSamples > 0;
}

static bool handleCommand(const String &command)
{
    Serial.print("Command: ");
    Serial.println(command);

    String reply = groq.chat(command);
    if (reply.length() == 0)
    {
        Serial.println("Chat response failed.");
        return false;
    }

    Serial.print("Smartcane: ");
    Serial.println(reply);

    // Orpheus -> /reply.wav -> speaker
    if (groq.speakToFile(reply, REPLY_FILE))
    {
        return true;
    }

    Serial.println("Orpheus failed — Google TTS fallback on speaker.");
    return speaker.speakText(reply);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("====================================");
    Serial.println(PROJECT_NAME);
    Serial.println(PROJECT_VERSION);
    Serial.println("Provider: Groq (api.groq.com)");
    Serial.println("Wake word: Smartcane");
    Serial.println("====================================");

    pinMode(LED_PIN, OUTPUT);
    setBusy(false);

    wifiManager.begin();

    if (!wifiManager.connected())
    {
        Serial.println("WiFi required for Groq. Check secrets.h and reboot.");
    }

    microphone.begin();
    speaker.begin();
    wav.begin();
    groq.begin();

    if (wifiManager.connected())
    {
        setBusy(true);
        Serial.println("Generating prompt.wav with Groq Orpheus...");
        if (!groq.ensurePromptAudio(PROMPT_FILE))
        {
            Serial.println("Orpheus prompt failed — Google TTS fallback.");
            speaker.speakText(PROMPT_TEXT);
        }
        setBusy(false);
    }

    Serial.println();
    Serial.println("System Ready");
    Serial.println("Say \"Smartcane\" to begin.");
}

void loop()
{
    wifiManager.loop();

    if (!wifiManager.connected())
    {
        delay(200);
        return;
    }

    if (groq.quotaBlocked())
    {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 20000)
        {
            lastWarn = millis();
            Serial.println("Paused: Groq rate limit. Wait, then reboot.");
        }
        delay(500);
        return;
    }

    if (!microphone.waitForSpeech(0))
    {
        return;
    }

    Serial.println("Speech detected — checking for Smartcane...");
    setBusy(true);

    if (!recordSeconds(WAKE_SECONDS, false))
    {
        setBusy(false);
        delay(API_ERROR_COOLDOWN_MS / 3);
        return;
    }

    String heard = groq.speechToText(RECORD_FILE);
    Serial.print("Heard: ");
    Serial.println(heard.length() ? heard : "(empty)");

    if (groq.quotaBlocked())
    {
        setBusy(false);
        return;
    }

    if (!containsWakeWord(heard))
    {
        Serial.println("Wake word not detected.");
        setBusy(false);
        delay(800);
        return;
    }

    Serial.println("Wake word detected!");

    String command = stripWakeWord(heard);

    if (command.length() < 2)
    {
        Serial.println("Asking what the user wants...");
        // Prefer cached /prompt.wav, else regenerate with Orpheus
        if (!speaker.playWavFile(PROMPT_FILE))
        {
            if (!groq.speakToFile(PROMPT_TEXT, PROMPT_FILE))
            {
                speaker.speakText(PROMPT_TEXT);
            }
        }

        if (!microphone.waitForSpeech(6000))
        {
            Serial.println("No follow-up command.");
            setBusy(false);
            return;
        }

        if (!recordSeconds(COMMAND_SECONDS, true))
        {
            setBusy(false);
            return;
        }

        command = groq.speechToText(RECORD_FILE);
        if (command.length() == 0)
        {
            Serial.println("Could not understand command.");
            setBusy(false);
            delay(API_ERROR_COOLDOWN_MS / 2);
            return;
        }
    }

    handleCommand(command);
    setBusy(false);
    delay(800);
}
