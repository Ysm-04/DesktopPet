#include "AIDiagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>
#include <QUrlQuery>
#include <QtGlobal>

namespace
{
constexpr int MaxBodySummaryLength = 360;

bool isSensitiveName(const QString &name)
{
    const QString lowerName = name.toLower();
    return lowerName.contains(QStringLiteral("key"))
        || lowerName.contains(QStringLiteral("token"))
        || lowerName.contains(QStringLiteral("secret"))
        || lowerName.contains(QStringLiteral("password"))
        || lowerName.contains(QStringLiteral("authorization"))
        || lowerName.contains(QStringLiteral("signature"))
        || lowerName.contains(QStringLiteral("apikey"))
        || lowerName == QStringLiteral("prompt")
        || lowerName == QStringLiteral("message")
        || lowerName == QStringLiteral("messages")
        || lowerName == QStringLiteral("content")
        || lowerName == QStringLiteral("contents");
}

QJsonValue sanitizedJsonValue(const QJsonValue &value, const QString &keyName = {})
{
    if (!keyName.isEmpty() && isSensitiveName(keyName))
    {
        return QStringLiteral("<redacted>");
    }

    if (value.isObject())
    {
        QJsonObject sanitized;
        const QJsonObject object = value.toObject();
        for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
        {
            sanitized.insert(iterator.key(), sanitizedJsonValue(iterator.value(), iterator.key()));
        }
        return sanitized;
    }

    if (value.isArray())
    {
        QJsonArray sanitized;
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
        {
            sanitized.append(sanitizedJsonValue(item));
        }
        return sanitized;
    }

    if (value.isString())
    {
        return AIDiagnostics::safeTextSummary(value.toString());
    }

    return value;
}

QString sanitizedUrlString(QUrl url)
{
    url.setUserInfo(QString());

    QUrlQuery query(url);
    if (!query.isEmpty())
    {
        QUrlQuery sanitizedQuery;
        const auto items = query.queryItems(QUrl::FullyDecoded);
        for (const auto &item : items)
        {
            sanitizedQuery.addQueryItem(
                item.first,
                isSensitiveName(item.first) ? QStringLiteral("<redacted>") : item.second);
        }
        url.setQuery(sanitizedQuery);
    }

    return url.toString(QUrl::FullyEncoded);
}
}

namespace AIDiagnostics
{
QString oneLine(QString text)
{
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return text.simplified();
}

QString safeTextSummary(const QString &text, int maxLength)
{
    QString summary = oneLine(text);
    if (summary.size() <= maxLength)
    {
        return summary;
    }

    return summary.left(qMax(0, maxLength)) + QStringLiteral("...");
}

QString diagnosticContext(const AIConfig &config, const QUrl &url)
{
    return QStringLiteral("provider=%1 protocol=%2 model=%3 url=%4 timeoutMs=%5 maxTokens=%6 temperature=%7")
        .arg(config.provider.trimmed().isEmpty() ? QStringLiteral("<empty>") : config.provider.trimmed())
        .arg(config.protocol.trimmed().isEmpty() ? QStringLiteral("<empty>") : config.protocol.trimmed())
        .arg(config.model.trimmed().isEmpty() ? QStringLiteral("<empty>") : config.model.trimmed())
        .arg(sanitizedUrlString(url))
        .arg(config.timeoutMs)
        .arg(config.maxTokens)
        .arg(QString::number(config.temperature, 'f', 2));
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
        return safeTextSummary(QString::fromUtf8(body), MaxBodySummaryLength);
    }

    const QJsonObject root = document.object();
    QStringList details;

    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isObject())
    {
        const QJsonObject error = errorValue.toObject();
        const QString message = error.value(QStringLiteral("message")).toString().trimmed();
        const QJsonValue codeValue = error.value(QStringLiteral("code"));
        const QString code = codeValue.isString()
            ? codeValue.toString().trimmed()
            : (codeValue.isDouble() ? QString::number(codeValue.toInt()) : QString());
        const QString type = error.value(QStringLiteral("type")).toString().trimmed();
        const QString status = error.value(QStringLiteral("status")).toString().trimmed();

        if (!message.isEmpty())
        {
            details.append(safeTextSummary(message));
        }
        if (!code.isEmpty())
        {
            details.append(QStringLiteral("code=") + code);
        }
        if (!type.isEmpty() && type != code)
        {
            details.append(QStringLiteral("type=") + type);
        }
        if (!status.isEmpty() && status != code && status != type)
        {
            details.append(QStringLiteral("status=") + status);
        }
    }
    else if (errorValue.isString())
    {
        const QString error = errorValue.toString().trimmed();
        if (!error.isEmpty())
        {
            details.append(safeTextSummary(error));
        }
    }

    const QString message = root.value(QStringLiteral("message")).toString().trimmed();
    if (!message.isEmpty() && !details.contains(message))
    {
        details.append(safeTextSummary(message));
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

    return responseBodySummary(body);
}

QString responseBodySummary(const QByteArray &body)
{
    if (body.trimmed().isEmpty())
    {
        return QStringLiteral("bytes=0");
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError)
    {
        const QJsonValue sanitized = document.isArray()
            ? sanitizedJsonValue(QJsonValue(document.array()))
            : sanitizedJsonValue(QJsonValue(document.object()));
        const QJsonDocument sanitizedDocument(
            sanitized.isArray() ? QJsonDocument(sanitized.toArray()) : QJsonDocument(sanitized.toObject()));
        return QStringLiteral("bytes=%1 json=%2")
            .arg(QString::number(body.size()))
            .arg(safeTextSummary(QString::fromUtf8(sanitizedDocument.toJson(QJsonDocument::Compact)), MaxBodySummaryLength));
    }

    return QStringLiteral("bytes=%1 preview=%2")
        .arg(QString::number(body.size()))
        .arg(safeTextSummary(QString::fromUtf8(body), MaxBodySummaryLength));
}
}
