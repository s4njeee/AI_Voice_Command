#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "secrets.h"
#include "wifi_manager.h"
#include "microphone.h"
#include "speaker.h"
#include "groq_client.h"
#include "action_dispatcher.h"  // [xypher] IoT action execution from voice commands
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

// Process voice command (local execution or fallback to AI)
static bool handleCommand(const String &command)
{
    Serial.print("Command: ");
    Serial.println(command);

    // Try local keyword match first
    ActionResult action;
    bool matched = actionDispatcher.localMatch(command, action);
    if (matched)
    {
        Serial.println("Local keyword action matched!");
    }
    else
    {
        Serial.println("No local match. Falling back to AI classification...");
        action = groq.classifyAction(command);
    }

    // Execute device action, status check, or alert locally
    if (action.type == ACTION_GPIO || action.type == ACTION_ALERT_GUARDIAN ||
        action.type == ACTION_TIME || action.type == ACTION_LOCATION)
    {
        String feedback = "";
        bool ok = true;

        if (action.type == ACTION_GPIO || action.type == ACTION_ALERT_GUARDIAN)
        {
            ok = actionDispatcher.execute(action);

            feedback = ok
                ? (action.feedback.length() > 0 ? action.feedback : "Done.")
                : "Sorry, that action failed.";
        }
        else if (action.type == ACTION_TIME)
        {
            time_t now = time(nullptr);
            struct tm timeinfo;
            if (now > 1700000000 && getLocalTime(&timeinfo))
            {
                char timeStr[64];
                char dateStr[64];
                strftime(timeStr, sizeof(timeStr), "%I:%M %p", &timeinfo);
                strftime(dateStr, sizeof(dateStr), "%A, %B %d, %Y", &timeinfo);

                String cmd = command;
                cmd.toLowerCase();

                if (cmd.indexOf("date") >= 0 || cmd.indexOf("day") >= 0 || cmd.indexOf("today") >= 0 || cmd.indexOf("calendar") >= 0)
                {
                    if (cmd.indexOf("time") >= 0 || cmd.indexOf("clock") >= 0 || cmd.indexOf("hour") >= 0)
                    {
                        feedback = String("The current time is ") + timeStr + ", and today is " + dateStr + ".";
                    }
                    else
                    {
                        feedback = String("Today is ") + dateStr + ".";
                    }
                }
                else
                {
                    feedback = String("The current time is ") + timeStr + ".";
                }
            }
            else
            {
                feedback = "Time is not synchronized yet.";
                ok = false;
            }
        }
        else if (action.type == ACTION_LOCATION)
        {
            if (wifiManager.hasLocation())
            {
                String cmd = command;
                cmd.toLowerCase();

                if (cmd.indexOf("country") >= 0)
                {
                    feedback = String("You are in ") + wifiManager.getCountry() + ".";
                }
                else if (cmd.indexOf("region") >= 0 || cmd.indexOf("state") >= 0)
                {
                    feedback = String("You are in ") + wifiManager.getRegion() + ".";
                }
                else if (cmd.indexOf("city") >= 0)
                {
                    feedback = String("You are in the city of ") + wifiManager.getCity() + ".";
                }
                else
                {
                    feedback = String("You are in ") + wifiManager.getCity() + ", " + wifiManager.getRegion() + ", " + wifiManager.getCountry() + ".";
                }
            }
            else
            {
                feedback = "I cannot determine your location yet.";
                ok = false;
            }
        }

        Serial.print("Action feedback: ");
        Serial.println(feedback);

        if (groq.speak(feedback))
        {
            return true;
        }

        return speaker.speakText(feedback);
    }

    // [xypher] Not a device command — treat it as a question and chat
    String reply = groq.chat(command);
    if (reply.length() == 0)
    {
        Serial.println("Chat response failed.");
        return false;
    }

    Serial.print("Smartcane: ");
    Serial.println(reply);

    if (groq.speak(reply))
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

    // [xypher] Initialize time zone offset right away to default offline fallback
    configTime(LOCAL_UTC_OFFSET, 0, "pool.ntp.org", "time.nist.gov");

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
    actionDispatcher.begin();  // [xypher] Set up IoT pins so voice commands can control them

    // Hardware check: bit-bang first (proves amp wiring), then I2S tones
    Serial.println("Speaker hardware test...");
    Serial.println("1) BITBANG — if silent, amp/power/wiring is wrong");
    speaker.playBitBangTone(1000, 400);
    delay(150);
    Serial.println("2) I2S — if bitbang worked but this is silent, tell us");
    speaker.playTone(880, 350);
    delay(100);
    speaker.playTone(1175, 400);

    if (wifiManager.connected())
    {
        setBusy(true);
        Serial.println("Preparing greeting audio...");
        if (!groq.ensurePromptAudio(PROMPT_FILE))
        {
            Serial.println("Orpheus prompt unavailable — online TTS fallback.");
            speaker.speakText(PROMPT_TEXT_PLAIN);
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
            Serial.println("Paused briefly: Groq STT/chat rate limit (TTS still OK via Google).");
        }
        delay(500);
        return;
    }
    else
    {
        groq.clearQuotaBlock();
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
        // Prefer cached /prompt.wav, else Orpheus, else Google TTS
        if (!speaker.playWavFile(PROMPT_FILE))
        {
            if (!groq.speakToFile(PROMPT_TEXT, PROMPT_FILE))
            {
                speaker.speakText(PROMPT_TEXT_PLAIN);
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
