#include "AIProviderFactory.h"

#include "../config/SecretStore.h"
#include "GoogleGeminiProvider.h"
#include "OpenAICompatibleProvider.h"

namespace
{
bool populateRuntimeApiKey(AIConfig &config, QString *errorMessage)
{
    if (!config.apiKey.trimmed().isEmpty())
    {
        return true;
    }

    if (config.apiKeyStorage == QStringLiteral("windows-dpapi"))
    {
        if (config.apiKeyEncrypted.trimmed().isEmpty())
        {
            *errorMessage = QStringLiteral("请先在设置里配置 API Key。");
            return false;
        }

        const SecretStore::Result result = SecretStore::unprotectText(config.apiKeyEncrypted);
        if (!result.success)
        {
            *errorMessage = QStringLiteral("API Key 解密失败：") + result.errorMessage;
            return false;
        }

        config.apiKey = result.value;
    }

    if (config.apiKey.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 API Key。");
        return false;
    }

    return true;
}
}

bool AIProviderFactory::prepareRuntimeConfig(AIConfig &config, QString *errorMessage)
{
    const bool supportedProtocol = config.protocol == QStringLiteral("openai-compatible")
        || config.protocol == QStringLiteral("google-generative-language");
    if (!supportedProtocol)
    {
        *errorMessage = QStringLiteral("当前 Provider 协议暂未接入。");
        return false;
    }

    if (!populateRuntimeApiKey(config, errorMessage))
    {
        return false;
    }

    if (config.baseUrl.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 Base URL。");
        return false;
    }

    if (config.model.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 Model。");
        return false;
    }

    return true;
}

std::unique_ptr<LLMProvider> AIProviderFactory::createProvider(const AIConfig &config)
{
    if (config.protocol == QStringLiteral("google-generative-language"))
    {
        return std::make_unique<GoogleGeminiProvider>(config);
    }

    if (config.protocol == QStringLiteral("openai-compatible"))
    {
        return std::make_unique<OpenAICompatibleProvider>(config);
    }

    return nullptr;
}
