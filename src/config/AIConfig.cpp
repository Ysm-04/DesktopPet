#include "AIConfig.h"

AIConfig defaultAIConfigForProvider(const QString &provider)
{
    AIConfig config;
    const QString normalizedProvider = provider.trimmed().toLower();
    config.provider = normalizedProvider;

    if (normalizedProvider == QStringLiteral("openai"))
    {
        config.protocol = QStringLiteral("openai-compatible");
        config.baseUrl = QStringLiteral("https://api.openai.com/v1");
        config.path = QStringLiteral("/chat/completions");
        return config;
    }

    if (normalizedProvider == QStringLiteral("deepseek"))
    {
        config.protocol = QStringLiteral("openai-compatible");
        config.baseUrl = QStringLiteral("https://api.deepseek.com/v1");
        config.path = QStringLiteral("/chat/completions");
        return config;
    }

    if (normalizedProvider == QStringLiteral("claude"))
    {
        config.protocol = QStringLiteral("anthropic");
        config.baseUrl = QStringLiteral("https://api.anthropic.com");
        config.path = QStringLiteral("/v1/messages");
        return config;
    }

    if (normalizedProvider == QStringLiteral("google"))
    {
        config.protocol = QStringLiteral("google-generative-language");
        config.baseUrl = QStringLiteral("https://generativelanguage.googleapis.com");
        config.path = QStringLiteral("/v1beta/models/{model}:generateContent");
        return config;
    }

    config.provider = QStringLiteral("custom");
    config.protocol = QStringLiteral("openai-compatible");
    config.path = QStringLiteral("/chat/completions");
    return config;
}
