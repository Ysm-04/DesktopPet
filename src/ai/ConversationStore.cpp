#include "ConversationStore.h"

#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtGlobal>

#include <utility>

namespace
{
constexpr int StoreVersion = 1;

QJsonObject objectFromMessage(const ChatMessage &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    object.insert(QStringLiteral("content"), message.content);
    if (message.timestamp.isValid())
    {
        object.insert(QStringLiteral("timestamp"), message.timestamp.toString(Qt::ISODate));
    }
    if (!message.provider.trimmed().isEmpty())
    {
        object.insert(QStringLiteral("provider"), message.provider.trimmed());
    }
    if (!message.model.trimmed().isEmpty())
    {
        object.insert(QStringLiteral("model"), message.model.trimmed());
    }
    return object;
}

ChatMessage messageFromObject(const QJsonObject &object)
{
    QDateTime timestamp = QDateTime::fromString(
        object.value(QStringLiteral("timestamp")).toString(),
        Qt::ISODate);
    if (!timestamp.isValid())
    {
        timestamp = QDateTime::fromString(
            object.value(QStringLiteral("createdAt")).toString(),
            Qt::ISODate);
    }

    return {
        object.value(QStringLiteral("role")).toString().trimmed(),
        object.value(QStringLiteral("content")).toString(),
        timestamp,
        object.value(QStringLiteral("provider")).toString().trimmed(),
        object.value(QStringLiteral("model")).toString().trimmed()
    };
}

bool sameMessage(const ChatMessage &left, const ChatMessage &right)
{
    return left.role == right.role && left.content == right.content;
}

int overlappingMessageCount(const QVector<ChatMessage> &existingMessages, const QVector<ChatMessage> &newMessages)
{
    const int maxOverlap = qMin(existingMessages.size(), newMessages.size());
    for (int overlap = maxOverlap; overlap > 0; --overlap)
    {
        bool matched = true;
        const int existingStart = existingMessages.size() - overlap;
        for (int index = 0; index < overlap; ++index)
        {
            if (!sameMessage(existingMessages.at(existingStart + index), newMessages.at(index)))
            {
                matched = false;
                break;
            }
        }

        if (matched)
        {
            return overlap;
        }
    }

    return 0;
}

void backupBrokenConversationHistory(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(
        fileInfo.completeBaseName() + QStringLiteral(".broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            fileInfo.completeBaseName()
            + QStringLiteral(".broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken conversation history was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken conversation history: %1").arg(filePath));
}
}

ConversationStore::ConversationStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QVector<ChatMessage> ConversationStore::load(int maxMessages, QString *errorMessage) const
{
    return recentMessages(readMessages(errorMessage), maxMessages);
}

bool ConversationStore::save(const QVector<ChatMessage> &messages, int maxMessages) const
{
    const QFileInfo fileInfo(m_filePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        Logger::warning(QStringLiteral("Unable to create conversation history directory."));
        return false;
    }

    QVector<ChatMessage> mergedMessages = readMessages();
    const int overlap = overlappingMessageCount(mergedMessages, messages);
    for (int index = overlap; index < messages.size(); ++index)
    {
        const ChatMessage &message = messages.at(index);
        if (message.role.isEmpty() || message.content.trimmed().isEmpty())
        {
            continue;
        }

        mergedMessages.append(message);
    }

    QJsonArray messageArray;
    const QVector<ChatMessage> storedMessages = recentMessages(mergedMessages, maxMessages);
    for (const ChatMessage &message : storedMessages)
    {
        if (message.role.isEmpty() || message.content.trimmed().isEmpty())
        {
            continue;
        }

        messageArray.append(objectFromMessage(message));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), StoreVersion);
    root.insert(QStringLiteral("messages"), messageArray);

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        Logger::warning(QStringLiteral("Unable to open conversation history for writing."));
        return false;
    }

    const QJsonDocument document(root);
    return file.write(document.toJson(QJsonDocument::Indented)) >= 0;
}

bool ConversationStore::clear() const
{
    if (!QFile::exists(m_filePath))
    {
        return true;
    }

    if (!QFile::remove(m_filePath))
    {
        Logger::warning(QStringLiteral("Unable to remove conversation history."));
        return false;
    }

    return true;
}

QString ConversationStore::filePath() const
{
    return m_filePath;
}

QVector<ChatMessage> ConversationStore::readMessages(QString *errorMessage) const
{
    QVector<ChatMessage> messages;

    QFile file(m_filePath);
    if (!file.exists())
    {
        return messages;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Unable to read conversation history.");
        }
        Logger::warning(QStringLiteral("Unable to read conversation history."));
        return messages;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenConversationHistory(m_filePath);
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Conversation history is not valid JSON.");
        }
        Logger::warning(QStringLiteral("Conversation history is broken; it will be ignored."));
        return messages;
    }

    const QJsonArray messageArray = document.object().value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &value : messageArray)
    {
        if (!value.isObject())
        {
            continue;
        }

        const ChatMessage message = messageFromObject(value.toObject());
        if (message.role.isEmpty() || message.content.trimmed().isEmpty())
        {
            continue;
        }

        messages.append(message);
    }

    return messages;
}

QVector<ChatMessage> ConversationStore::recentMessages(const QVector<ChatMessage> &messages, int maxMessages) const
{
    int boundedMaxMessages = qMax(0, maxMessages);
    if (boundedMaxMessages == 1)
    {
        boundedMaxMessages = 0;
    }
    else if (boundedMaxMessages > 1)
    {
        boundedMaxMessages -= boundedMaxMessages % 2;
    }
    if (boundedMaxMessages <= 0 || messages.isEmpty())
    {
        return {};
    }

    const int firstIndex = qMax(0, messages.size() - boundedMaxMessages);
    QVector<ChatMessage> recent = messages.mid(firstIndex);
    if (recent.size() % 2 != 0)
    {
        recent.erase(recent.begin());
    }
    return recent;
}
