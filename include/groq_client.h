#ifndef GROQ_CLIENT_H
#define GROQ_CLIENT_H

#include <Arduino.h>

class GroqClient
{
public:
    bool begin();

    String speechToText(const char *wavFile);

    String chat(const String &prompt);

    // Generate Orpheus WAV to LittleFS (optional cache)
    bool textToSpeech(const String &text, const char *outputFile);

    // Generate Orpheus audio and play on speaker (no file upload needed)
    bool speak(const String &text);

    bool ensurePromptAudio(const char *outputFile);

    bool quotaBlocked() const { return quotaBlocked_; }
    void clearQuotaBlock() { quotaBlocked_ = false; }

private:
    bool quotaBlocked_ = false;
    void handleApiFailure(int httpStatus, const String &body, const char *where);
    String clipForOrpheus(const String &text) const;
    bool fetchSpeechWav(const String &text, uint8_t **outData, size_t *outLen);
};

extern GroqClient groq;

#endif
