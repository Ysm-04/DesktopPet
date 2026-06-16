#include "WebChatManager.h"

#include "../ai/AIDiagnostics.h"
#include "../ai/AIProviderFactory.h"
#include "../util/Logger.h"
#include "WebCapabilityDetector.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <utility>

namespace
{
QString normalizedGeminiModel(QString model)
{
    model = model.trimmed();
    if (model.startsWith(QStringLiteral("models/")))
    {
        model.remove(0, QStringLiteral("models/").size());
    }
    return model;
}

QString geminiRole(const QString &role)
{
    if (role == QStringLiteral("assistant") || role == QStringLiteral("model"))
    {
        return QStringLiteral("model");
    }
    return QStringLiteral("user");
}

QJsonObject textPart(const QString &text)
{
    QJsonObject part;
    part.insert(QStringLiteral("text"), text);
    return part;
}

QString openAIResponsesUrl(const AIConfig &config)
{
    QString baseUrl = config.baseUrl.trimmed();
    while (baseUrl.endsWith(QLatin1Char('/')))
    {
        baseUrl.chop(1);
    }

    if (baseUrl.endsWith(QStringLiteral("/v1")))
    {
        return baseUrl + QStringLiteral("/responses");
    }

    return baseUrl + QStringLiteral("/v1/responses");
}

QUrl geminiRequestUrl(const AIConfig &config)
{
    QString baseUrl = config.baseUrl.trimmed();
    QString path = config.path.trimmed();
    if (path.isEmpty())
    {
        path = QStringLiteral("/v1beta/models/{model}:generateContent");
    }

    while (baseUrl.endsWith(QLatin1Char('/')))
    {
        baseUrl.chop(1);
    }

    if (!path.startsWith(QLatin1Char('/')))
    {
        path.prepend(QLatin1Char('/'));
    }

    if (path.contains(QStringLiteral(":streamGenerateContent")))
    {
        path.replace(QStringLiteral(":streamGenerateContent"), QStringLiteral(":generateContent"));
    }

    const QString model = QString::fromUtf8(QUrl::toPercentEncoding(normalizedGeminiModel(config.model)));
    path.replace(QStringLiteral("{model}"), model);
    return QUrl(baseUrl + path);
}

QJsonObject openAIInputMessage(const ChatMessage &message)
{
    QJsonObject item;
    item.insert(QStringLiteral("role"), message.role == QStringLiteral("assistant") ? QStringLiteral("assistant") : QStringLiteral("user"));
    item.insert(QStringLiteral("content"), message.content);
    return item;
}

QJsonObject openAIPayload(const ChatRequest &request, const AIConfig &config)
{
    QStringList instructions;
    QJsonArray input;
    for (const ChatMessage &message : request.messages)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            instructions.append(content);
            continue;
        }

        input.append(openAIInputMessage(message));
    }

    QJsonArray tools;
    QJsonObject webSearchTool;
    webSearchTool.insert(QStringLiteral("type"), QStringLiteral("web_search"));
    tools.append(webSearchTool);

    QJsonArray include;
    include.append(QStringLiteral("web_search_call.action.sources"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), config.model);
    payload.insert(QStringLiteral("input"), input);
    payload.insert(QStringLiteral("tools"), tools);
    payload.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    payload.insert(QStringLiteral("include"), include);
    payload.insert(QStringLiteral("temperature"), config.temperature);
    payload.insert(QStringLiteral("max_output_tokens"), config.maxTokens);
    if (!instructions.isEmpty())
    {
        payload.insert(QStringLiteral("instructions"), instructions.join(QStringLiteral("\n\n")));
    }
    return payload;
}

QJsonObject geminiPayload(const ChatRequest &request, const AIConfig &config)
{
    QStringList systemParts;
    QJsonArray contents;

    for (const ChatMessage &message : request.messages)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            systemParts.append(content);
            continue;
        }

        QJsonArray parts;
        parts.append(textPart(content));

        QJsonObject item;
        item.insert(QStringLiteral("role"), geminiRole(message.role));
        item.insert(QStringLiteral("parts"), parts);
        contents.append(item);
    }

    QJsonArray tools;
    QJsonObject googleSearchTool;
    googleSearchTool.insert(QStringLiteral("google_search"), QJsonObject());
    tools.append(googleSearchTool);

    QJsonObject payload;
    payload.insert(QStringLiteral("contents"), contents);
    payload.insert(QStringLiteral("tools"), tools);

    if (!systemParts.isEmpty())
    {
        QJsonArray parts;
        parts.append(textPart(systemParts.join(QStringLiteral("\n\n"))));

        QJsonObject systemInstruction;
        systemInstruction.insert(QStringLiteral("parts"), parts);
        payload.insert(QStringLiteral("systemInstruction"), systemInstruction);
    }

    QJsonObject generationConfig;
    generationConfig.insert(QStringLiteral("temperature"), config.temperature);
    generationConfig.insert(QStringLiteral("maxOutputTokens"), config.maxTokens);
    payload.insert(QStringLiteral("generationConfig"), generationConfig);
    return payload;
}

QString textFromGeminiCandidate(const QJsonObject &candidate)
{
    const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
    const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();

    QString result;
    for (const QJsonValue &partValue : parts)
    {
        if (!partValue.isObject())
        {
            continue;
        }

        const QString text = partValue.toObject().value(QStringLiteral("text")).toString();
        if (!text.isEmpty())
        {
            result += text;
        }
    }

    return result;
}

void appendCitation(QVector<WebCitation> *citations, QSet<QString> *seenUrls, QString title, QString url, const QString &sourceName)
{
    if (citations == nullptr || seenUrls == nullptr)
    {
        return;
    }

    title = title.trimmed();
    url = url.trimmed();
    if (url.isEmpty() || seenUrls->contains(url))
    {
        return;
    }

    WebCitation citation;
    citation.index = citations->size() + 1;
    citation.title = title.isEmpty() ? url : title;
    citation.url = url;
    citation.sourceName = sourceName;
    citations->append(citation);
    seenUrls->insert(url);
}

void collectOpenAIAnnotations(
    const QJsonArray &annotations,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls)
{
    for (const QJsonValue &annotationValue : annotations)
    {
        if (!annotationValue.isObject())
        {
            continue;
        }

        const QJsonObject annotation = annotationValue.toObject();
        const QString type = annotation.value(QStringLiteral("type")).toString();
        if (type != QStringLiteral("url_citation"))
        {
            continue;
        }

        appendCitation(
            citations,
            seenUrls,
            annotation.value(QStringLiteral("title")).toString(),
            annotation.value(QStringLiteral("url")).toString(),
            QStringLiteral("OpenAI Web Search"));
    }
}

void collectOpenAISourceObjects(
    const QJsonValue &value,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls)
{
    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
        {
            collectOpenAISourceObjects(item, citations, seenUrls);
        }
        return;
    }

    if (!value.isObject())
    {
        return;
    }

    const QJsonObject object = value.toObject();
    const auto appendSourceArray = [&object, citations, seenUrls](const QString &fieldName) {
        const QJsonArray sources = object.value(fieldName).toArray();
        for (const QJsonValue &sourceValue : sources)
        {
            if (!sourceValue.isObject())
            {
                continue;
            }

            const QJsonObject source = sourceValue.toObject();
            QString url = source.value(QStringLiteral("url")).toString().trimmed();
            if (url.isEmpty())
            {
                url = source.value(QStringLiteral("uri")).toString().trimmed();
            }

            appendCitation(
                citations,
                seenUrls,
                source.value(QStringLiteral("title")).toString(),
                url,
                QStringLiteral("OpenAI Web Search"));
        }
    };

    appendSourceArray(QStringLiteral("sources"));
    appendSourceArray(QStringLiteral("results"));

    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
    {
        collectOpenAISourceObjects(iterator.value(), citations, seenUrls);
    }
}

void collectGeminiGroundingCitations(
    const QJsonObject &candidate,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls)
{
    const QJsonObject grounding = candidate.value(QStringLiteral("groundingMetadata")).toObject();
    const QJsonArray chunks = grounding.value(QStringLiteral("groundingChunks")).toArray();
    for (const QJsonValue &chunkValue : chunks)
    {
        if (!chunkValue.isObject())
        {
            continue;
        }

        const QJsonObject web = chunkValue.toObject().value(QStringLiteral("web")).toObject();
        appendCitation(
            citations,
            seenUrls,
            web.value(QStringLiteral("title")).toString(),
            web.value(QStringLiteral("uri")).toString(),
            QStringLiteral("Google Search"));
    }

    const QJsonObject citationMetadata = candidate.value(QStringLiteral("citationMetadata")).toObject();
    const QJsonArray citationSources = citationMetadata.value(QStringLiteral("citationSources")).toArray();
    for (const QJsonValue &sourceValue : citationSources)
    {
        if (!sourceValue.isObject())
        {
            continue;
        }

        const QJsonObject source = sourceValue.toObject();
        appendCitation(
            citations,
            seenUrls,
            source.value(QStringLiteral("title")).toString(),
            source.value(QStringLiteral("uri")).toString(),
            QStringLiteral("Google Search"));
    }
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

    if (capability.kind == WebProviderKind::GeminiGrounding)
    {
        startGeminiGrounding({request.chatRequest, runtimeConfig, webConfig});
        return;
    }

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
    const QUrl url(openAIResponsesUrl(request.aiConfig));
    const QByteArray payload = QJsonDocument(openAIPayload(request.chatRequest, request.aiConfig)).toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size(), QByteArray("Bearer ") + request.aiConfig.apiKey.toUtf8());
}

void WebChatManager::startGeminiGrounding(const WebChatRequest &request)
{
    const QUrl url = geminiRequestUrl(request.aiConfig);
    const QByteArray payload = QJsonDocument(geminiPayload(request.chatRequest, request.aiConfig)).toJson(QJsonDocument::Compact);
    startRequest(url, payload, request.chatRequest.messages.size());
}

void WebChatManager::startRequest(const QUrl &url, const QByteArray &payload, int messageCount, const QByteArray &authorizationHeader)
{
    if (!url.isValid())
    {
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
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        Logger::warning(QStringLiteral("Web chat OpenAI request network error: %1 qtError=%2 errorString=\"%3\" httpStatus=%4 bodySummary=\"%5\"")
            .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
            .arg(static_cast<int>(reply->error()))
            .arg(AIDiagnostics::oneLine(reply->errorString()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, false, {}, bodyError.isEmpty() ? reply->errorString() : bodyError, {}, httpStatus};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        Logger::warning(QStringLiteral("Web chat OpenAI response JSON parse failed: %1 parseError=\"%2\" bodySummary=\"%3\"")
            .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
            .arg(AIDiagnostics::oneLine(parseError.errorString()))
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, false, {}, QStringLiteral("联网响应不是有效 JSON。"), {}, httpStatus};
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("error")))
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        return {false, false, {}, bodyError.isEmpty() ? QStringLiteral("联网响应返回错误。") : bodyError, {}, httpStatus};
    }

    QString content = root.value(QStringLiteral("output_text")).toString();
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;
    bool usedWeb = false;

    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &outputValue : output)
    {
        if (!outputValue.isObject())
        {
            continue;
        }

        const QJsonObject outputItem = outputValue.toObject();
        const QString type = outputItem.value(QStringLiteral("type")).toString();
        if (type.contains(QStringLiteral("web_search"), Qt::CaseInsensitive))
        {
            usedWeb = true;
            collectOpenAISourceObjects(outputItem, &citations, &seenUrls);
        }

        const QJsonArray contentItems = outputItem.value(QStringLiteral("content")).toArray();
        for (const QJsonValue &contentValue : contentItems)
        {
            if (!contentValue.isObject())
            {
                continue;
            }

            const QJsonObject contentItem = contentValue.toObject();
            const QString text = contentItem.value(QStringLiteral("text")).toString();
            if (content.trimmed().isEmpty() && !text.isEmpty())
            {
                content += text;
            }
            collectOpenAIAnnotations(contentItem.value(QStringLiteral("annotations")).toArray(), &citations, &seenUrls);
        }
    }

    usedWeb = usedWeb || !citations.isEmpty();
    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("联网响应内容为空。"), citations, httpStatus};
    }

    Logger::info(QStringLiteral("Web chat OpenAI request completed: %1 httpStatus=%2 responseChars=%3 citations=%4 usedWeb=%5")
        .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
        .arg(httpStatus)
        .arg(QString::number(content.size()))
        .arg(QString::number(citations.size()))
        .arg(usedWeb ? QStringLiteral("true") : QStringLiteral("false")));

    return {true, usedWeb, content, {}, citations, httpStatus};
}

WebChatResponse WebChatManager::parseGeminiResponse(QNetworkReply *reply, const QByteArray &body) const
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        Logger::warning(QStringLiteral("Web chat Gemini request network error: %1 qtError=%2 errorString=\"%3\" httpStatus=%4 bodySummary=\"%5\"")
            .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
            .arg(static_cast<int>(reply->error()))
            .arg(AIDiagnostics::oneLine(reply->errorString()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, false, {}, bodyError.isEmpty() ? reply->errorString() : bodyError, {}, httpStatus};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        Logger::warning(QStringLiteral("Web chat Gemini response JSON parse failed: %1 parseError=\"%2\" bodySummary=\"%3\"")
            .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
            .arg(AIDiagnostics::oneLine(parseError.errorString()))
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, false, {}, QStringLiteral("联网响应不是有效 JSON。"), {}, httpStatus};
    }

    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("error")))
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        return {false, false, {}, bodyError.isEmpty() ? QStringLiteral("联网响应返回错误。") : bodyError, {}, httpStatus};
    }

    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty() || !candidates.first().isObject())
    {
        return {false, false, {}, QStringLiteral("Gemini 联网响应没有候选结果。"), {}, httpStatus};
    }

    const QJsonObject candidate = candidates.first().toObject();
    const QString content = textFromGeminiCandidate(candidate);
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;
    collectGeminiGroundingCitations(candidate, &citations, &seenUrls);

    const bool usedWeb = candidate.contains(QStringLiteral("groundingMetadata")) || !citations.isEmpty();
    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("Gemini 联网响应内容为空。"), citations, httpStatus};
    }

    Logger::info(QStringLiteral("Web chat Gemini request completed: %1 httpStatus=%2 responseChars=%3 citations=%4 usedWeb=%5")
        .arg(AIDiagnostics::diagnosticContext(m_currentConfig, reply->request().url()))
        .arg(httpStatus)
        .arg(QString::number(content.size()))
        .arg(QString::number(citations.size()))
        .arg(usedWeb ? QStringLiteral("true") : QStringLiteral("false")));

    return {true, usedWeb, content, {}, citations, httpStatus};
}
