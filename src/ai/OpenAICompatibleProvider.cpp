#include "OpenAICompatibleProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace
{
QString trimmedResponseBody(const QByteArray &body)
{
    const QString text = QString::fromUtf8(body).trimmed();
    constexpr int MaxErrorBodyLength = 1000;
    if (text.size() <= MaxErrorBodyLength)
    {
        return text;
    }

    return text.left(MaxErrorBodyLength) + QStringLiteral("...");
}

QString errorMessageFromBody(const QByteArray &body)
{
    if (body.trimmed().isEmpty())
    {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return trimmedResponseBody(body);
    }

    const QJsonObject root = document.object();
    QStringList details;

    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isObject())
    {
        const QJsonObject error = errorValue.toObject();
        const QString message = error.value(QStringLiteral("message")).toString().trimmed();
        const QString code = error.value(QStringLiteral("code")).toString().trimmed();
        const QString type = error.value(QStringLiteral("type")).toString().trimmed();

        if (!message.isEmpty())
        {
            details.append(message);
        }
        if (!code.isEmpty())
        {
            details.append(QStringLiteral("code=") + code);
        }
        if (!type.isEmpty() && type != code)
        {
            details.append(QStringLiteral("type=") + type);
        }
    }
    else if (errorValue.isString())
    {
        const QString error = errorValue.toString().trimmed();
        if (!error.isEmpty())
        {
            details.append(error);
        }
    }

    const QString message = root.value(QStringLiteral("message")).toString().trimmed();
    if (!message.isEmpty() && !details.contains(message))
    {
        details.append(message);
    }

    const QString requestId = root.value(QStringLiteral("request_id")).toString().trimmed();
    if (!requestId.isEmpty())
    {
        details.append(QStringLiteral("request_id=") + requestId);
    }

    if (!details.isEmpty())
    {
        return details.join(QStringLiteral("; "));
    }

    return trimmedResponseBody(body);
}
}

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

    QNetworkRequest networkRequest(requestUrl());
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + m_config.apiKey.toUtf8());

    const QJsonDocument document(buildPayload(request));
    m_currentReply = m_networkManager.post(networkRequest, document.toJson(QJsonDocument::Compact));
    m_replyFinishedConnection = QObject::connect(m_currentReply, &QNetworkReply::finished, [this]() {
        if (m_currentReply.isNull())
        {
            return;
        }

        QNetworkReply *reply = m_currentReply;
        const QByteArray body = reply->readAll();
        ChatResponse response = parseResponse(reply, body);
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
    clearReply();

    if (!reply.isNull())
    {
        reply->abort();
    }
}

QJsonObject OpenAICompatibleProvider::buildPayload(const ChatRequest &request) const
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
    payload.insert(QStringLiteral("stream"), false);
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

    if (!path.startsWith(QLatin1Char('/')))
    {
        path.prepend(QLatin1Char('/'));
    }

    return QUrl(baseUrl + path);
}

ChatResponse OpenAICompatibleProvider::parseResponse(QNetworkReply *reply, const QByteArray &body) const
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        const QString bodyError = errorMessageFromBody(body);
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
        return {false, {}, QStringLiteral("AI response is not valid JSON."), httpStatus};
    }

    const QJsonObject root = document.object();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject())
    {
        return {false, {}, QStringLiteral("AI response has no choices."), httpStatus};
    }

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();
    if (content.isEmpty())
    {
        return {false, {}, QStringLiteral("AI response content is empty."), httpStatus};
    }

    return {true, content, {}, httpStatus};
}

void OpenAICompatibleProvider::finishWithError(const QString &message, int httpStatus)
{
    QPointer<QNetworkReply> reply = m_currentReply;
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

    if (!m_currentReply.isNull())
    {
        m_currentReply->deleteLater();
        m_currentReply.clear();
    }
}
