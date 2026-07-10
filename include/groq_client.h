#ifndef GROQ_CLIENT_H
#define GROQ_CLIENT_H

#include <Arduino.h>

class GroqClient
{
public:
    bool begin();

    String speechToText(const char *wavFile);

    String chat(const String &prompt);

    // Save Orpheus speech into a .wav path (PROMPT_FILE / REPLY_FILE)
    bool textToSpeech(const String &text, const char *outputFile);

    // Generate Orpheus .wav, save to path if possible, play on speaker
    bool speakToFile(const String &text, const char *wavPath);

    // Same as speakToFile(text, REPLY_FILE)
    bool speak(const String &text);

    // Build PROMPT_FILE with Orpheus and play it
    bool ensurePromptAudio(const char *outputFile);

    bool quotaBlocked() const { return quotaBlocked_; }
    void clearQuotaBlock() { quotaBlocked_ = false; }

private:
    bool quotaBlocked_ = false;
    void handleApiFailure(int httpStatus, const String &body, const char *where);
    String clipForOrpheus(const String &text) const;
    bool fetchSpeechWav(const String &text, uint8_t **outData, size_t *outLen);
    bool saveWavFile(const char *path, const uint8_t *data, size_t len);
};

extern GroqClient groq;

#endif
