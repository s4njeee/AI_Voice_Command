#ifndef SPEAKER_H
#define SPEAKER_H

#include <Arduino.h>

class Speaker
{
public:
    bool begin();

    bool playPCM(const int16_t *buffer,
                 size_t samples);

    bool playWavFile(const char *filename);

    // Play a WAV sitting in RAM/PSRAM (from Groq Orpheus)
    bool playWavBuffer(const uint8_t *data, size_t length);

    // Speaks text via Google TTS over I2S (fallback when Groq TTS unavailable)
    bool speakText(const String &text);

    void stop();
    void end();

private:
    bool started_ = false;
    bool setSampleRate(uint32_t sampleRate);
    uint32_t currentSampleRate = 0;
};

extern Speaker speaker;

#endif
