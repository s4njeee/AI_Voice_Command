#include "speaker.h"
#include "config.h"
#include "microphone.h"

#include <driver/i2s.h>
#include <driver/gpio.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <math.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3/minimp3.h"

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

    esp_err_t err = i2s_set_clk(
        SPK_I2S_PORT,
        sampleRate,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO);

    if (err != ESP_OK)
    {
        Serial.printf("Speaker sample rate %lu failed (%d)\n",
                      (unsigned long)sampleRate, (int)err);
        return false;
    }

    currentSampleRate = sampleRate;
    return true;
}

bool Speaker::begin()
{
    Serial.println("========================================");
    Serial.printf("Speaker Ready %s\n", PROJECT_VERSION);
    Serial.printf("Amp pins BCLK=%d LRC=%d DIN=%d (I2S0 while playing)\n",
                  SPK_BCLK, SPK_LRC, SPK_DIN);
    Serial.println("Check: VIN=5V, SD floating, speaker on amp +/-");
    Serial.println("========================================");
    return true;
}

bool Speaker::ensureOutput()
{
    // Mic and speaker share I2S0 — never both installed
    microphone.end();

    if (started_)
    {
        return true;
    }

    gpio_reset_pin((gpio_num_t)SPK_BCLK);
    gpio_reset_pin((gpio_num_t)SPK_LRC);
    gpio_reset_pin((gpio_num_t)SPK_DIN);
    gpio_set_drive_capability((gpio_num_t)SPK_BCLK, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)SPK_LRC, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability((gpio_num_t)SPK_DIN, GPIO_DRIVE_CAP_3);

    i2s_config_t i2s_config =
    {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config =
    {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = SPK_BCLK,
        .ws_io_num = SPK_LRC,
        .data_out_num = SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    // Mic may have left I2S0 installed — uninstall first
    i2s_driver_uninstall(SPK_I2S_PORT);

    esp_err_t err = i2s_driver_install(SPK_I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK)
    {
        Serial.printf("Speaker I2S install failed (%d)\n", (int)err);
        return false;
    }

    err = i2s_set_pin(SPK_I2S_PORT, &pin_config);
    if (err != ESP_OK)
    {
        Serial.printf("Speaker pin config failed (%d)\n", (int)err);
        i2s_driver_uninstall(SPK_I2S_PORT);
        return false;
    }

    i2s_zero_dma_buffer(SPK_I2S_PORT);
    currentSampleRate = 0;
    if (!setSampleRate(SAMPLE_RATE))
    {
        i2s_driver_uninstall(SPK_I2S_PORT);
        return false;
    }

    started_ = true;
    return true;
}

void Speaker::releaseOutput(bool restoreMic)
{
    end();
    if (restoreMic)
    {
        microphone.begin();
    }
}

bool Speaker::playPCM(const int16_t *buffer, size_t samples)
{
    if (!ensureOutput() || buffer == nullptr || samples == 0)
    {
        return false;
    }

    static int16_t stereo[256];
    size_t done = 0;

    while (done < samples)
    {
        const size_t n = (samples - done > 128) ? 128 : (samples - done);
        for (size_t i = 0; i < n; i++)
        {
            const int16_t s = buffer[done + i];
            stereo[i * 2] = s;
            stereo[i * 2 + 1] = s;
        }

        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(
            SPK_I2S_PORT,
            stereo,
            n * 2 * sizeof(int16_t),
            &bytesWritten,
            portMAX_DELAY);

        if (err != ESP_OK || bytesWritten == 0)
        {
            Serial.printf("i2s_write failed err=%d written=%u\n",
                          (int)err, (unsigned)bytesWritten);
            return false;
        }
        done += n;
    }

    return true;
}

bool Speaker::playBitBangTone(uint32_t freqHz, uint32_t durationMs)
{
    // Pure GPIO clocks — if this is silent, amp/wiring/power is wrong (not I2S config)
    microphone.end();
    end();

    pinMode(SPK_BCLK, OUTPUT);
    pinMode(SPK_LRC, OUTPUT);
    pinMode(SPK_DIN, OUTPUT);
    digitalWrite(SPK_BCLK, LOW);
    digitalWrite(SPK_LRC, LOW);
    digitalWrite(SPK_DIN, LOW);

    Serial.printf("BITBANG amp test %lu Hz %lu ms on BCLK=%d LRC=%d DIN=%d\n",
                  (unsigned long)freqHz, (unsigned long)durationMs,
                  SPK_BCLK, SPK_LRC, SPK_DIN);

    const uint32_t sampleRate = 8000;
    const uint32_t totalSamples = (sampleRate * durationMs) / 1000;
    const uint32_t period = (freqHz > 0) ? (sampleRate / freqHz) : 1;

    for (uint32_t n = 0; n < totalSamples; n++)
    {
        const int16_t sample = ((n % period) < (period / 2)) ? 20000 : -20000;

        for (int ch = 0; ch < 2; ch++)
        {
            digitalWrite(SPK_LRC, ch ? HIGH : LOW);
            for (int bit = 15; bit >= 0; bit--)
            {
                digitalWrite(SPK_DIN, (sample >> bit) & 1);
                digitalWrite(SPK_BCLK, HIGH);
                delayMicroseconds(1);
                digitalWrite(SPK_BCLK, LOW);
                delayMicroseconds(1);
            }
        }
        // Rough pacing (~8 kHz frames); bit-bang is slow on purpose
        delayMicroseconds(20);
    }

    digitalWrite(SPK_DIN, LOW);
    microphone.begin();
    Serial.println("BITBANG test done — if silent, check amp power/wiring/SD/speaker");
    return true;
}

bool Speaker::playTone(uint32_t freqHz, uint32_t durationMs)
{
    if (freqHz == 0 || durationMs == 0)
    {
        return false;
    }

    if (!ensureOutput() || !setSampleRate(SAMPLE_RATE))
    {
        return false;
    }

    const size_t totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    static int16_t chunk[128];
    size_t produced = 0;

    Serial.printf("I2S tone %lu Hz %lu ms (square)\n",
                  (unsigned long)freqHz, (unsigned long)durationMs);

    while (produced < totalSamples)
    {
        const size_t n = (totalSamples - produced > 128) ? 128 : (totalSamples - produced);
        for (size_t i = 0; i < n; i++)
        {
            const size_t idx = produced + i;
            const size_t period = SAMPLE_RATE / freqHz;
            chunk[i] = ((period > 0) && ((idx % period) < (period / 2))) ? 30000 : -30000;
        }
        if (!playPCM(chunk, n))
        {
            releaseOutput(true);
            return false;
        }
        produced += n;
    }

    memset(chunk, 0, sizeof(chunk));
    playPCM(chunk, 128);
    releaseOutput(true);
    return true;
}

bool Speaker::playWavFile(const char *filename)
{
    if (!ensureOutput())
    {
        return false;
    }

    File file = LittleFS.open(filename, FILE_READ);
    if (!file)
    {
        Serial.printf("Cannot open %s for playback\n", filename);
        releaseOutput(true);
        return false;
    }

    if (file.size() < 44)
    {
        Serial.println("WAV file too small");
        file.close();
        releaseOutput(true);
        return false;
    }

    uint8_t header[44];
    if (file.read(header, sizeof(header)) != sizeof(header))
    {
        Serial.println("Failed to read WAV header");
        file.close();
        releaseOutput(true);
        return false;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
    {
        Serial.println("Not a WAV file");
        file.close();
        releaseOutput(true);
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

        // Guard against corrupt/huge chunk sizes
        if (chunkSize > file.size())
        {
            break;
        }
        file.seek(file.position() + chunkSize);
    }

    if (!foundData)
    {
        Serial.println("WAV data chunk not found");
        file.close();
        releaseOutput(true);
        return false;
    }

    // Orpheus often sets data size to 0xFFFFFFFF (streaming). Use file length.
    const size_t fileSize = file.size();
    if (dataSize == 0 || dataSize == 0xFFFFFFFFu ||
        (uint64_t)dataOffset + (uint64_t)dataSize > (uint64_t)fileSize)
    {
        if (fileSize <= dataOffset)
        {
            Serial.println("WAV data offset past EOF");
            file.close();
            releaseOutput(true);
            return false;
        }
        dataSize = (uint32_t)(fileSize - dataOffset);
    }

    if (audioFormat != 1 || bitsPerSample != 16)
    {
        Serial.printf("Unsupported WAV format=%u bits=%u\n",
                      audioFormat, bitsPerSample);
        file.close();
        releaseOutput(true);
        return false;
    }

    if (!setSampleRate(sampleRate))
    {
        file.close();
        releaseOutput(true);
        return false;
    }

    Serial.printf("Playing %s (%lu Hz, %u ch, %lu bytes)\n",
                  filename, (unsigned long)sampleRate, numChannels, (unsigned long)dataSize);

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
            releaseOutput(true);
            return false;
        }

        remaining -= n;
    }

    file.close();
    setSampleRate(SAMPLE_RATE);
    Serial.println("Playback done");
    releaseOutput(true);
    return true;
}

bool Speaker::playWavBuffer(const uint8_t *data, size_t length)
{
    if (data == nullptr || length < 44)
    {
        Serial.println("WAV buffer too small");
        return false;
    }

    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
    {
        Serial.println("Not a WAV buffer");
        return false;
    }

    const uint16_t audioFormat = data[20] | (data[21] << 8);
    const uint16_t numChannels = data[22] | (data[23] << 8);
    const uint32_t sampleRate =
        data[24] |
        (data[25] << 8) |
        (data[26] << 16) |
        (data[27] << 24);
    const uint16_t bitsPerSample = data[34] | (data[35] << 8);

    size_t pos = 12;
    size_t dataOffset = 0;
    size_t dataSize = 0;
    bool foundData = false;

    while (pos + 8 <= length)
    {
        const char *chunkId = (const char *)(data + pos);
        const uint32_t chunkSize =
            data[pos + 4] |
            (data[pos + 5] << 8) |
            (data[pos + 6] << 16) |
            (data[pos + 7] << 24);
        pos += 8;

        if (memcmp(chunkId, "data", 4) == 0)
        {
            dataOffset = pos;
            dataSize = chunkSize;
            foundData = true;
            break;
        }

        if (chunkSize > length || pos + chunkSize > length)
        {
            break;
        }
        pos += chunkSize;
    }

    if (!foundData || dataOffset >= length)
    {
        Serial.println("WAV data chunk missing in buffer");
        return false;
    }

    if (dataSize == 0 || dataSize == 0xFFFFFFFFu ||
        dataOffset + dataSize > length)
    {
        dataSize = length - dataOffset;
    }

    if (audioFormat != 1 || bitsPerSample != 16)
    {
        Serial.printf("Unsupported WAV format=%u bits=%u\n",
                      audioFormat, bitsPerSample);
        return false;
    }

    if (!ensureOutput() || !setSampleRate(sampleRate))
    {
        releaseOutput(true);
        return false;
    }

    Serial.printf("Playing Orpheus WAV (%lu Hz, %u ch, %u bytes)\n",
                  (unsigned long)sampleRate, numChannels, (unsigned)dataSize);

    size_t remaining = dataSize;
    size_t offset = dataOffset;
    int16_t pcm[256];

    while (remaining > 0)
    {
        size_t toRead = remaining > sizeof(pcm) ? sizeof(pcm) : remaining;
        memcpy(pcm, data + offset, toRead);
        offset += toRead;
        remaining -= toRead;

        size_t samples = toRead / sizeof(int16_t);
        if (numChannels == 2)
        {
            size_t monoSamples = samples / 2;
            for (size_t i = 0; i < monoSamples; i++)
            {
                pcm[i] = (int16_t)(((int32_t)pcm[i * 2] + pcm[i * 2 + 1]) / 2);
            }
            samples = monoSamples;
        }

        if (!playPCM(pcm, samples))
        {
            setSampleRate(SAMPLE_RATE);
            releaseOutput(true);
            return false;
        }
    }

    setSampleRate(SAMPLE_RATE);
    Serial.println("Playback done");
    releaseOutput(true);
    return true;
}

static String plainTtsText(const String &text)
{
    String out;
    out.reserve(text.length());
    bool inBracket = false;
    for (size_t i = 0; i < text.length(); i++)
    {
        const char c = text[i];
        if (c == '[')
        {
            inBracket = true;
            continue;
        }
        if (c == ']')
        {
            inBracket = false;
            continue;
        }
        if (!inBracket)
        {
            out += c;
        }
    }
    out.trim();
    while (out.indexOf("  ") >= 0)
    {
        out.replace("  ", " ");
    }
    return out;
}

static String urlEncode(const String &in)
{
    String out;
    out.reserve(in.length() * 3);
    const char *hex = "0123456789ABCDEF";
    for (size_t i = 0; i < in.length(); i++)
    {
        const uint8_t c = (uint8_t)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out += (char)c;
        }
        else if (c == ' ')
        {
            out += "%20";
        }
        else
        {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

bool Speaker::downloadUrl(const String &host, const String &path, uint8_t **out, size_t *outLen)
{
    *out = nullptr;
    *outLen = 0;

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20);

    if (!client.connect(host.c_str(), 443))
    {
        Serial.printf("TTS TLS connect failed: %s\n", host.c_str());
        return false;
    }

    client.print(String("GET ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("User-Agent: Mozilla/5.0 (ESP32) Smartcane/1.0\r\n");
    client.print("Accept: audio/mpeg,audio/*;q=0.9,*/*;q=0.8\r\n");
    client.print("Connection: close\r\n\r\n");

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    int httpStatus = 0;
    int sp1 = statusLine.indexOf(' ');
    int sp2 = statusLine.indexOf(' ', sp1 + 1);
    if (sp1 > 0 && sp2 > sp1)
    {
        httpStatus = statusLine.substring(sp1 + 1, sp2).toInt();
    }

    int contentLength = -1;
    bool chunked = false;
    while (client.connected() || client.available())
    {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
        {
            break;
        }
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:"))
        {
            contentLength = lower.substring(15).toInt();
        }
        else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0)
        {
            chunked = true;
        }
    }

    if (httpStatus != 200)
    {
        Serial.printf("TTS HTTP %d from %s\n", httpStatus, host.c_str());
        client.stop();
        return false;
    }

    // Use heap malloc (not ps_malloc) so realloc is safe on ESP32
    size_t capacity = contentLength > 0 ? (size_t)contentLength + 16 : 64 * 1024;
    if (capacity > 200 * 1024)
    {
        capacity = 200 * 1024;
    }
    uint8_t *buf = (uint8_t *)malloc(capacity);
    if (!buf)
    {
        Serial.println("TTS: out of memory");
        client.stop();
        return false;
    }

    size_t total = 0;
    auto append = [&](const uint8_t *src, size_t n) -> bool
    {
        if (total + n > capacity)
        {
            size_t newCap = capacity;
            while (total + n > newCap)
            {
                newCap *= 2;
            }
            uint8_t *grown = (uint8_t *)realloc(buf, newCap);
            if (!grown)
            {
                return false;
            }
            buf = grown;
            capacity = newCap;
        }
        memcpy(buf + total, src, n);
        total += n;
        return true;
    };

    if (chunked)
    {
        while (client.connected() || client.available())
        {
            String lenLine = client.readStringUntil('\n');
            lenLine.trim();
            if (lenLine.length() == 0)
            {
                continue;
            }
            const int chunkLen = (int)strtol(lenLine.c_str(), nullptr, 16);
            if (chunkLen <= 0)
            {
                break;
            }
            size_t got = 0;
            while ((int)got < chunkLen)
            {
                if (!client.available())
                {
                    if (!client.connected())
                    {
                        break;
                    }
                    delay(1);
                    continue;
                }
                uint8_t tmp[512];
                size_t want = chunkLen - got;
                if (want > sizeof(tmp))
                {
                    want = sizeof(tmp);
                }
                int n = client.readBytes(tmp, want);
                if (n <= 0)
                {
                    break;
                }
                if (!append(tmp, n))
                {
                    free(buf);
                    client.stop();
                    return false;
                }
                got += n;
            }
            client.readStringUntil('\n');
        }
    }
    else
    {
        unsigned long last = millis();
        while ((contentLength < 0 || (int)total < contentLength) &&
               (client.connected() || client.available() || millis() - last < 4000))
        {
            if (!client.available())
            {
                delay(1);
                continue;
            }
            uint8_t tmp[1024];
            int n = client.readBytes(tmp, sizeof(tmp));
            if (n <= 0)
            {
                break;
            }
            if (!append(tmp, n))
            {
                free(buf);
                client.stop();
                return false;
            }
            last = millis();
        }
    }

    client.stop();

    if (total < 64)
    {
        Serial.printf("TTS body too small (%u)\n", (unsigned)total);
        free(buf);
        return false;
    }

    *out = buf;
    *outLen = total;
    Serial.printf("TTS downloaded %u bytes from %s\n", (unsigned)total, host.c_str());
    return true;
}

bool Speaker::playMp3Buffer(const uint8_t *mp3, size_t len)
{
    if (mp3 == nullptr || len < 64)
    {
        return false;
    }

    if (!started_)
    {
        if (!ensureOutput())
        {
            return false;
        }
    }

    // All large buffers are static — minimp3 on the stack overflows loopTask (~8KB)
    static mp3dec_t dec;
    static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    static int16_t monoBuf[1152];

    mp3dec_init(&dec);

    size_t offset = 0;
    size_t frames = 0;
    int lastRate = 0;

    while (offset < len)
    {
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof(info));
        const int samples = mp3dec_decode_frame(
            &dec,
            mp3 + offset,
            (int)(len - offset),
            pcm,
            &info);

        if (info.frame_bytes > 0)
        {
            offset += (size_t)info.frame_bytes;
        }
        else
        {
            offset++;
            continue;
        }

        if (samples <= 0)
        {
            continue;
        }

        if (info.hz > 0 && info.hz != lastRate)
        {
            if (!setSampleRate((uint32_t)info.hz))
            {
                return false;
            }
            lastRate = info.hz;
        }

        if (info.channels == 2)
        {
            const int mono = samples;
            if (mono > 1152)
            {
                continue;
            }
            for (int i = 0; i < mono; i++)
            {
                monoBuf[i] = (int16_t)(((int32_t)pcm[i * 2] + pcm[i * 2 + 1]) / 2);
            }
            if (!playPCM(monoBuf, (size_t)mono))
            {
                return false;
            }
        }
        else
        {
            if (!playPCM(pcm, (size_t)samples))
            {
                return false;
            }
        }
        frames++;
    }

    setSampleRate(SAMPLE_RATE);
    Serial.printf("MP3 playback frames=%u\n", (unsigned)frames);
    return frames > 0;
}

bool Speaker::speakText(const String &text)
{
    if (text.length() == 0)
    {
        return false;
    }

    String clipped = plainTtsText(text);
    if (clipped.length() == 0)
    {
        clipped = text;
    }
    // Keep TTS short — smaller MP3, less RAM/CPU, fits Orpheus limit too
    if (clipped.length() > 100)
    {
        clipped = clipped.substring(0, 97) + "...";
    }

    const String encoded = urlEncode(clipped);
    Serial.printf("Speaking (online TTS): %s\n", clipped.c_str());

    delay(20);

    uint8_t *mp3 = nullptr;
    size_t mp3Len = 0;
    bool ok = false;

    // Prefer StreamElements first (often smaller / more reliable than Google on ESP32)
    {
        const String path =
            String("/kappa/v2/speech?voice=Brian&text=") + encoded;
        if (downloadUrl("api.streamelements.com", path, &mp3, &mp3Len))
        {
            ok = playMp3Buffer(mp3, mp3Len);
            free(mp3);
            mp3 = nullptr;
        }
    }

    if (!ok)
    {
        Serial.println("StreamElements TTS failed — trying Google...");
        const String path =
            String("/translate_tts?ie=UTF-8&client=tw-ob&tl=en&q=") + encoded;
        if (downloadUrl("translate.google.com", path, &mp3, &mp3Len))
        {
            ok = playMp3Buffer(mp3, mp3Len);
            free(mp3);
            mp3 = nullptr;
        }
    }

    releaseOutput(true);

    if (ok)
    {
        Serial.println("Speaker playback done");
    }
    else
    {
        Serial.println("Online TTS failed — playing error beeps");
        playTone(400, 150);
        delay(80);
        playTone(400, 150);
    }
    return ok;
}

void Speaker::stop()
{
    if (started_)
    {
        i2s_zero_dma_buffer(SPK_I2S_PORT);
    }
}

void Speaker::end()
{
    if (!started_)
    {
        return;
    }

    i2s_driver_uninstall(SPK_I2S_PORT);
    started_ = false;
    currentSampleRate = 0;
}
