#ifndef OPENAI_H
#define OPENAI_H

#include <Arduino.h>

class OpenAI
{
public:
    bool begin();

    String speechToText(const char *wavFile);

    String chat(const String &prompt);

    bool textToSpeech(const String &text,
                      const char *outputFile);

    // Build greeting WAV once so wake responses are instant
    bool ensurePromptAudio(const char *outputFile);
};

extern OpenAI openai;

#endif
