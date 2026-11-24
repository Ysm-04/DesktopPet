#include "WebCapabilityDetector.h"

#include <QUrl>

namespace
{
QString normalizedProviderName(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("custom") : normalized;
}

bool isOfficialHost(const QString &baseUrl, const QString &expectedHost)
{
    const QUrl url(baseUrl.trimmed());
    const QString scheme = url.scheme().toLower();
    return url.isValid()
        && (scheme == QStringLiteral("https") || scheme == QStringLiteral("http"))
        && url.host().compare(expectedHost, Qt::CaseInsensitive) == 0;
}

bool isOpenAICompatibleProtocol(const AIConfig &config)
{
    return config.protocol == QStringLiteral("openai-compatible");
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

bool hasCompleteAIConfig(const AIConfig &config)
{
    return !config.baseUrl.trimmed().isEmpty()
        && !config.model.trimmed().isEmpty()
        && hasApiKeyReference(config);
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

WebCapability protocolMismatchCapability(const QString &expectedWebName, const QString &currentProtocolName)
{
    return {
        false,
        WebProviderKind::None,
        QStringLiteral("联网协议与当前 AI 协议冲突"),
        QStringLiteral("当前联网模式手动选择了 %1，与当前 AI 接口协议（%2）不匹配。请切回“自动”或选择匹配的协议。")
            .arg(expectedWebName, currentProtocolName)
    };
}

WebCapability detectManualMode(const AIConfig &config, const QString &mode)
{
    if (!hasCompleteAIConfig(config))
    {
        return missingAIConfigCapability();
    }

    if (mode == QStringLiteral("openai-responses"))
    {
        if (!isOpenAICompatibleProtocol(config))
        {
            return protocolMismatchCapability(
                QStringLiteral("OpenAI Responses"),
                QStringLiteral("Google Gemini 原生"));
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("api.openai.com")))
        {
            return {
                true,
                WebProviderKind::OpenAIResponses,
                QStringLiteral("OpenAI 官方联网能力可用"),
                QStringLiteral("当前 OpenAI 官方配置支持联网模式。")
            };
        }

        return {
            true,
            WebProviderKind::OpenAIResponses,
            QStringLiteral("OpenAI Responses 联网可用（中转/自定义）"),
            QStringLiteral("已手动指定 OpenAI Responses 联网（/v1/responses + web_search），建议先在设置页点击“测试联网模式”确认中转站支持透传。")
        };
    }

    if (mode == QStringLiteral("openai-chat-search"))
    {
        if (!isOpenAICompatibleProtocol(config))
        {
            return protocolMismatchCapability(
                QStringLiteral("OpenAI Chat 兼容"),
                QStringLiteral("Google Gemini 原生"));
        }

        return {
            true,
            WebProviderKind::OpenAIChatSearch,
            QStringLiteral("OpenAI Chat 兼容联网已启用（老中转/搜索模型）"),
            QStringLiteral("已手动指定 OpenAI Chat 兼容联网（/chat/completions + web_search_options），适用于不支持 /v1/responses 的中转站或搜索模型，建议先点击测试确认。")
        };
    }

    if (mode == QStringLiteral("deepseek-web-tools"))
    {
        if (!isOpenAICompatibleProtocol(config))
        {
            return protocolMismatchCapability(
                QStringLiteral("DeepSeek 原生联网"),
                QStringLiteral("Google Gemini 原生"));
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("api.deepseek.com")))
        {
            return {
                true,
                WebProviderKind::DeepSeekWebTools,
                QStringLiteral("DeepSeek 官方联网能力可用"),
                QStringLiteral("当前 DeepSeek 官方配置支持原生联网工具（deepseek_web_search），建议使用支持 Web Tools 的新版模型（如 deepseek-flash）。")
            };
        }

        return {
            true,
            WebProviderKind::DeepSeekWebTools,
            QStringLiteral("DeepSeek 原生联网已启用（中转/自定义）"),
            QStringLiteral("已手动指定 DeepSeek 原生联网（deepseek_web_search），中转站需直连或透传官方 Web Tools，建议先点击测试确认。")
        };
    }

    if (mode == QStringLiteral("gemini-grounding"))
    {
        if (!isGeminiProtocol(config))
        {
            return {
                false,
                WebProviderKind::None,
                QStringLiteral("联网协议与当前 AI 协议冲突"),
                QStringLiteral("当前联网模式手动选择了 Gemini 原生联网，但当前 AI 接口协议为 OpenAI Compatible。若使用 Gemini 原生反代，请在 Google 分组配置或将 Custom 接口协议切为“Google Gemini 原生”；否则请在联网页改回“自动”或匹配的协议。")
            };
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("generativelanguage.googleapis.com")))
        {
            return {
                true,
                WebProviderKind::GeminiGrounding,
                QStringLiteral("Gemini 官方联网能力可用"),
                QStringLiteral("当前 Gemini 官方配置支持联网模式。")
            };
        }

        return {
            true,
            WebProviderKind::GeminiGrounding,
            QStringLiteral("Gemini 原生联网可用（中转/自定义）"),
            QStringLiteral("已指定 Gemini 原生联网（google_search），当前为自定义/中转地址，建议先在设置页点击“测试联网模式”确认。")
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

namespace WebCapabilityDetector
{
QString providerKindName(WebProviderKind kind)
{
    switch (kind)
    {
    case WebProviderKind::OpenAIResponses:
        return QStringLiteral("OpenAI Web Search (Responses)");
    case WebProviderKind::OpenAIChatSearch:
        return QStringLiteral("OpenAI Web Search (Chat Completions)");
    case WebProviderKind::GeminiGrounding:
        return QStringLiteral("Gemini Google Search");
    case WebProviderKind::DeepSeekWebTools:
        return QStringLiteral("DeepSeek Web Search");
    case WebProviderKind::None:
        return QStringLiteral("None");
    }

    return QStringLiteral("None");
}

QString normalizedProviderMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("openai") || normalized == QStringLiteral("openai-responses"))
    {
        return QStringLiteral("openai-responses");
    }
    if (normalized == QStringLiteral("openai-chat-search"))
    {
        return QStringLiteral("openai-chat-search");
    }
    if (normalized == QStringLiteral("gemini") || normalized == QStringLiteral("gemini-grounding"))
    {
        return QStringLiteral("gemini-grounding");
    }
    if (normalized == QStringLiteral("deepseek") || normalized == QStringLiteral("deepseek-web-tools"))
    {
        return QStringLiteral("deepseek-web-tools");
    }
    return QStringLiteral("auto");
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

    const QString mode = normalizedProviderMode(webConfig.providerMode);
    if (mode != QStringLiteral("auto"))
    {
        return detectManualMode(config, mode);
    }

    const QString provider = normalizedProviderName(config.provider);

    if (isGeminiProtocol(config))
    {
        if (!hasCompleteAIConfig(config))
        {
            return missingAIConfigCapability();
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("generativelanguage.googleapis.com")))
        {
            return {
                true,
                WebProviderKind::GeminiGrounding,
                QStringLiteral("Gemini 官方联网能力可用"),
                QStringLiteral("当前 Gemini 官方配置支持联网模式。")
            };
        }

        return {
            true,
            WebProviderKind::GeminiGrounding,
            QStringLiteral("Gemini 协议联网可用（中转/自定义）"),
            QStringLiteral("当前为 Gemini 原生协议自定义/中转地址，将尝试发送 Google Search 联网参数，建议先在设置页点击“测试联网模式”确认。")
        };
    }

    if (provider == QStringLiteral("openai") && isOpenAICompatibleProtocol(config))
    {
        if (!hasCompleteAIConfig(config))
        {
            return missingAIConfigCapability();
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("api.openai.com")))
        {
            return {
                true,
                WebProviderKind::OpenAIResponses,
                QStringLiteral("OpenAI 官方联网能力可用"),
                QStringLiteral("当前 OpenAI 官方配置支持联网模式。")
            };
        }

        return {
            false,
            WebProviderKind::None,
            QStringLiteral("OpenAI 中转地址需手动选择联网协议"),
            QStringLiteral("当前 Base URL 不是官方 api.openai.com，自动模式无法确认中转站支持的联网协议。请在设置页“联网模式”中手动选择 OpenAI Responses 或 OpenAI Chat 兼容模式，并点击测试。")
        };
    }

    if (provider == QStringLiteral("deepseek") && isOpenAICompatibleProtocol(config))
    {
        if (!hasCompleteAIConfig(config))
        {
            return missingAIConfigCapability();
        }

        if (isOfficialHost(config.baseUrl, QStringLiteral("api.deepseek.com")))
        {
            return {
                true,
                WebProviderKind::DeepSeekWebTools,
                QStringLiteral("DeepSeek 官方联网能力可用"),
                QStringLiteral("当前 DeepSeek 官方配置支持原生联网工具（deepseek_web_search），建议使用支持 Web Tools 的新版模型（如 deepseek-flash）。")
            };
        }

        return {
            false,
            WebProviderKind::None,
            QStringLiteral("DeepSeek 中转地址需手动选择联网协议"),
            QStringLiteral("当前 Base URL 不是官方 api.deepseek.com，第三方托管渠道可能不含 DeepSeek 原生搜索。若中转站透传官方 Web Tools，请在设置页“联网模式”中手动选择 DeepSeek 原生联网并点击测试。")
        };
    }

    if (provider == QStringLiteral("custom"))
    {
        return {
            false,
            WebProviderKind::None,
            QStringLiteral("自定义 Provider 需手动指定联网协议"),
            QStringLiteral("自定义 OpenAI 兼容接口在自动模式下无法确认联网协议。若中转站支持原生联网，请在设置页“联网模式”中手动选择 OpenAI Responses、OpenAI Chat 兼容模式或 DeepSeek 原生联网，并点击测试。")
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
