#include "OpenAICompatibleProvider.h"

#include "AIDiagnostics.h"
#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

OpenAICompatibleProvider::OpenAICompatibleProvider(const AIConfig &config)
    : m_config(config)
{
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]() {
        finishWithError(QStringLiteral("AI request timed out."));
    });
}

OpenAICompatibleProvider::~OpenAICompatibleProvider()
{
    cancel();
}

bool OpenAICompatibleProvider::isBusy() const
{
    return !m_currentReply.isNull();
}

void OpenAICompatibleProvider::sendChatRequest(const ChatRequest &request, ResponseCallback callback)
{
    sendChatRequestInternal(request, false, nullptr, std::move(callback));
}

void OpenAICompatibleProvider::sendStreamingChatRequest(
    const ChatRequest &request,
    StreamCallback streamCallback,
    ResponseCallback callback)
{
    sendChatRequestInternal(request, true, std::move(streamCallback), std::move(callback));
}

void OpenAICompatibleProvider::sendChatRequestInternal(
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

    const QUrl url = requestUrl();
    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());

    const QJsonDocument document(buildPayload(request, stream));
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

void OpenAICompatibleProvider::cancel()
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

QJsonObject OpenAICompatibleProvider::buildPayload(const ChatRequest &request, bool stream) const
{
    QJsonArray messages;
    for (const ChatMessage &message : request.messages)
    {
        QJsonObject item;
        item.insert(QStringLiteral("role"), message.role);
        item.insert(QStringLiteral("content"), message.content);
        messages.append(item);
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_config.model);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("stream"), stream);
    payload.insert(QStringLiteral("temperature"), m_config.temperature);
    payload.insert(QStringLiteral("max_tokens"), m_config.maxTokens);
    return payload;
}

QUrl OpenAICompatibleProvider::requestUrl() const
{
    QString baseUrl = m_config.baseUrl.trimmed();
    QString path = m_config.path.trimmed();

    while (baseUrl.endsWith(QLatin1Char('/')))
    {
        baseUrl.chop(1);
    }

    if (path.isEmpty())
    {
        path = QStringLiteral("/chat/completions");
    }

    if (!path.startsWith(QLatin1Char('/')))
    {
        path.prepend(QLatin1Char('/'));
    }

    if (baseUrl.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive)
        && path.compare(QStringLiteral("/chat/completions"), Qt::CaseInsensitive) == 0)
    {
        return QUrl(baseUrl);
    }

    return QUrl(baseUrl + path);
}

void OpenAICompatibleProvider::handleStreamReadyRead()
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

void OpenAICompatibleProvider::processStreamBuffer()
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
        if (line.isEmpty() || line.startsWith(':'))
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

bool OpenAICompatibleProvider::handleStreamPayload(const QByteArray &payload)
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
            .arg(AIDiagnostics::diagnosticContext(m_config, requestUrl()))
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

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject())
    {
        return true;
    }

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
    const QString content = delta.value(QStringLiteral("content")).toString();
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

ChatResponse OpenAICompatibleProvider::parseResponse(QNetworkReply *reply, const QByteArray &body) const
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
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject())
    {
        Logger::warning(QStringLiteral("AI response has no choices: %1 httpStatus=%2 bodySummary=\"%3\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        return {false, {}, QStringLiteral("AI response has no choices."), httpStatus};
    }

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (content.isEmpty())
    {
        const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString().trimmed();
        Logger::warning(QStringLiteral("AI response content is empty: %1 httpStatus=%2 bodySummary=\"%3\"")
            .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
            .arg(httpStatus)
            .arg(AIDiagnostics::responseBodySummary(body)));
        if (finishReason == QStringLiteral("length"))
        {
            return {
                false,
                {},
                QStringLiteral("AI response reached the token limit before producing content. Increase Max Tokens."),
                httpStatus,
            };
        }
        return {false, {}, QStringLiteral("AI response content is empty."), httpStatus};
    }

    Logger::info(QStringLiteral("AI request completed: %1 httpStatus=%2 responseChars=%3 responseBytes=%4")
        .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
        .arg(httpStatus)
        .arg(QString::number(content.size()))
        .arg(QString::number(body.size())));

    return {true, content, {}, httpStatus};
}

ChatResponse OpenAICompatibleProvider::finishStreamingResponse(QNetworkReply *reply, const QByteArray &body) const
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
        return {false, {}, QStringLiteral("AI streaming response content is empty."), httpStatus};
    }

    Logger::info(QStringLiteral("AI streaming request completed: %1 httpStatus=%2 streamDone=%3 responseChars=%4")
        .arg(AIDiagnostics::diagnosticContext(m_config, reply->request().url()))
        .arg(httpStatus)
        .arg(m_streamDone ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(QString::number(m_streamedContent.size())));

    return {true, m_streamedContent, {}, httpStatus};
}

void OpenAICompatibleProvider::finishWithError(const QString &message, int httpStatus)
{
    QPointer<QNetworkReply> reply = m_currentReply;
    const QUrl url = reply.isNull() ? requestUrl() : reply->request().url();
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

void OpenAICompatibleProvider::clearReply()
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
