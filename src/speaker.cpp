#include "speaker.h"
#include "config.h"

#include <driver/i2s.h>
#include <LittleFS.h>

Speaker speaker;

bool Speaker::setSampleRate(uint32_t sampleRate)
{
    if (sampleRate == 0)
    {
        sampleRate = SAMPLE_RATE;
    }

    if (currentSampleRate == sampleRate)
    {
        return true;
    }

    esp_err_t err = i2s_set_sample_rates(SPK_I2S_PORT, sampleRate);
    if (err != ESP_OK)
    {
        Serial.printf("Speaker sample rate %lu failed\n", sampleRate);
        return false;
    }

    currentSampleRate = sampleRate;
    return true;
}

bool Speaker::begin()
{
    i2s_config_t i2s_config =
    {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config =
    {
        .bck_io_num = SPK_BCLK,
        .ws_io_num = SPK_LRC,
        .data_out_num = SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(
        SPK_I2S_PORT,
        &i2s_config,
        0,
        NULL);

    if (err != ESP_OK)
    {
        Serial.println("Speaker I2S install failed");
        return false;
    }

    err = i2s_set_pin(SPK_I2S_PORT, &pin_config);
    if (err != ESP_OK)
    {
        Serial.println("Speaker pin config failed");
        return false;
    }

    i2s_zero_dma_buffer(SPK_I2S_PORT);
    currentSampleRate = SAMPLE_RATE;

    Serial.println("Speaker Ready");
    return true;
}

bool Speaker::playPCM(const int16_t *buffer, size_t samples)
{
    if (buffer == nullptr || samples == 0)
    {
        return false;
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(
        SPK_I2S_PORT,
        buffer,
        samples * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY);

    return err == ESP_OK;
}

bool Speaker::playWavFile(const char *filename)
{
    File file = LittleFS.open(filename, FILE_READ);
    if (!file)
    {
        Serial.printf("Cannot open %s for playback\n", filename);
        return false;
    }

    if (file.size() < 44)
    {
        Serial.println("WAV file too small");
        file.close();
        return false;
    }

    uint8_t header[44];
    if (file.read(header, sizeof(header)) != sizeof(header))
    {
        Serial.println("Failed to read WAV header");
        file.close();
        return false;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
    {
        Serial.println("Not a WAV file");
        file.close();
        return false;
    }

    uint16_t audioFormat = header[20] | (header[21] << 8);
    uint16_t numChannels = header[22] | (header[23] << 8);
    uint32_t sampleRate =
        header[24] |
        (header[25] << 8) |
        (header[26] << 16) |
        (header[27] << 24);
    uint16_t bitsPerSample = header[34] | (header[35] << 8);

    // Find data chunk in case extra chunks exist before PCM data.
    uint32_t dataOffset = 12;
    uint32_t dataSize = 0;
    bool foundData = false;

    file.seek(12);
    while (file.available() >= 8)
    {
        char chunkId[4];
        uint8_t sizeBytes[4];
        file.read((uint8_t *)chunkId, 4);
        file.read(sizeBytes, 4);
        uint32_t chunkSize =
            sizeBytes[0] |
            (sizeBytes[1] << 8) |
            (sizeBytes[2] << 16) |
            (sizeBytes[3] << 24);

        if (memcmp(chunkId, "data", 4) == 0)
        {
            dataOffset = file.position();
            dataSize = chunkSize;
            foundData = true;
            break;
        }

        file.seek(file.position() + chunkSize);
    }

    if (!foundData)
    {
        Serial.println("WAV data chunk not found");
        file.close();
        return false;
    }

    if (audioFormat != 1 || bitsPerSample != 16)
    {
        Serial.printf("Unsupported WAV format=%u bits=%u\n",
                      audioFormat, bitsPerSample);
        file.close();
        return false;
    }

    if (!setSampleRate(sampleRate))
    {
        file.close();
        return false;
    }

    Serial.printf("Playing %s (%lu Hz, %u ch, %lu bytes)\n",
                  filename, sampleRate, numChannels, dataSize);

    file.seek(dataOffset);

    int16_t pcm[256];
    size_t remaining = dataSize;

    while (remaining > 0 && file.available())
    {
        size_t toRead = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
        size_t n = file.read((uint8_t *)pcm, toRead);
        if (n == 0)
        {
            break;
        }

        size_t samples = n / sizeof(int16_t);

        if (numChannels == 2)
        {
            // Downmix stereo to mono for MAX98357A left-only config.
            size_t monoSamples = samples / 2;
            for (size_t i = 0; i < monoSamples; i++)
            {
                pcm[i] = (int16_t)(((int32_t)pcm[i * 2] + pcm[i * 2 + 1]) / 2);
            }
            samples = monoSamples;
        }

        if (!playPCM(pcm, samples))
        {
            file.close();
            setSampleRate(SAMPLE_RATE);
            return false;
        }

        remaining -= n;
    }

    file.close();
    setSampleRate(SAMPLE_RATE);
    Serial.println("Playback done");
    return true;
}

void Speaker::stop()
{
    i2s_zero_dma_buffer(SPK_I2S_PORT);
}
