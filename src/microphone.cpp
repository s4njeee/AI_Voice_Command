#include "microphone.h"
#include "config.h"
#include "wifi_manager.h"

Microphone microphone;

void Microphone::installI2S()
{
    i2s_config_t i2s_config =
    {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config =
    {
        .bck_io_num = MIC_SCK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD
    };

    i2s_driver_install(MIC_I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(MIC_I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(MIC_I2S_PORT);
}

bool Microphone::begin()
{
    if (started_)
    {
        return true;
    }

    installI2S();
    started_ = true;
    Serial.println("INMP441 Initialized");
    return true;
}

bool Microphone::readSamples(
    int16_t *buffer,
    size_t samples,
    size_t &samplesRead)
{
    size_t bytesRead = 0;
    static int32_t rawBuffer[512];

    if (samples > 512)
    {
        samples = 512;
    }

    esp_err_t result = i2s_read(
        MIC_I2S_PORT,
        rawBuffer,
        samples * sizeof(int32_t),
        &bytesRead,
        portMAX_DELAY);

    if (result != ESP_OK)
    {
        samplesRead = 0;
        return false;
    }

    samplesRead = bytesRead / sizeof(int32_t);

    for (size_t i = 0; i < samplesRead; i++)
    {
        buffer[i] = rawBuffer[i] >> 14;
    }

    return true;
}

uint32_t Microphone::measureLevel()
{
    int16_t buffer[256];
    size_t samplesRead = 0;

    if (!readSamples(buffer, 256, samplesRead) || samplesRead == 0)
    {
        return 0;
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < samplesRead; i++)
    {
        int32_t sample = buffer[i];
        if (sample < 0)
        {
            sample = -sample;
        }
        sum += (uint32_t)sample;
    }

    return (uint32_t)(sum / samplesRead);
}

bool Microphone::waitForSpeech(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    uint8_t hotFrames = 0;
    unsigned long lastWifiCheck = millis();

    while (true)
    {
        if (timeoutMs > 0 && (millis() - start) >= timeoutMs)
        {
            return false;
        }

        // Keep WiFi maintenance alive while listening
        if (millis() - lastWifiCheck >= 1000)
        {
            lastWifiCheck = millis();
            wifiManager.loop();
            if (!wifiManager.connected())
            {
                return false;
            }
        }

        const uint32_t level = measureLevel();
        if (level >= VAD_THRESHOLD)
        {
            hotFrames++;
            if (hotFrames >= 2)
            {
                return true;
            }
        }
        else
        {
            hotFrames = 0;
        }
    }
}

void Microphone::end()
{
    if (!started_)
    {
        return;
    }

    i2s_driver_uninstall(MIC_I2S_PORT);
    started_ = false;
}
