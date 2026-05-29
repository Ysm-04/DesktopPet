#pragma once

#include "../config/AIConfig.h"
#include "LLMProvider.h"

#include <memory>

class AIProviderFactory
{
public:
    static bool prepareRuntimeConfig(AIConfig &config, QString *errorMessage);
    static std::unique_ptr<LLMProvider> createProvider(const AIConfig &config);
};
