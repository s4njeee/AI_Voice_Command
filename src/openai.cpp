#include "openai.h"
#include "config.h"
#include "secrets.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

OpenAI openai;

bool OpenAI::begin()
{
    return true;
}

String OpenAI::chat(const String &prompt)
{
    WiFiClientSecure client;
    client.setInsecure();   // Sample only. For production, validate certificates.

    HTTPClient https;

    if (!https.begin(client, "https://api.openai.com/v1/responses"))
    {
        Serial.println("Unable to connect");
        return "";
    }

    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));

    JsonDocument doc;
    doc["model"] = CHAT_MODEL;
    doc["input"] = prompt;

    String body;
    serializeJson(doc, body);

    int httpCode = https.POST(body);

    if (httpCode <= 0)
    {
        Serial.printf("HTTP Error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return "";
    }

    String response = https.getString();

    Serial.println("========== API RESPONSE ==========");
    Serial.println(response);
    Serial.println("==================================");

    JsonDocument result;

    DeserializationError error = deserializeJson(result, response);

    if (error)
    {
        Serial.println("JSON Parse Error");
        https.end();
        return "";
    }

    String answer = "";

    // NOTE:
    // The Responses API returns structured output.
    // This extraction is simplified and may need to be updated
    // depending on the response format.

    if (result["output"].is<JsonArray>())
    {
        JsonArray output = result["output"];

        if (output.size() > 0)
        {
            JsonObject first = output[0];

            if (first["content"].is<JsonArray>())
            {
                JsonArray content = first["content"];

                if (content.size() > 0)
                {
                    answer = content[0]["text"].as<String>();
                }
            }
        }
    }

    https.end();

    return answer;
}

String OpenAI::speechToText(const char *wavFile)
{
    Serial.println("speechToText() not implemented yet.");
    return "";
}

bool OpenAI::textToSpeech(const String &text, const char *outputFile)
{
    Serial.println("textToSpeech() not implemented yet.");
    return false;
}