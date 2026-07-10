#include <Arduino.h>

#include "config.h"
#include "secrets.h"
#include "wifi_manager.h"
#include "microphone.h"
#include "speaker.h"
#include "openai.h"
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
            // drop separators so "smart-cane" -> "smartcane" after space collapse
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

    // Common STT variants of "Smartcane"
    const char *variants[] = {
        "smartcane",
        "smartcan",
        "smartkane",
        "smartgain",
        "smartgame",
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

    // "smart cane" with space kept as smartcane after normalizeText space strip
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

    String reply = openai.chat(command);
    if (reply.length() == 0)
    {
        Serial.println("Chat response failed.");
        return false;
    }

    Serial.print("Smartcane: ");
    Serial.println(reply);

    if (!openai.textToSpeech(reply, REPLY_FILE))
    {
        Serial.println("Text-to-speech failed.");
        return false;
    }

    return speaker.playWavFile(REPLY_FILE);
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("====================================");
    Serial.println(PROJECT_NAME);
    Serial.println(PROJECT_VERSION);
    Serial.println("Wake word: Smartcane");
    Serial.println("====================================");

    pinMode(LED_PIN, OUTPUT);
    setBusy(false);

    wifiManager.begin();

    if (!wifiManager.connected())
    {
        Serial.println("WiFi required for OpenAI. Check secrets.h and reboot.");
    }

    microphone.begin();
    speaker.begin();
    wav.begin();
    openai.begin();

    if (wifiManager.connected())
    {
        setBusy(true);
        openai.ensurePromptAudio(PROMPT_FILE);
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

    // Always listening for speech energy — no BOOT button needed
    if (!microphone.waitForSpeech(0))
    {
        return;
    }

    Serial.println("Speech detected — checking for Smartcane...");
    setBusy(true);

    if (!recordSeconds(WAKE_SECONDS, false))
    {
        setBusy(false);
        delay(200);
        return;
    }

    String heard = openai.speechToText(RECORD_FILE);
    Serial.print("Heard: ");
    Serial.println(heard.length() ? heard : "(empty)");

    if (!containsWakeWord(heard))
    {
        Serial.println("Wake word not detected.");
        setBusy(false);
        delay(150);
        return;
    }

    Serial.println("Wake word detected!");

    String command = stripWakeWord(heard);

    // If user only said the name, ask what they want (cached audio = fast)
    if (command.length() < 2)
    {
        Serial.println("Asking what the user wants...");
        speaker.playWavFile(PROMPT_FILE);

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

        command = openai.speechToText(RECORD_FILE);
        if (command.length() == 0)
        {
            Serial.println("Could not understand command.");
            setBusy(false);
            return;
        }
    }

    handleCommand(command);
    setBusy(false);

    // Brief cooldown so playback echo does not re-trigger wake
    delay(400);
}
