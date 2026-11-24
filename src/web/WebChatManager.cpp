#include "WebChatManager.h"

#include "../ai/AIDiagnostics.h"
#include "../ai/AIProviderFactory.h"
#include "../util/Logger.h"
#include "WebCapabilityDetector.h"
#include "WebProtocolCodec.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtGlobal>

#include <utility>

namespace
{
using BodyParserFn = WebChatResponse (*)(const QByteArray &, int, QString *);

WebChatResponse parseProviderResponse(
    const AIConfig &config,
    QNetworkReply *reply,
    const QByteArray &body,
    const QString &protocolLabel,
    BodyParserFn parser)
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString context = AIDiagnostics::diagnosticContext(config, reply->request().url());

    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        Logger::warning(QStringLiteral("Web chat %1 request network error: %2 qtError=%3 errorString=\"%4\" httpStatus=%5 bodySummary=\"%6\"")
            .arg(protocolLabel, context)
            .arg(static_cast<int>(reply->error()))
            .arg(AIDiagnostics::oneLine(reply->errorString()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, false, {}, bodyError.isEmpty() ? reply->errorString() : bodyError, {}, httpStatus};
    }

    QString jsonParseError;
    const WebChatResponse response = parser(body, httpStatus, &jsonParseError);
    if (!jsonParseError.isEmpty())
    {
        Logger::warning(QStringLiteral("Web chat %1 response JSON parse failed: %2 parseError=\"%3\" bodySummary=\"%4\"")
            .arg(protocolLabel, context, AIDiagnostics::oneLine(jsonParseError), AIDiagnostics::responseBodySummary(body)));
        return response;
    }

    if (!response.success)
    {
        Logger::warning(QStringLiteral("Web chat %1 response failed: %2 httpStatus=%3 error=\"%4\"")
            .arg(protocolLabel, context)
            .arg(httpStatus)
            .arg(AIDiagnostics::safeTextSummary(response.errorMessage)));
        return response;
    }

    Logger::info(QStringLiteral("Web chat %1 request completed: %2 httpStatus=%3 responseChars=%4 citations=%5 usedWeb=%6")
        .arg(protocolLabel, context)
        .arg(httpStatus)
        .arg(QString::number(response.content.size()))
        .arg(QString::number(response.citations.size()))
        .arg(response.usedWeb ? QStringLiteral("true") : QStringLiteral("false")));

    return response;
}
}

WebChatManager::WebChatManager()
{
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]() {
        finishWithError(QStringLiteral("联网请求超时。"));
    });
}

WebChatManager::~WebChatManager()
{
    cancel();
}

bool WebChatManager::isBusy() const
{
    return !m_currentReply.isNull();
}

void WebChatManager::sendWebChat(const WebChatRequest &request, WebChatCallback callback)
{
    if (isBusy())
    {
        if (callback)
        {
            callback({false, false, {}, QStringLiteral("联网请求正在进行，请稍后。"), {}, 0});
        }
        return;
    }

    m_callback = std::move(callback);
    AIConfig runtimeConfig = request.aiConfig;
    WebConfig webConfig = request.webConfig;
    const WebCapability capability = WebCapabilityDetector::detect(runtimeConfig, webConfig);
    if (!capability.supported)
    {
        invokeCallback({false, false, {}, capability.userMessage, {}, 0});
        return;
    }

    QString errorMessage;
    if (!AIProviderFactory::prepareRuntimeConfig(runtimeConfig, &errorMessage))
    {
        invokeCallback({false, false, {}, errorMessage, {}, 0});
        return;
    }
    runtimeConfig.timeoutMs = qBound(3000, webConfig.timeoutMs, 120000);

    if (request.chatRequest.messages.isEmpty())
    {
        invokeCallback({false, false, {}, QStringLiteral("联网请求内容为空。"), {}, 0});
        return;
    }

    m_currentConfig = runtimeConfig;
    m_currentKind = capability.kind;

    if (capability.kind == WebProviderKind::OpenAIResponses)
    {
        startOpenAIResponses({request.chatRequest, runtimeConfig, webConfig});
        return;
    }

    if (capability.kind == WebProviderKind::OpenAIChatSearch)
    {
        startOpenAIChatSearch({request.chatRequest, runtimeConfig, webConfig});
        return;
    }

    if (capability.kind == WebProviderKind::DeepSeekWebTools)
    {
        startDeepSeekWebTools({request.chatRequest, runtimeConfig, webConfig});
        return;
    }

    if (capability.kind == WebProviderKind::GeminiGrounding)
    {
        startGeminiGrounding({request.chatRequest, runtimeConfig, webConfig});
        return;
    }

    m_currentKind = WebProviderKind::None;
    invokeCallback({false, false, {}, QStringLiteral("当前 AI 配置没有可用的原生联网功能。"), {}, 0});
}

void WebChatManager::cancel()
{
    m_callback = nullptr;
    QPointer<QNetworkReply> reply = m_currentReply;
    if (!reply.isNull())
    {
        Logger::info(QStringLiteral("Web chat request canceled: %1")
            .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url())));
    }
    clearReply();

    if (!reply.isNull())
    {
        reply->abort();
    }
}

void WebChatManager::startOpenAIResponses(const WebChatRequest &request)
{
    const QUrl url = WebProtocolCodec::buildOpenAIResponsesUrl(request.aiConfig);
    const QByteArray payload = QJsonDocument(
        WebProtocolCodec::buildOpenAIResponsesPayload(request.chatRequest, request.aiConfig))
        .toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size(), QByteArray("Bearer ") + request.aiConfig.apiKey.toUtf8());
}

void WebChatManager::startOpenAIChatSearch(const WebChatRequest &request)
{
    const QUrl url = WebProtocolCodec::buildOpenAIChatUrl(request.aiConfig);
    const QByteArray payload = QJsonDocument(
        WebProtocolCodec::buildOpenAIChatSearchPayload(request.chatRequest, request.aiConfig))
        .toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size(), QByteArray("Bearer ") + request.aiConfig.apiKey.toUtf8());
}

void WebChatManager::startDeepSeekWebTools(const WebChatRequest &request)
{
    const QUrl url = WebProtocolCodec::buildOpenAIChatUrl(request.aiConfig);
    const QByteArray payload = QJsonDocument(
        WebProtocolCodec::buildDeepSeekWebToolsPayload(request.chatRequest, request.aiConfig))
        .toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size(), QByteArray("Bearer ") + request.aiConfig.apiKey.toUtf8());
}

void WebChatManager::startGeminiGrounding(const WebChatRequest &request)
{
    const QUrl url = WebProtocolCodec::buildGeminiGroundingUrl(request.aiConfig);
    const QByteArray payload = QJsonDocument(
        WebProtocolCodec::buildGeminiGroundingPayload(request.chatRequest, request.aiConfig))
        .toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size());
}

void WebChatManager::startRequest(const QUrl &url, const QByteArray &payload, int messageCount, const QByteArray &authorizationHeader)
{
    if (!url.isValid())
    {
        clearReply();
        invokeCallback({false, false, {}, QStringLiteral("联网请求 URL 无效。"), {}, 0});
        return;
    }

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!authorizationHeader.isEmpty())
    {
        networkRequest.setRawHeader("Authorization", authorizationHeader);
    }
    if (m_currentKind == WebProviderKind::GeminiGrounding)
    {
        networkRequest.setRawHeader("x-goog-api-key", m_currentConfig.apiKey.toUtf8());
    }

    Logger::info(QStringLiteral("Web chat request started: %1 kind=%2 messageCount=%3 payloadBytes=%4")
        .arg(AIDiagnostics::diagnosticContext(m_currentConfig, url))
        .arg(WebCapabilityDetector::providerKindName(m_currentKind))
        .arg(QString::number(messageCount))
        .arg(QString::number(payload.size())));

    m_currentReply = m_networkManager.post(networkRequest, payload);
    m_replyFinishedConnection = QObject::connect(m_currentReply, &QNetworkReply::finished, [this]() {
        finishReply();
    });
    m_timeoutTimer.start(qBound(3000, m_currentConfig.timeoutMs, 120000));
}

void WebChatManager::finishReply()
{
    if (m_currentReply.isNull())
    {
        return;
    }

    QNetworkReply *reply = m_currentReply;
    const QByteArray body = reply->readAll();
    WebChatResponse response;
    if (m_currentKind == WebProviderKind::OpenAIResponses)
    {
        response = parseOpenAIResponse(reply, body);
    }
    else if (m_currentKind == WebProviderKind::OpenAIChatSearch)
    {
        response = parseOpenAIChatSearchResponse(reply, body);
    }
    else if (m_currentKind == WebProviderKind::DeepSeekWebTools)
    {
        response = parseDeepSeekWebToolsResponse(reply, body);
    }
    else if (m_currentKind == WebProviderKind::GeminiGrounding)
    {
        response = parseGeminiResponse(reply, body);
    }
    else
    {
        response = {false, false, {}, QStringLiteral("未知联网 Provider。"), {}, 0};
    }

    clearReply();
    invokeCallback(response);
}

void WebChatManager::finishWithError(const QString &message, int httpStatus)
{
    QPointer<QNetworkReply> reply = m_currentReply;
    const QUrl url = reply.isNull() ? QUrl() : reply->request().url();
    Logger::warning(QStringLiteral("Web chat request finished with error: %1 httpStatus=%2 error=\"%3\"")
        .arg(AIDiagnostics::diagnosticContext(m_currentConfig, url))
        .arg(httpStatus)
        .arg(AIDiagnostics::safeTextSummary(message)));

    clearReply();
    if (!reply.isNull())
    {
        reply->abort();
    }

    invokeCallback({false, false, {}, message, {}, httpStatus});
}

void WebChatManager::invokeCallback(const WebChatResponse &response)
{
    if (!m_callback)
    {
        return;
    }

    const WebChatCallback callback = std::move(m_callback);
    m_callback = nullptr;
    callback(response);
}

void WebChatManager::clearReply()
{
    m_timeoutTimer.stop();
    if (m_replyFinishedConnection)
    {
        QObject::disconnect(m_replyFinishedConnection);
        m_replyFinishedConnection = {};
    }

    if (!m_currentReply.isNull())
    {
        m_currentReply->deleteLater();
        m_currentReply.clear();
    }
    m_currentKind = WebProviderKind::None;
}

WebChatResponse WebChatManager::parseOpenAIResponse(QNetworkReply *reply, const QByteArray &body) const
{
    return parseProviderResponse(
        m_currentConfig,
        reply,
        body,
        QStringLiteral("OpenAI Responses"),
        &WebProtocolCodec::parseOpenAIResponsesBody);
}

WebChatResponse WebChatManager::parseOpenAIChatSearchResponse(QNetworkReply *reply, const QByteArray &body) const
{
    return parseProviderResponse(
        m_currentConfig,
        reply,
        body,
        QStringLiteral("OpenAI Chat Search"),
        &WebProtocolCodec::parseOpenAIChatSearchBody);
}

WebChatResponse WebChatManager::parseDeepSeekWebToolsResponse(QNetworkReply *reply, const QByteArray &body) const
{
    return parseProviderResponse(
        m_currentConfig,
        reply,
        body,
        QStringLiteral("DeepSeek Web Tools"),
        &WebProtocolCodec::parseDeepSeekWebToolsBody);
}

WebChatResponse WebChatManager::parseGeminiResponse(QNetworkReply *reply, const QByteArray &body) const
{
    return parseProviderResponse(
        m_currentConfig,
        reply,
        body,
        QStringLiteral("Gemini"),
        &WebProtocolCodec::parseGeminiGroundingBody);
}
