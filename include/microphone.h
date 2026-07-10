#ifndef MICROPHONE_H
#define MICROPHONE_H

#include <Arduino.h>
#include <driver/i2s.h>

class Microphone
{
public:
    bool begin();

    bool readSamples(int16_t *buffer,
                     size_t samples,
                     size_t &samplesRead);

    void end();

private:
    void installI2S();
};

extern Microphone microphone;

#endif