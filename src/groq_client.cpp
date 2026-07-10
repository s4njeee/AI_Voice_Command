#include "groq_client.h"
#include "config.h"
#include "secrets.h"
#include "microphone.h"
#include "speaker.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

GroqClient groq;

static String groqApiKey()
{
#if defined(GROQ_API_KEY)
    String key = GROQ_API_KEY;
    if (key.length() > 10 && key.indexOf("PASTE_") < 0 && key.indexOf("your_") < 0)
    {
        return key;
    }
#endif
#if defined(AI_API_KEY)
    return String(AI_API_KEY);
#else
    return String("");
#endif
}

static void pauseAudioForApi()
{
    microphone.end();
    speaker.end();
    delay(30);
}

static void resumeAudioAfterApi()
{
    microphone.begin();
    speaker.begin();
}

bool GroqClient::quotaBlocked() const
{
    if (!quotaBlocked_)
    {
        return false;
    }
    if (quotaBlockedUntil_ != 0 && millis() >= quotaBlockedUntil_)
    {
        return false;
    }
    return true;
}

void GroqClient::clearQuotaBlock()
{
    quotaBlocked_ = false;
    quotaBlockedUntil_ = 0;
}

void GroqClient::handleApiFailure(int httpStatus, const String &body, const char *where)
{
    Serial.printf("%s failed HTTP %d\n", where, httpStatus);

    if (httpStatus == 401)
    {
        Serial.println("Groq rejected the API key. Check GROQ_API_KEY in secrets.h");
    }
    else if (httpStatus == 400 && body.indexOf("model_terms_required") >= 0)
    {
        Serial.println("========================================");
        Serial.println("Accept Groq Orpheus TTS terms once:");
        Serial.println("https://console.groq.com/playground?model=canopylabs/orpheus-v1-english");
        Serial.println("Then reboot. Until then, Google TTS fallback is used.");
        Serial.println("========================================");
        ttsRateLimited_ = true;
    }
    else if (httpStatus == 404)
    {
        Serial.println("Model not found on Groq. Firmware must use Groq model IDs.");
    }
    else if (httpStatus == 429 || body.indexOf("rate_limit") >= 0)
    {
        // Orpheus TTS TPD is tiny — do NOT freeze STT/chat for that.
        if (where != nullptr && strcmp(where, "TTS") == 0)
        {
            ttsRateLimited_ = true;
            Serial.println("Orpheus TTS rate-limited. Using Google TTS. STT/chat still active.");
        }
        else
        {
            quotaBlocked_ = true;
            quotaBlockedUntil_ = millis() + 60000UL;
            Serial.println("Groq STT/chat rate limit. Pausing ~60s, then retrying.");
        }
    }

    if (body.length() > 0 && body.length() < 600)
    {
        Serial.println(body);
    }
}

bool GroqClient::begin()
{
    const String key = groqApiKey();
    if (key.length() < 20 || key.indexOf("PASTE_") >= 0 || !key.startsWith("gsk_"))
    {
        Serial.println("========================================");
        Serial.println("GROQ KEY MISSING OR INVALID");
        Serial.println("Edit include/secrets.h:");
        Serial.println("  #define GROQ_API_KEY \"gsk_...\"");
        Serial.println("Then Build + Upload");
        Serial.println("========================================");
    }

    Serial.println("Groq AI client ready (api.groq.com)");
    Serial.printf("STT=%s\n", GROQ_STT_MODEL);
    Serial.printf("CHAT=%s\n", GROQ_CHAT_MODEL);
    Serial.printf("TTS=%s voice=%s\n", GROQ_TTS_MODEL, GROQ_TTS_VOICE);
    return true;
}

bool GroqClient::cachedWavLooksValid(const char *path) const
{
    if (path == nullptr || !LittleFS.exists(path))
    {
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f || f.size() < 44)
    {
        if (f)
        {
            f.close();
        }
        return false;
    }

    uint8_t hdr[12];
    const size_t n = f.read(hdr, sizeof(hdr));
    f.close();
    if (n != sizeof(hdr))
    {
        return false;
    }

    return hdr[0] == 'R' && hdr[1] == 'I' && hdr[2] == 'F' && hdr[3] == 'F' &&
           hdr[8] == 'W' && hdr[9] == 'A' && hdr[10] == 'V' && hdr[11] == 'E';
}

bool GroqClient::ensurePromptAudio(const char *outputFile)
{
    // Reuse cached greeting — avoids burning Orpheus TPD every reboot
    if (cachedWavLooksValid(outputFile))
    {
        Serial.printf("Playing cached %s...\n", outputFile);
        if (speaker.playWavFile(outputFile))
        {
            return true;
        }
    }

    if (ttsRateLimited_)
    {
        Serial.println("Orpheus TTS limited — skip prompt build.");
        return false;
    }

    Serial.printf("Building %s with Orpheus voice=%s...\n",
                  outputFile, GROQ_TTS_VOICE);

    // One attempt only (second Orpheus call wastes TPD after a 429)
    return speakToFile(PROMPT_TEXT, outputFile);
}

String GroqClient::clipForOrpheus(const String &text) const
{
    String clipped = text;
    clipped.trim();
    if (clipped.length() > GROQ_TTS_MAX_CHARS)
    {
        clipped = clipped.substring(0, GROQ_TTS_MAX_CHARS - 3) + "...";
    }
    return clipped;
}

bool GroqClient::saveWavFile(const char *path, const uint8_t *data, size_t len)
{
    if (path == nullptr || data == nullptr || len < 44)
    {
        return false;
    }

    if (LittleFS.exists(path))
    {
        LittleFS.remove(path);
    }

    File out = LittleFS.open(path, "w");
    if (!out)
    {
        Serial.printf("Cannot write %s\n", path);
        return false;
    }

    const size_t written = out.write(data, len);
    out.flush();
    out.close();

    if (written != len)
    {
        Serial.printf("Short write to %s (%u/%u)\n",
                      path, (unsigned)written, (unsigned)len);
        return false;
    }

    Serial.printf("Saved Orpheus audio -> %s (%u bytes)\n",
                  path, (unsigned)len);
    return true;
}

bool GroqClient::speakToFile(const String &text, const char *wavPath)
{
    if (ttsRateLimited_)
    {
        return false;
    }

    uint8_t *wav = nullptr;
    size_t len = 0;
    if (!fetchSpeechWav(text, &wav, &len))
    {
        return false;
    }

    const bool saved = saveWavFile(wavPath, wav, len);
    bool played = false;

    if (saved)
    {
        played = speaker.playWavFile(wavPath);
    }

    if (!played)
    {
        Serial.println("Playing Orpheus WAV from RAM...");
        played = speaker.playWavBuffer(wav, len);
    }

    free(wav);
    return played;
}

bool GroqClient::speak(const String &text)
{
    return speakToFile(text, REPLY_FILE);
}

bool GroqClient::textToSpeech(const String &text, const char *outputFile)
{
    uint8_t *wav = nullptr;
    size_t len = 0;
    if (!fetchSpeechWav(text, &wav, &len))
    {
        return false;
    }

    const bool saved = saveWavFile(outputFile, wav, len);
    free(wav);
    return saved;
}

bool GroqClient::fetchSpeechWav(const String &text, uint8_t **outData, size_t *outLen)
{
    *outData = nullptr;
    *outLen = 0;

    if (ttsRateLimited_)
    {
        return false;
    }

    const String clipped = clipForOrpheus(text);
    if (clipped.length() == 0)
    {
        return false;
    }

    const String key = groqApiKey();
    pauseAudioForApi();

    JsonDocument doc;
    doc["model"] = GROQ_TTS_MODEL;
    doc["input"] = clipped;
    doc["voice"] = GROQ_TTS_VOICE;
    doc["response_format"] = "wav";

    String reqBody;
    serializeJson(doc, reqBody);

    Serial.printf("Orpheus TTS voice=%s model=%s\n", GROQ_TTS_VOICE, GROQ_TTS_MODEL);
    Serial.printf("Orpheus text: %s\n", clipped.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    if (!client.connect(AI_HOST, AI_PORT))
    {
        Serial.println("Orpheus TLS connect failed");
        resumeAudioAfterApi();
        return false;
    }

    client.print("POST /openai/v1/audio/speech HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(AI_HOST);
    client.print("\r\n");
    client.print("Authorization: Bearer ");
    client.print(key);
    client.print("\r\n");
    client.print("Content-Type: application/json\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)reqBody.length());
    client.print("Connection: close\r\n\r\n");
    client.print(reqBody);

    int httpStatus = 0;
    int contentLength = -1;
    bool chunked = false;

    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    int sp1 = statusLine.indexOf(' ');
    int sp2 = statusLine.indexOf(' ', sp1 + 1);
    if (sp1 > 0 && sp2 > sp1)
    {
        httpStatus = statusLine.substring(sp1 + 1, sp2).toInt();
    }

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

    size_t capacity = contentLength > 0 ? (size_t)contentLength + 8 : 256 * 1024;
    uint8_t *buffer = (uint8_t *)ps_malloc(capacity);
    if (!buffer)
    {
        buffer = (uint8_t *)malloc(capacity);
    }
    if (!buffer)
    {
        Serial.println("Out of memory for Orpheus WAV");
        client.stop();
        resumeAudioAfterApi();
        return false;
    }

    size_t total = 0;
    unsigned long lastData = millis();

    auto appendBytes = [&](const uint8_t *src, size_t n) -> bool
    {
        if (total + n > capacity)
        {
            size_t newCap = capacity;
            while (total + n > newCap)
            {
                newCap *= 2;
            }
            uint8_t *grown = (uint8_t *)realloc(buffer, newCap);
            if (!grown)
            {
                return false;
            }
            buffer = grown;
            capacity = newCap;
        }
        memcpy(buffer + total, src, n);
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
                if (!appendBytes(tmp, n))
                {
                    Serial.println("Orpheus WAV grew too large");
                    free(buffer);
                    client.stop();
                    resumeAudioAfterApi();
                    return false;
                }
                got += n;
                lastData = millis();
            }
            client.readStringUntil('\n'); // trailing CRLF
        }
    }
    else
    {
        while ((contentLength < 0 || (int)total < contentLength) &&
               (client.connected() || client.available() || millis() - lastData < 5000))
        {
            if (!client.available())
            {
                delay(1);
                if (!client.connected() && !client.available())
                {
                    break;
                }
                continue;
            }

            uint8_t tmp[1024];
            int n = client.readBytes(tmp, sizeof(tmp));
            if (n <= 0)
            {
                break;
            }
            if (!appendBytes(tmp, n))
            {
                Serial.println("Orpheus WAV grew too large");
                free(buffer);
                client.stop();
                resumeAudioAfterApi();
                return false;
            }
            lastData = millis();
        }
    }

    client.stop();
    resumeAudioAfterApi();

    if (httpStatus != 200)
    {
        String err;
        err.reserve(total + 1);
        for (size_t i = 0; i < total && i < 500; i++)
        {
            err += (char)buffer[i];
        }
        free(buffer);
        handleApiFailure(httpStatus, err, "TTS");
        return false;
    }

    // Find RIFF header (some proxies/prefixes can appear before it)
    size_t riffAt = SIZE_MAX;
    for (size_t i = 0; i + 12 < total; i++)
    {
        if (buffer[i] == 'R' && buffer[i + 1] == 'I' && buffer[i + 2] == 'F' && buffer[i + 3] == 'F' &&
            buffer[i + 8] == 'W' && buffer[i + 9] == 'A' && buffer[i + 10] == 'V' && buffer[i + 11] == 'E')
        {
            riffAt = i;
            break;
        }
    }

    Serial.printf("Orpheus download %u bytes, status=%d, head=%02X %02X %02X %02X\n",
                  (unsigned)total, httpStatus,
                  total > 0 ? buffer[0] : 0,
                  total > 1 ? buffer[1] : 0,
                  total > 2 ? buffer[2] : 0,
                  total > 3 ? buffer[3] : 0);

    if (riffAt == SIZE_MAX)
    {
        Serial.println("Orpheus body is not WAV (no RIFF). First bytes as text:");
        for (size_t i = 0; i < total && i < 120; i++)
        {
            char c = (char)buffer[i];
            Serial.print((c >= 32 && c < 127) ? c : '.');
        }
        Serial.println();
        free(buffer);
        return false;
    }

    if (riffAt > 0)
    {
        Serial.printf("Skipping %u prefix bytes before RIFF\n", (unsigned)riffAt);
        memmove(buffer, buffer + riffAt, total - riffAt);
        total -= riffAt;
    }

    *outData = buffer;
    *outLen = total;
    Serial.printf("Orpheus WAV ready (%u bytes)\n", (unsigned)total);
    return true;
}

static bool skipHttpHeaders(WiFiClientSecure &client, int &httpStatus)
{
    httpStatus = 0;
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    int sp1 = statusLine.indexOf(' ');
    int sp2 = statusLine.indexOf(' ', sp1 + 1);
    if (sp1 > 0 && sp2 > sp1)
    {
        httpStatus = statusLine.substring(sp1 + 1, sp2).toInt();
    }

    while (client.connected() || client.available())
    {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
        {
            return true;
        }
    }

    return false;
}

static String readRemainingBody(WiFiClientSecure &client)
{
    String body;
    unsigned long lastData = millis();

    while (millis() - lastData < HTTPS_TIMEOUT)
    {
        while (client.available())
        {
            body += (char)client.read();
            lastData = millis();
        }

        if (!client.connected() && !client.available())
        {
            break;
        }

        delay(1);
    }

    return body;
}

String GroqClient::chat(const String &prompt)
{
    const String key = groqApiKey();
    pauseAudioForApi();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    HTTPClient https;
    https.setTimeout(HTTPS_TIMEOUT);

    if (!https.begin(client, AI_CHAT_URL))
    {
        Serial.println("Unable to connect to Groq chat API");
        resumeAudioAfterApi();
        return "";
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + key);

    JsonDocument doc;
    doc["model"] = GROQ_CHAT_MODEL;
    doc["temperature"] = 0.1;
    doc["max_tokens"] = 40;

    JsonArray messages = doc["messages"].to<JsonArray>();

    JsonObject systemMsg = messages.add<JsonObject>();
    systemMsg["role"] = "system";
    systemMsg["content"] =
        "You are Smartcane, a voice assistant for a blind/low-vision user. "
        "Reply in ONE short sentence, max 12 words. "
        "No markdown, lists, questions, or filler.";

    JsonObject userMsg = messages.add<JsonObject>();
    userMsg["role"] = "user";
    userMsg["content"] = prompt;

    String body;
    serializeJson(doc, body);

    Serial.println("Groq chat...");
    int httpCode = https.POST(body);
    if (httpCode <= 0)
    {
        Serial.printf("Chat HTTP error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        resumeAudioAfterApi();
        return "";
    }

    String response = https.getString();
    Serial.printf("Chat HTTP %d\n", httpCode);

    if (httpCode != 200)
    {
        handleApiFailure(httpCode, response, "Chat");
        https.end();
        resumeAudioAfterApi();
        return "";
    }

    JsonDocument result;
    DeserializationError error = deserializeJson(result, response);
    https.end();
    resumeAudioAfterApi();

    if (error)
    {
        Serial.printf("Chat JSON parse error: %s\n", error.c_str());
        return "";
    }

    if (result["error"].is<JsonObject>())
    {
        handleApiFailure(httpCode, response, "Chat");
        return "";
    }

    String answer = result["choices"][0]["message"]["content"].as<String>();
    answer.trim();
    // Hard cap for voice — long answers burn Orpheus TPD and crash TTS
    if (answer.length() > 120)
    {
        answer = answer.substring(0, 117) + "...";
    }
    return answer;
}

String GroqClient::speechToText(const char *wavFile)
{
    const String key = groqApiKey();
    File file = LittleFS.open(wavFile, FILE_READ);
    if (!file)
    {
        Serial.println("Cannot open WAV for transcription");
        return "";
    }

    const size_t fileSize = file.size();
    if (fileSize == 0)
    {
        Serial.println("WAV file is empty");
        file.close();
        return "";
    }

    // Pause I2S so TLS to Groq does not abort
    pauseAudioForApi();

    const String boundary = "----ESP32GroqBoundary7MA4YWxk";

    String head;
    head.reserve(480);
    head += "--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n";
    head += GROQ_STT_MODEL;
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nen";
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"temperature\"\r\n\r\n0";
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"prompt\"\r\n\r\n";
    head += "Smartcane. Hey Smartcane. Voice commands for help, directions, time, weather.";
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"record.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";

    const String tail = "\r\n--" + boundary + "--\r\n";
    const size_t contentLength = head.length() + fileSize + tail.length();

    Serial.printf("Groq STT -> %s model=%s (%u bytes)\n",
                  AI_HOST, GROQ_STT_MODEL, (unsigned)fileSize);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    bool connected = false;
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        Serial.printf("STT TLS connect attempt %d/3...\n", attempt);
        if (client.connect(AI_HOST, AI_PORT))
        {
            connected = true;
            break;
        }
        delay(400 * attempt);
    }

    if (!connected)
    {
        Serial.println("STT connect to api.groq.com failed");
        file.close();
        resumeAudioAfterApi();
        return "";
    }

    client.print("POST /openai/v1/audio/transcriptions HTTP/1.1\r\n");
    client.print("Host: ");
    client.print(AI_HOST);
    client.print("\r\n");
    client.print("Authorization: Bearer ");
    client.print(key);
    client.print("\r\n");
    client.print("Content-Type: multipart/form-data; boundary=");
    client.print(boundary);
    client.print("\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)contentLength);
    client.print("Connection: close\r\n\r\n");
    client.print(head);

    uint8_t buffer[1024];
    while (file.available())
    {
        size_t n = file.read(buffer, sizeof(buffer));
        size_t written = 0;
        while (written < n)
        {
            size_t w = client.write(buffer + written, n - written);
            if (w == 0)
            {
                Serial.println("STT upload stalled");
                file.close();
                client.stop();
                resumeAudioAfterApi();
                return "";
            }
            written += w;
        }
    }
    file.close();
    client.print(tail);

    int httpStatus = 0;
    if (!skipHttpHeaders(client, httpStatus))
    {
        Serial.println("STT response headers incomplete");
        client.stop();
        resumeAudioAfterApi();
        return "";
    }

    String response = readRemainingBody(client);
    client.stop();
    resumeAudioAfterApi();

    if (httpStatus != 200)
    {
        handleApiFailure(httpStatus, response, "STT");
        return "";
    }

    JsonDocument result;
    DeserializationError error = deserializeJson(result, response);
    if (error)
    {
        Serial.printf("STT JSON parse error: %s\n", error.c_str());
        Serial.println(response);
        return "";
    }

    if (result["error"].is<JsonObject>())
    {
        Serial.print("STT API error: ");
        Serial.println(result["error"]["message"].as<String>());
        return "";
    }

    String text = result["text"].as<String>();
    text.trim();
    return text;
}
