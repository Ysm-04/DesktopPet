#pragma once

#include <QString>

struct AIConfig
{
    QString providerType = QStringLiteral("openai-compatible");
    QString baseUrl;
    QString apiKey;
    QString model;
    QString path = QStringLiteral("/chat/completions");
    bool stream = false;
    int timeoutMs = 60000;
    double temperature = 0.7;
    int maxTokens = 1024;
};
