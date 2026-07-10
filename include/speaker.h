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

    void stop();

private:
    bool setSampleRate(uint32_t sampleRate);
    uint32_t currentSampleRate = 0;
};

extern Speaker speaker;

#endif
