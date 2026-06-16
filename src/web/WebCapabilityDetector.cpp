#include "WebCapabilityDetector.h"

#include <QUrl>

namespace
{
QString normalizedProviderName(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("custom") : normalized;
}

QString normalizedMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("openai") || normalized == QStringLiteral("gemini"))
    {
        return normalized;
    }
    return QStringLiteral("auto");
}

bool isOfficialOpenAIBaseUrl(const QString &baseUrl)
{
    const QUrl url(baseUrl.trimmed());
    return url.isValid()
        && url.scheme().startsWith(QStringLiteral("http"))
        && url.host().compare(QStringLiteral("api.openai.com"), Qt::CaseInsensitive) == 0;
}

bool isGeminiProtocol(const AIConfig &config)
{
    return config.protocol == QStringLiteral("google-generative-language");
}

bool hasApiKeyReference(const AIConfig &config)
{
    return !config.apiKey.trimmed().isEmpty()
        || !config.apiKeyEncrypted.trimmed().isEmpty();
}

WebCapability missingAIConfigCapability()
{
    return {
        false,
        WebProviderKind::None,
        QStringLiteral("AI 未配置完整"),
        QStringLiteral("请先在设置页配置当前 AI Provider 的 Base URL、Model 和 API Key。")
    };
}
}

namespace WebCapabilityDetector
{
QString providerKindName(WebProviderKind kind)
{
    switch (kind)
    {
    case WebProviderKind::OpenAIResponses:
        return QStringLiteral("OpenAI Web Search");
    case WebProviderKind::GeminiGrounding:
        return QStringLiteral("Gemini Google Search");
    case WebProviderKind::None:
        return QStringLiteral("None");
    }

    return QStringLiteral("None");
}

WebCapability detect(const AIConfig &config, const WebConfig &webConfig)
{
    if (!webConfig.enabled)
    {
        return {
            false,
            WebProviderKind::None,
            QStringLiteral("联网模式已关闭"),
            QStringLiteral("联网模式已在设置页关闭。")
        };
    }

    const QString provider = normalizedProviderName(config.provider);
    const QString mode = normalizedMode(webConfig.providerMode);

    if ((mode == QStringLiteral("auto") || mode == QStringLiteral("openai"))
        && provider == QStringLiteral("openai")
        && config.protocol == QStringLiteral("openai-compatible"))
    {
        if (config.baseUrl.trimmed().isEmpty() || config.model.trimmed().isEmpty() || !hasApiKeyReference(config))
        {
            return missingAIConfigCapability();
        }

        if (!isOfficialOpenAIBaseUrl(config.baseUrl))
        {
            return {
                false,
                WebProviderKind::None,
                QStringLiteral("OpenAI 联网仅支持官方 API"),
                QStringLiteral("OpenAI 联网模式仅在 Base URL 为 https://api.openai.com 时可用。")
            };
        }

        return {
            true,
            WebProviderKind::OpenAIResponses,
            QStringLiteral("OpenAI 官方联网能力可用"),
            QStringLiteral("当前 OpenAI 官方配置支持联网模式。")
        };
    }

    if ((mode == QStringLiteral("auto") || mode == QStringLiteral("gemini"))
        && isGeminiProtocol(config))
    {
        if (config.baseUrl.trimmed().isEmpty() || config.model.trimmed().isEmpty() || !hasApiKeyReference(config))
        {
            return missingAIConfigCapability();
        }

        return {
            true,
            WebProviderKind::GeminiGrounding,
            QStringLiteral("Gemini 官方联网能力可用"),
            QStringLiteral("当前 Gemini 配置支持联网模式。")
        };
    }

    if (provider == QStringLiteral("deepseek"))
    {
        return {
            false,
            WebProviderKind::None,
            QStringLiteral("DeepSeek 不支持原生联网"),
            QStringLiteral("当前 DeepSeek 官方 API 暂不提供原生联网模式。")
        };
    }

    if (provider == QStringLiteral("custom"))
    {
        return {
            false,
            WebProviderKind::None,
            QStringLiteral("自定义 Provider 无法确认联网能力"),
            QStringLiteral("当前自定义或第三方兼容 API 无法确认支持联网模式。")
        };
    }

    return {
        false,
        WebProviderKind::None,
        QStringLiteral("当前 AI 配置不支持联网模式"),
        QStringLiteral("当前 AI 配置没有可用的原生联网功能。")
    };
}
}
