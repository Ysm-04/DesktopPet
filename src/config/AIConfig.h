#pragma once

#include <QString>

struct AIConfig
{
    QString provider = QStringLiteral("custom");
    QString protocol = QStringLiteral("openai-compatible");
    QString baseUrl;
    QString model;
    QString path = QStringLiteral("/chat/completions");
    QString apiKeyStorage = QStringLiteral("none");
    QString apiKeyEncrypted;
    QString apiKey;
    bool allowPlainApiKey = false;
    bool stream = false;
    int timeoutMs = 60000;
    double temperature = 0.7;
    int maxTokens = 1024;
};

AIConfig defaultAIConfigForProvider(const QString &provider);
