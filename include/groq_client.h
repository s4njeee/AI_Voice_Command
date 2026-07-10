#ifndef GROQ_CLIENT_H
#define GROQ_CLIENT_H

#include <Arduino.h>

class GroqClient
{
public:
    bool begin();

    String speechToText(const char *wavFile);

    String chat(const String &prompt);

    bool textToSpeech(const String &text, const char *outputFile);

    bool ensurePromptAudio(const char *outputFile);

    bool quotaBlocked() const { return quotaBlocked_; }
    void clearQuotaBlock() { quotaBlocked_ = false; }

private:
    bool quotaBlocked_ = false;
    void handleApiFailure(int httpStatus, const String &body, const char *where);
};

extern GroqClient groq;

#endif
