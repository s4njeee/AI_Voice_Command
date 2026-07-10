#ifndef SPEAKER_H
#define SPEAKER_H

#include <Arduino.h>

class Speaker
{
public:
    bool begin();

    bool playPCM(const int16_t *buffer,
                 size_t samples);

    void stop();
};

extern Speaker speaker;

#endif