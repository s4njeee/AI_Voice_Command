#include "wav.h"

#include <LittleFS.h>

WAV wav;

static File wavFile;
static uint32_t dataSize = 0;

// Standard 44-byte WAV header
struct WAVHeader
{
    char riff[4];
    uint32_t chunkSize;
    char wave[4];

    char fmt[4];
    uint32_t subChunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;

    char data[4];
    uint32_t subChunk2Size;
};

bool WAV::begin()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS mount failed");
        return false;
    }

    return true;
}

bool WAV::create(const char *filename)
{
    dataSize = 0;

    wavFile = LittleFS.open(filename, FILE_WRITE);

    if (!wavFile)
    {
        Serial.println("Cannot create WAV file");
        return false;
    }

    WAVHeader header;

    memcpy(header.riff, "RIFF", 4);
    header.chunkSize = 36;

    memcpy(header.wave, "WAVE", 4);

    memcpy(header.fmt, "fmt ", 4);
    header.subChunk1Size = 16;
    header.audioFormat = 1;
    header.numChannels = 1;
    header.sampleRate = 16000;
    header.bitsPerSample = 16;
    header.byteRate = 16000 * 1 * 16 / 8;
    header.blockAlign = 1 * 16 / 8;

    memcpy(header.data, "data", 4);
    header.subChunk2Size = 0;

    wavFile.write((uint8_t *)&header, sizeof(header));

    return true;
}

bool WAV::appendSamples(const int16_t *samples,
                        size_t count)
{
    if (!wavFile)
        return false;

    size_t bytes = count * sizeof(int16_t);

    wavFile.write((uint8_t *)samples, bytes);

    dataSize += bytes;

    return true;
}

bool WAV::close()
{
    if (!wavFile)
        return false;

    WAVHeader header;

    memcpy(header.riff, "RIFF", 4);
    header.chunkSize = 36 + dataSize;

    memcpy(header.wave, "WAVE", 4);

    memcpy(header.fmt, "fmt ", 4);
    header.subChunk1Size = 16;
    header.audioFormat = 1;
    header.numChannels = 1;
    header.sampleRate = 16000;
    header.bitsPerSample = 16;
    header.byteRate = 16000 * 2;
    header.blockAlign = 2;

    memcpy(header.data, "data", 4);
    header.subChunk2Size = dataSize;

    wavFile.seek(0);

    wavFile.write((uint8_t *)&header,
                  sizeof(header));

    wavFile.close();

    Serial.println("WAV Saved");

    return true;
}
