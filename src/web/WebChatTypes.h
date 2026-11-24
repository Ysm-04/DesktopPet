#pragma once

#include "../ai/LLMTypes.h"
#include "../config/AIConfig.h"
#include "WebConfig.h"

#include <QString>
#include <QVector>

enum class WebProviderKind
{
    None,
    OpenAIResponses,
    OpenAIChatSearch,
    GeminiGrounding,
    DeepSeekWebTools,
};

struct WebCapability
{
    bool supported = false;
    WebProviderKind kind = WebProviderKind::None;
    QString statusText;
    QString userMessage;
};

struct WebCitation
{
    int index = 0;
    QString title;
    QString url;
    QString sourceName;
};

struct WebChatRequest
{
    ChatRequest chatRequest;
    AIConfig aiConfig;
    WebConfig webConfig;
};

struct WebChatResponse
{
    bool success = false;
    bool usedWeb = false;
    QString content;
    QString errorMessage;
    QVector<WebCitation> citations;
    int httpStatus = 0;
};
