#include "groq_client.h"
#include "config.h"
#include "secrets.h"
#include "microphone.h"
#include "speaker.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

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
    }
    else if (httpStatus == 404)
    {
        Serial.println("Model not found on Groq. Firmware must use Groq model IDs.");
    }
    else if (httpStatus == 429 || body.indexOf("rate_limit") >= 0)
    {
        quotaBlocked_ = true;
        Serial.println("Groq free-tier rate limit hit. Wait, then reboot.");
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
    Serial.printf("TTS=%s\n", GROQ_TTS_MODEL);
    return true;
}

bool GroqClient::ensurePromptAudio(const char *outputFile)
{
    if (LittleFS.exists(outputFile))
    {
        File existing = LittleFS.open(outputFile, FILE_READ);
        if (existing && existing.size() > 44)
        {
            existing.close();
            Serial.println("Cached Smartcane greeting ready");
            return true;
        }
        if (existing)
        {
            existing.close();
        }
        LittleFS.remove(outputFile);
    }

    Serial.println("Generating Smartcane greeting via Groq TTS...");
    if (!textToSpeech(PROMPT_TEXT, outputFile))
    {
        Serial.println("Greeting TTS skipped (will print text instead when woken).");
        return false;
    }
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
    doc["temperature"] = 0.2;
    doc["max_tokens"] = 120;

    JsonArray messages = doc["messages"].to<JsonArray>();

    JsonObject systemMsg = messages.add<JsonObject>();
    systemMsg["role"] = "system";
    systemMsg["content"] =
        "You are Smartcane, a helpful voice assistant. "
        "Answer briefly in 1-2 short spoken sentences. "
        "No markdown, lists, or special characters.";

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
    head.reserve(320);
    head += "--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n";
    head += GROQ_STT_MODEL;
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nen";
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

bool GroqClient::textToSpeech(const String &text, const char *outputFile)
{
    if (text.length() == 0)
    {
        return false;
    }

    const String key = groqApiKey();
    pauseAudioForApi();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    HTTPClient https;
    https.setTimeout(HTTPS_TIMEOUT);

    Serial.printf("Groq TTS -> %s model=%s\n", AI_TTS_URL, GROQ_TTS_MODEL);
    if (!https.begin(client, AI_TTS_URL))
    {
        Serial.println("Unable to connect to Groq TTS API");
        resumeAudioAfterApi();
        return false;
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + key);

    JsonDocument doc;
    doc["model"] = GROQ_TTS_MODEL;
    doc["input"] = text;
    doc["voice"] = GROQ_TTS_VOICE;
    doc["response_format"] = "wav";

    String body;
    serializeJson(doc, body);

    int httpCode = https.POST(body);
    if (httpCode <= 0)
    {
        Serial.printf("TTS HTTP error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        resumeAudioAfterApi();
        return false;
    }

    if (httpCode != 200)
    {
        handleApiFailure(httpCode, https.getString(), "TTS");
        https.end();
        resumeAudioAfterApi();
        return false;
    }

    if (LittleFS.exists(outputFile))
    {
        LittleFS.remove(outputFile);
    }

    File out = LittleFS.open(outputFile, FILE_WRITE);
    if (!out)
    {
        Serial.println("Cannot create TTS output file on LittleFS");
        https.end();
        resumeAudioAfterApi();
        return false;
    }

    WiFiClient *stream = https.getStreamPtr();
    uint8_t buffer[1024];
    size_t total = 0;
    unsigned long lastData = millis();

    while (https.connected() || stream->available())
    {
        size_t avail = stream->available();
        if (avail == 0)
        {
            if (millis() - lastData > 5000)
            {
                break;
            }
            delay(1);
            continue;
        }

        size_t toRead = avail > sizeof(buffer) ? sizeof(buffer) : avail;
        int n = stream->readBytes(buffer, toRead);
        if (n <= 0)
        {
            break;
        }

        out.write(buffer, n);
        total += n;
        lastData = millis();
    }

    out.close();
    https.end();
    resumeAudioAfterApi();

    Serial.printf("TTS saved %u bytes to %s\n", (unsigned)total, outputFile);
    return total > 44;
}
