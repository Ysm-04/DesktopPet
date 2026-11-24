#include "GoogleGeminiProvider.h"

#include "AIDiagnostics.h"
#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrlQuery>

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

        const QJsonObject partObject = partValue.toObject();
        if (partObject.value(QStringLiteral("thought")).toBool(false))
        {
            continue;
        }

        const QString text = partObject.value(QStringLiteral("text")).toString();
        if (!text.isEmpty())
        {
            result += text;
        }
    }

    return result;
}

QString textFromGeminiResponse(const QJsonObject &root)
{
    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty() || !candidates.first().isObject())
    {
        return {};
    }

    return textFromGeminiCandidate(candidates.first().toObject());
}

QString geminiEmptyResponseReason(const QJsonObject &root)
{
    QStringList details;

    const QJsonObject promptFeedback = root.value(QStringLiteral("promptFeedback")).toObject();
    const QString blockReason = promptFeedback.value(QStringLiteral("blockReason")).toString().trimmed();
    if (!blockReason.isEmpty())
    {
        details.append(QStringLiteral("prompt blocked: ") + blockReason);
    }

    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (!candidates.isEmpty() && candidates.first().isObject())
    {
        const QJsonObject candidate = candidates.first().toObject();
        const QString finishReason = candidate.value(QStringLiteral("finishReason")).toString().trimmed();
        if (!finishReason.isEmpty())
        {
            details.append(QStringLiteral("finishReason=") + finishReason);
        }
    }

    if (!details.isEmpty())
    {
        return details.join(QStringLiteral("; "));
    }

    return QStringLiteral("Gemini response content is empty.");
}
}

GoogleGeminiProvider::GoogleGeminiProvider(const AIConfig &config)
    : m_config(config)
{
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]() {
        finishWithError(QStringLiteral("AI request timed out."));
    });
}

GoogleGeminiProvider::~GoogleGeminiProvider()
{
    cancel();
}

bool GoogleGeminiProvider::isBusy() const
{
    return !m_currentReply.isNull();
}

void GoogleGeminiProvider::sendChatRequest(const ChatRequest &request, ResponseCallback callback)
{
    sendChatRequestInternal(request, false, nullptr, std::move(callback));
}

void GoogleGeminiProvider::sendStreamingChatRequest(
    const ChatRequest &request,
    StreamCallback streamCallback,
    ResponseCallback callback)
{
    sendChatRequestInternal(request, true, std::move(streamCallback), std::move(callback));
}

void GoogleGeminiProvider::sendChatRequestInternal(
    const ChatRequest &request,
    bool stream,
    StreamCallback streamCallback,
    ResponseCallback callback)
{
    if (isBusy())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("AI request is already running."), 0});
        }
        return;
    }

    if (m_config.baseUrl.trimmed().isEmpty())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("Base URL is empty."), 0});
        }
        return;
    }

    if (m_config.model.trimmed().isEmpty())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("Model is empty."), 0});
        }
        return;
    }

    if (m_config.apiKey.trimmed().isEmpty())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("API Key is empty."), 0});
        }
        return;
    }

    m_callback = std::move(callback);
    m_streamCallback = std::move(streamCallback);
    m_streaming = stream;
    m_streamDone = false;
    m_streamBuffer.clear();
    m_streamRawBody.clear();
    m_streamedContent.clear();

    const QUrl url = requestUrl(stream);
    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("x-goog-api-key", m_config.apiKey.toUtf8());
    if (stream)
    {
        networkRequest.setRawHeader("Accept", "text/event-stream");
    }

    const QJsonDocument document(buildPayload(request));
    const QByteArray payload = document.toJson(QJsonDocument::Compact);
    Logger::info(QStringLiteral("AI request started: %1 stream=%2 messageCount=%3 payloadBytes=%4")
        .arg(AIDiagnostics::diagnosticContext(m_config, url))
        .arg(stream ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(QString::number(request.messages.size()))
        .arg(QString::number(payload.size())));

    m_currentReply = m_networkManager.post(networkRequest, payload);
    if (stream)
    {
        m_readyReadConnection = QObject::connect(m_currentReply, &QNetworkReply::readyRead, [this]() {
            handleStreamReadyRead();
        });
    }

    m_replyFinishedConnection = QObject::connect(m_currentReply, &QNetworkReply::finished, [this]() {
        if (m_currentReply.isNull())
        {
            return;
        }

        QNetworkReply *reply = m_currentReply;
        const QByteArray body = reply->readAll();
        ChatResponse response;
        if (m_streaming)
        {
            if (!body.isEmpty())
            {
                m_streamRawBody.append(body);
            }
            if (reply->error() == QNetworkReply::NoError && !body.isEmpty())
            {
                m_streamBuffer.append(body);
                processStreamBuffer();
                if (m_currentReply.isNull())
                {
                    return;
                }
            }
            if (reply->error() == QNetworkReply::NoError && !m_streamBuffer.isEmpty())
            {
                m_streamBuffer.append('\n');
                processStreamBuffer();
                if (m_currentReply.isNull())
                {
                    return;
                }
            }
            response = finishStreamingResponse(reply, body);
        }
        else
        {
            response = parseResponse(reply, body);
        }
        clearReply();

        if (m_callback)
        {
            const ResponseCallback callback = std::move(m_callback);
            m_callback = nullptr;
            callback(response);
        }
    });

    m_timeoutTimer.start(m_config.timeoutMs);
}

void GoogleGeminiProvider::cancel()
{
    m_callback = nullptr;
    QPointer<QNetworkReply> reply = m_currentReply;
    if (!reply.isNull())
    {
        Logger::info(QStringLiteral("AI request canceled: %1")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url())));
    }
    clearReply();

    if (!reply.isNull())
    {
        reply->abort();
    }
}

QJsonObject GoogleGeminiProvider::buildPayload(const ChatRequest &request) const
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

    QJsonObject payload;
    payload.insert(QStringLiteral("contents"), contents);

    if (!systemParts.isEmpty())
    {
        QJsonArray parts;
        parts.append(textPart(systemParts.join(QStringLiteral("\n\n"))));

        QJsonObject systemInstruction;
        systemInstruction.insert(QStringLiteral("parts"), parts);
        payload.insert(QStringLiteral("systemInstruction"), systemInstruction);
    }

    QJsonObject generationConfig;
    generationConfig.insert(QStringLiteral("temperature"), m_config.temperature);
    generationConfig.insert(QStringLiteral("maxOutputTokens"), m_config.maxTokens);
    payload.insert(QStringLiteral("generationConfig"), generationConfig);

    return payload;
}

QUrl GoogleGeminiProvider::requestUrl(bool stream) const
{
    QString baseUrl = m_config.baseUrl.trimmed();
    while (baseUrl.endsWith(QLatin1Char('/')))
    {
        baseUrl.chop(1);
    }

    const QString model = QString::fromUtf8(QUrl::toPercentEncoding(normalizedGeminiModel(m_config.model)));
    QString fullUrl;
    if (baseUrl.contains(QStringLiteral(":generateContent"), Qt::CaseInsensitive)
        || baseUrl.contains(QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive))
    {
        if (stream)
        {
            baseUrl.replace(QStringLiteral(":generateContent"), QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive);
        }
        else
        {
            baseUrl.replace(QStringLiteral(":streamGenerateContent"), QStringLiteral(":generateContent"), Qt::CaseInsensitive);
        }
        baseUrl.replace(QStringLiteral("{model}"), model);
        fullUrl = baseUrl;
    }
    else
    {
        QString path = m_config.path.trimmed();
        if (path.isEmpty())
        {
            path = QStringLiteral("/v1beta/models/{model}:generateContent");
        }

        if (!path.startsWith(QLatin1Char('/')))
        {
            path.prepend(QLatin1Char('/'));
        }

        if (baseUrl.endsWith(QStringLiteral("/v1beta"), Qt::CaseInsensitive)
            && path.startsWith(QStringLiteral("/v1beta/"), Qt::CaseInsensitive))
        {
            baseUrl.chop(7);
        }
        else if (baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive)
            && (path.startsWith(QStringLiteral("/v1/"), Qt::CaseInsensitive)
                || path.startsWith(QStringLiteral("/v1beta/"), Qt::CaseInsensitive)))
        {
            baseUrl.chop(3);
        }

        if (stream && path.contains(QStringLiteral(":generateContent"), Qt::CaseInsensitive))
        {
            path.replace(QStringLiteral(":generateContent"), QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive);
        }
        else if (!stream && path.contains(QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive))
        {
            path.replace(QStringLiteral(":streamGenerateContent"), QStringLiteral(":generateContent"), Qt::CaseInsensitive);
        }

        path.replace(QStringLiteral("{model}"), model);
        fullUrl = baseUrl + path;
    }

    QUrl url(fullUrl);
    if (stream)
    {
        QUrlQuery query(url);
        if (!query.hasQueryItem(QStringLiteral("alt")))
        {
            query.addQueryItem(QStringLiteral("alt"), QStringLiteral("sse"));
        }
        url.setQuery(query);
    }

    return url;
}

ChatResponse GoogleGeminiProvider::parseResponse(QNetworkReply *reply, const QByteArray &body) const
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        Logger::warning(QStringLiteral("AI request network error: %1 qtError=%2 errorString=\"%3\" httpStatus=%4 bodySummary=\"%5\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(static_cast<int>(reply->error()))
            .arg(AIDiagnostics::oneLine(reply->errorString()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));

        if (!bodyError.isEmpty())
        {
            return {false, {}, bodyError, httpStatus};
        }

        return {false, {}, reply->errorString(), httpStatus};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        Logger::warning(QStringLiteral("AI response JSON parse failed: %1 httpStatus=%2 parseError=\"%3\" bodySummary=\"%4\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(AIDiagnostics::oneLine(parseError.errorString()))
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, {}, QStringLiteral("AI response is not valid JSON."), httpStatus};
    }

    const QJsonObject root = document.object();
    if (AIDiagnostics::hasErrorPayload(root))
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        Logger::warning(QStringLiteral("AI response returned error: %1 httpStatus=%2 bodySummary=\"%3\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, {}, bodyError.isEmpty() ? QStringLiteral("AI response returned an error.") : bodyError, httpStatus};
    }

    const QString content = textFromGeminiResponse(root);
    if (content.isEmpty())
    {
        const QString reason = geminiEmptyResponseReason(root);
        Logger::warning(QStringLiteral("AI response content is empty: %1 httpStatus=%2 reason=\"%3\" bodySummary=\"%4\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(AIDiagnostics::safeTextSummary(reason))
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, {}, reason, httpStatus};
    }

    Logger::info(QStringLiteral("AI request completed: %1 httpStatus=%2 responseChars=%3 responseBytes=%4")
        .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
        .arg(httpStatus)
        .arg(QString::number(content.size()))
        .arg(QString::number(body.size())));

    return {true, content, {}, httpStatus};
}

void GoogleGeminiProvider::handleStreamReadyRead()
{
    if (m_currentReply.isNull())
    {
        return;
    }

    const QByteArray chunk = m_currentReply->readAll();
    m_streamRawBody.append(chunk);
    m_streamBuffer.append(chunk);
    processStreamBuffer();
}

void GoogleGeminiProvider::processStreamBuffer()
{
    while (true)
    {
        const int lineEnd = m_streamBuffer.indexOf('\n');
        if (lineEnd < 0)
        {
            return;
        }

        QByteArray line = m_streamBuffer.left(lineEnd);
        m_streamBuffer.remove(0, lineEnd + 1);
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(':') || line.startsWith("event:"))
        {
            continue;
        }

        if (!line.startsWith("data:"))
        {
            continue;
        }

        const QByteArray payload = line.mid(5).trimmed();
        if (!handleStreamPayload(payload))
        {
            return;
        }
    }
}

bool GoogleGeminiProvider::handleStreamPayload(const QByteArray &payload)
{
    if (payload == "[DONE]")
    {
        m_streamDone = true;
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        Logger::warning(QStringLiteral("AI stream chunk JSON parse failed: %1 parseError=\"%2\" chunkBytes=%3")
            .arg(AIDiagnostics::diagnosticContext(m_config, requestUrl(true)))
            .arg(AIDiagnostics::oneLine(parseError.errorString()))
            .arg(QString::number(payload.size())));
        return true;
    }

    const QJsonObject root = document.object();
    if (AIDiagnostics::hasErrorPayload(root))
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(payload);
        finishWithError(bodyError.isEmpty() ? QStringLiteral("AI stream returned an error.") : bodyError);
        return false;
    }

    const QString content = textFromGeminiResponse(root);
    if (content.isEmpty())
    {
        return true;
    }

    m_streamedContent += content;
    if (m_streamCallback)
    {
        m_streamCallback(content);
    }

    return true;
}

ChatResponse GoogleGeminiProvider::finishStreamingResponse(QNetworkReply *reply, const QByteArray &body) const
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray diagnosticBody = m_streamedContent.isEmpty()
        ? (m_streamRawBody.isEmpty() ? body : m_streamRawBody)
        : QByteArray();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(diagnosticBody);
        Logger::warning(QStringLiteral("AI streaming request network error: %1 qtError=%2 errorString=\"%3\" httpStatus=%4 bodySummary=\"%5\" streamedChars=%6")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(static_cast<int>(reply->error()))
            .arg(AIDiagnostics::oneLine(reply->errorString()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(diagnosticBody))
            .arg(QString::number(m_streamedContent.size())));

        if (!bodyError.isEmpty())
        {
            return {false, {}, bodyError, httpStatus};
        }

        return {false, {}, reply->errorString(), httpStatus};
    }

    if (m_streamedContent.isEmpty())
    {
        Logger::warning(QStringLiteral("AI streaming response content is empty: %1 httpStatus=%2 streamDone=%3 residualBytes=%4 bodySummary=\"%5\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(m_streamDone ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(QString::number(m_streamBuffer.size()))
            .arg(AIDiagnostics::responseBodySummary(diagnosticBody)));
        return {false, {}, QStringLiteral("Gemini streaming response content is empty."), httpStatus};
    }

    Logger::info(QStringLiteral("AI streaming request completed: %1 httpStatus=%2 streamDone=%3 responseChars=%4")
        .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
        .arg(httpStatus)
        .arg(m_streamDone ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(QString::number(m_streamedContent.size())));

    return {true, m_streamedContent, {}, httpStatus};
}

void GoogleGeminiProvider::finishWithError(const QString &message, int httpStatus)
{
    QPointer<QNetworkReply> reply = m_currentReply;
    const QUrl url = reply.isNull() ? requestUrl(m_streaming) : reply->request().url();
    Logger::warning(QStringLiteral("AI request finished with error: %1 httpStatus=%2 error=\"%3\"")
        .arg(AIDiagnostics::diagnosticContext(m_config, url))
        .arg(httpStatus)
        .arg(AIDiagnostics::safeTextSummary(message)));

    clearReply();

    if (!reply.isNull())
    {
        reply->abort();
    }

    if (m_callback)
    {
        const ResponseCallback callback = std::move(m_callback);
        m_callback = nullptr;
        callback({false, {}, message, httpStatus});
    }
}

void GoogleGeminiProvider::clearReply()
{
    m_timeoutTimer.stop();
    if (m_replyFinishedConnection)
    {
        QObject::disconnect(m_replyFinishedConnection);
        m_replyFinishedConnection = {};
    }
    if (m_readyReadConnection)
    {
        QObject::disconnect(m_readyReadConnection);
        m_readyReadConnection = {};
    }

    if (!m_currentReply.isNull())
    {
        m_currentReply->deleteLater();
        m_currentReply.clear();
    }

    m_streamCallback = nullptr;
    m_streamBuffer.clear();
    m_streamRawBody.clear();
    m_streamedContent.clear();
    m_streaming = false;
    m_streamDone = false;
}
