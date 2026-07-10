#include "openai.h"
#include "config.h"
#include "secrets.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

OpenAI openai;

bool OpenAI::begin()
{
    Serial.println("OpenAI client ready");
    return true;
}

bool OpenAI::ensurePromptAudio(const char *outputFile)
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
    }

    Serial.println("Generating Smartcane greeting...");
    return textToSpeech(PROMPT_TEXT, outputFile);
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

String OpenAI::chat(const String &prompt)
{
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    HTTPClient https;
    https.setTimeout(HTTPS_TIMEOUT);

    // Chat Completions is simpler/faster to parse than Responses API
    if (!https.begin(client, "https://api.openai.com/v1/chat/completions"))
    {
        Serial.println("Unable to connect to OpenAI chat API");
        return "";
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));

    JsonDocument doc;
    doc["model"] = CHAT_MODEL;
    doc["temperature"] = 0;
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

    int httpCode = https.POST(body);
    if (httpCode <= 0)
    {
        Serial.printf("Chat HTTP error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return "";
    }

    String response = https.getString();
    Serial.printf("Chat HTTP %d\n", httpCode);

    if (httpCode != 200)
    {
        Serial.println(response);
        https.end();
        return "";
    }

    JsonDocument result;
    DeserializationError error = deserializeJson(result, response);
    https.end();

    if (error)
    {
        Serial.printf("Chat JSON parse error: %s\n", error.c_str());
        return "";
    }

    if (result["error"].is<JsonObject>())
    {
        Serial.println(result["error"]["message"].as<String>());
        return "";
    }

    String answer = result["choices"][0]["message"]["content"].as<String>();
    answer.trim();
    return answer;
}

String OpenAI::speechToText(const char *wavFile)
{
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

    const String boundary = "----ESP32VoiceBoundary7MA4YWxk";

    String head;
    head.reserve(320);
    head += "--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n";
    head += STT_MODEL;
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\nen";
    head += "\r\n--";
    head += boundary;
    head += "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"record.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";

    const String tail = "\r\n--" + boundary + "--\r\n";
    const size_t contentLength = head.length() + fileSize + tail.length();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    Serial.println("Transcribing...");
    if (!client.connect(OPENAI_HOST, OPENAI_PORT))
    {
        Serial.println("STT connect failed");
        file.close();
        return "";
    }

    client.printf("POST /v1/audio/transcriptions HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", OPENAI_HOST);
    client.printf("Authorization: Bearer %s\r\n", OPENAI_API_KEY);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %u\r\n", (unsigned)contentLength);
    client.print("Connection: close\r\n\r\n");
    client.print(head);

    uint8_t buffer[1024];
    while (file.available())
    {
        size_t n = file.read(buffer, sizeof(buffer));
        if (client.write(buffer, n) != n)
        {
            Serial.println("STT upload stalled");
            file.close();
            client.stop();
            return "";
        }
    }
    file.close();
    client.print(tail);

    int httpStatus = 0;
    if (!skipHttpHeaders(client, httpStatus))
    {
        Serial.println("STT response headers incomplete");
        client.stop();
        return "";
    }

    String response = readRemainingBody(client);
    client.stop();

    if (httpStatus != 200)
    {
        Serial.printf("STT failed HTTP %d\n", httpStatus);
        Serial.println(response);
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

bool OpenAI::textToSpeech(const String &text, const char *outputFile)
{
    if (text.length() == 0)
    {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(HTTPS_TIMEOUT / 1000);

    HTTPClient https;
    https.setTimeout(HTTPS_TIMEOUT);

    if (!https.begin(client, "https://api.openai.com/v1/audio/speech"))
    {
        Serial.println("Unable to connect to OpenAI TTS API");
        return false;
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));

    JsonDocument doc;
    doc["model"] = TTS_MODEL;
    doc["input"] = text;
    doc["voice"] = TTS_VOICE;
    doc["response_format"] = "wav";
    doc["speed"] = 1.15;

    String body;
    serializeJson(doc, body);

    int httpCode = https.POST(body);
    if (httpCode <= 0)
    {
        Serial.printf("TTS HTTP error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return false;
    }

    if (httpCode != 200)
    {
        Serial.printf("TTS failed HTTP %d\n", httpCode);
        Serial.println(https.getString());
        https.end();
        return false;
    }

    File out = LittleFS.open(outputFile, FILE_WRITE);
    if (!out)
    {
        Serial.println("Cannot create TTS output file");
        https.end();
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

    Serial.printf("TTS saved %u bytes to %s\n", (unsigned)total, outputFile);
    return total > 44;
}
