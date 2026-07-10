#include <Arduino.h>

#include "config.h"
#include "secrets.h"
#include "wifi_manager.h"
#include "microphone.h"
#include "speaker.h"
#include "openai.h"
#include "wav.h"

int16_t audioBuffer[512];

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("====================================");
    Serial.println(PROJECT_NAME);
    Serial.println(PROJECT_VERSION);
    Serial.println("====================================");

    wifiManager.begin();

    microphone.begin();

    speaker.begin();

    wav.begin();

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    openai.begin();

    Serial.println("System Ready");
    Serial.println("Press BOOT button to record.");
}

void loop()
{
    wifiManager.loop();

    if (digitalRead(BUTTON_PIN) == LOW)
    {
        Serial.println("Recording...");

        size_t samplesRead = 0;
        size_t totalSamples = SAMPLE_RATE * RECORD_SECONDS;
        size_t recordedSamples = 0;

        if (wav.create(RECORD_FILE))
        {
            while (recordedSamples < totalSamples)
            {
                if (!microphone.readSamples(audioBuffer,
                                            512,
                                            samplesRead))
                {
                    Serial.println("Failed to read microphone samples.");
                    break;
                }

                if (samplesRead == 0)
                {
                    Serial.println("No samples read from microphone.");
                    break;
                }

                wav.appendSamples(audioBuffer,
                                  samplesRead);
                recordedSamples += samplesRead;
            }

            wav.close();

            Serial.print("Recorded samples: ");
            Serial.println(recordedSamples);

            if (recordedSamples > 0)
            {
                Serial.println("Uploading to OpenAI...");

                String text = openai.speechToText(RECORD_FILE);

                if (text.length() == 0)
                {
                    Serial.println("Speech recognition failed.");
                }
                else
                {
                    Serial.print("You: ");
                    Serial.println(text);

                    String reply = openai.chat(text);

                    if (reply.length() == 0)
                    {
                        Serial.println("Chat response failed.");
                    }
                    else
                    {
                        Serial.print("AI: ");
                        Serial.println(reply);

                        if (!openai.textToSpeech(reply,
                                                 "/reply.wav"))
                        {
                            Serial.println("Text-to-speech failed.");
                        }
                        else
                        {
                            Serial.println("Reply audio saved to /reply.wav");
                        }
                    }
                }
            }
        }

        // speaker.playFile("/reply.mp3");

        while (digitalRead(BUTTON_PIN) == LOW)
        {
            delay(10);
        }
    }

    delay(10);
}