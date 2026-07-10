#ifndef SPEAKER_H
#define SPEAKER_H

#include <Arduino.h>

class Speaker
{
public:
    // Soft init (does not grab I2S yet)
    bool begin();

    // mono PCM samples — written as stereo L=R for MAX98357A
    bool playPCM(const int16_t *buffer, size_t samples);

    bool playWavFile(const char *filename);

    // Play a WAV sitting in RAM/PSRAM (from Groq Orpheus)
    bool playWavBuffer(const uint8_t *data, size_t length);

    // Short tone via I2S (Hz, duration ms)
    bool playTone(uint32_t freqHz, uint32_t durationMs);

    // Bit-bang I2S-like clocks on the amp pins (wiring diagnostic)
    bool playBitBangTone(uint32_t freqHz, uint32_t durationMs);

    // Google / StreamElements TTS → MP3 decode → speaker
    bool speakText(const String &text);

    void stop();
    void end();

private:
    bool started_ = false;
    bool setSampleRate(uint32_t sampleRate);
    bool ensureOutput();
    void releaseOutput(bool restoreMic);
    uint32_t currentSampleRate = 0;

    bool playMp3Buffer(const uint8_t *mp3, size_t len);
    bool downloadUrl(const String &host, const String &path, uint8_t **out, size_t *outLen);
};

extern Speaker speaker;

#endif
