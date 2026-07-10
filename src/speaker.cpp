#include "speaker.h"
#include "config.h"

#include <driver/i2s.h>

Speaker speaker;

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

    esp_err_t err;

    err = i2s_driver_install(
        SPK_I2S_PORT,
        &i2s_config,
        0,
        NULL);

    if(err != ESP_OK)
    {
        Serial.println("Speaker I2S install failed");
        return false;
    }

    err = i2s_set_pin(
        SPK_I2S_PORT,
        &pin_config);

    if(err != ESP_OK)
    {
        Serial.println("Speaker pin config failed");
        return false;
    }

    i2s_zero_dma_buffer(SPK_I2S_PORT);

    Serial.println("Speaker Ready");

    return true;
}

bool Speaker::playPCM(const int16_t *buffer, size_t samples)
{
    if(buffer == nullptr || samples == 0)
        return false;

    size_t bytesWritten = 0;

    esp_err_t err = i2s_write(
        SPK_I2S_PORT,
        buffer,
        samples * sizeof(int16_t),
        &bytesWritten,
        portMAX_DELAY);

    return (err == ESP_OK);
}

void Speaker::stop()
{
    i2s_zero_dma_buffer(SPK_I2S_PORT);
}