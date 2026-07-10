#ifndef WAV_H
#define WAV_H

#include <Arduino.h>

class WAV
{
public:
    bool begin();

    bool create(const char *filename);

    bool appendSamples(const int16_t *samples,
                       size_t count);

    bool close();

private:
    size_t totalSamples = 0;
};

extern WAV wav;

#endif