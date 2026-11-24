#pragma once

#include "../ai/LLMTypes.h"
#include "../config/AIConfig.h"
#include "WebChatTypes.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUrl>

namespace WebProtocolCodec
{
QUrl buildOpenAIResponsesUrl(const AIConfig &config);
QUrl buildOpenAIChatUrl(const AIConfig &config);
QUrl buildGeminiGroundingUrl(const AIConfig &config);

QJsonObject buildOpenAIResponsesPayload(const ChatRequest &request, const AIConfig &config);
QJsonObject buildOpenAIChatSearchPayload(const ChatRequest &request, const AIConfig &config);
QJsonObject buildDeepSeekWebToolsPayload(const ChatRequest &request, const AIConfig &config);
QJsonObject buildGeminiGroundingPayload(const ChatRequest &request, const AIConfig &config);

WebChatResponse parseOpenAIResponsesBody(const QByteArray &body, int httpStatus, QString *jsonParseError = nullptr);
WebChatResponse parseOpenAIChatSearchBody(const QByteArray &body, int httpStatus, QString *jsonParseError = nullptr);
WebChatResponse parseDeepSeekWebToolsBody(const QByteArray &body, int httpStatus, QString *jsonParseError = nullptr);
WebChatResponse parseGeminiGroundingBody(const QByteArray &body, int httpStatus, QString *jsonParseError = nullptr);
}
