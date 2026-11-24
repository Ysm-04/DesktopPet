#include "LongTermMemoryStore.h"

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
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QtGlobal>

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
constexpr int StoreVersion = 1;
constexpr int MinContentChars = 2;

int sourcePriority(LongTermMemorySource source)
{
    switch (source)
    {
    case LongTermMemorySource::Manual:
        return 3;
    case LongTermMemorySource::Explicit:
        return 2;
    case LongTermMemorySource::Auto:
        return 1;
    }

    return 1;
}

bool isUserConfirmedSource(LongTermMemorySource source)
{
    return source == LongTermMemorySource::Manual || source == LongTermMemorySource::Explicit;
}

QString normalizedComparisonKey(const QString &text)
{
    QString key;
    key.reserve(text.size());
    for (const QChar ch : text)
    {
        if (ch.isLetterOrNumber())
        {
            key.append(ch.toLower());
        }
    }
    return key;
}

QDateTime parseTimestamp(const QJsonObject &object, const QString &key)
{
    return QDateTime::fromString(object.value(key).toString(), Qt::ISODate);
}

QJsonObject objectFromMemoryItem(const LongTermMemoryItem &item)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("content"), item.content);
    object.insert(QStringLiteral("category"), longTermMemoryCategoryToString(item.category));
    object.insert(QStringLiteral("source"), longTermMemorySourceToString(item.source));
    if (item.createdAt.isValid())
    {
        object.insert(QStringLiteral("createdAt"), item.createdAt.toString(Qt::ISODate));
    }
    if (item.updatedAt.isValid())
    {
        object.insert(QStringLiteral("updatedAt"), item.updatedAt.toString(Qt::ISODate));
    }
    return object;
}

LongTermMemoryItem memoryItemFromObject(const QJsonObject &object, const QDateTime &fallbackNow)
{
    LongTermMemoryItem item;
    item.id = object.value(QStringLiteral("id")).toString().trimmed();
    item.content = LongTermMemoryStore::sanitizeContent(object.value(QStringLiteral("content")).toString());
    item.category = longTermMemoryCategoryFromString(object.value(QStringLiteral("category")).toString());
    item.source = longTermMemorySourceFromString(object.value(QStringLiteral("source")).toString());
    item.createdAt = parseTimestamp(object, QStringLiteral("createdAt"));
    item.updatedAt = parseTimestamp(object, QStringLiteral("updatedAt"));

    if (!item.createdAt.isValid())
    {
        item.createdAt = item.updatedAt.isValid() ? item.updatedAt : fallbackNow;
    }
    if (!item.updatedAt.isValid())
    {
        item.updatedAt = item.createdAt;
    }
    if (item.id.isEmpty())
    {
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    return item;
}

void sortByUpdatedDescending(QVector<LongTermMemoryItem> &items)
{
    std::stable_sort(items.begin(), items.end(), [](const LongTermMemoryItem &left, const LongTermMemoryItem &right) {
        const qint64 leftUpdated = left.updatedAt.isValid() ? left.updatedAt.toMSecsSinceEpoch() : -1;
        const qint64 rightUpdated = right.updatedAt.isValid() ? right.updatedAt.toMSecsSinceEpoch() : -1;
        if (leftUpdated != rightUpdated)
        {
            return leftUpdated > rightUpdated;
        }

        const qint64 leftCreated = left.createdAt.isValid() ? left.createdAt.toMSecsSinceEpoch() : -1;
        const qint64 rightCreated = right.createdAt.isValid() ? right.createdAt.toMSecsSinceEpoch() : -1;
        return leftCreated > rightCreated;
    });
}

void backupBrokenMemoryFile(const QString &filePath)
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
        Logger::warning(QStringLiteral("Broken long-term memory file was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken long-term memory file: %1").arg(filePath));
}

bool validateCleanedContent(const QString &cleaned, QString *errorMessage)
{
    if (cleaned.size() < MinContentChars)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("记忆内容至少需要 2 个有效字符。");
        }
        return false;
    }

    if (LongTermMemoryStore::containsSensitiveSecret(cleaned))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("检测到疑似密钥、Token 或密码等敏感信息，长期记忆不会明文保存此类内容。");
        }
        return false;
    }

    return true;
}
}

LongTermMemoryStore::LongTermMemoryStore(QString filePath, int maxItems)
    : m_filePath(std::move(filePath))
    , m_maxItems(qMax(1, maxItems))
{
}

QVector<LongTermMemoryItem> LongTermMemoryStore::load(QString *errorMessage) const
{
    QVector<LongTermMemoryItem> items;

    QFile file(m_filePath);
    if (!file.exists())
    {
        return items;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取长期记忆文件。");
        }
        Logger::warning(QStringLiteral("Unable to read long-term memory file."));
        return items;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenMemoryFile(m_filePath);
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("长期记忆文件格式损坏，已备份并忽略。");
        }
        Logger::warning(QStringLiteral("Long-term memory file is broken; it will be ignored."));
        return items;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QJsonArray itemArray = document.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : itemArray)
    {
        if (!value.isObject())
        {
            continue;
        }

        const LongTermMemoryItem item = memoryItemFromObject(value.toObject(), now);
        if (item.content.size() < MinContentChars)
        {
            continue;
        }
        items.append(item);
    }

    sortByUpdatedDescending(items);
    return items;
}

bool LongTermMemoryStore::saveAll(const QVector<LongTermMemoryItem> &items, QString *errorMessage) const
{
    const QFileInfo fileInfo(m_filePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建长期记忆存储目录。");
        }
        Logger::warning(QStringLiteral("Unable to create long-term memory directory."));
        return false;
    }

    QVector<LongTermMemoryItem> normalized = items;
    sortByUpdatedDescending(normalized);

    QJsonArray itemArray;
    for (LongTermMemoryItem item : normalized)
    {
        item.content = sanitizeContent(item.content);
        if (item.content.size() < MinContentChars)
        {
            continue;
        }
        if (item.id.trimmed().isEmpty())
        {
            item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        itemArray.append(objectFromMemoryItem(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), StoreVersion);
    root.insert(QStringLiteral("items"), itemArray);

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法打开长期记忆文件进行写入。");
        }
        Logger::warning(QStringLiteral("Unable to open long-term memory file for writing."));
        return false;
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入长期记忆文件不完整。");
        }
        Logger::warning(QStringLiteral("Long-term memory write was incomplete."));
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交长期记忆文件失败。");
        }
        Logger::warning(QStringLiteral("Unable to commit long-term memory file."));
        return false;
    }

    return true;
}

bool LongTermMemoryStore::upsert(
    const QString &content,
    LongTermMemoryCategory category,
    LongTermMemorySource source,
    LongTermMemoryItem *savedItem,
    bool *updatedExisting,
    QString *errorMessage) const
{
    if (updatedExisting != nullptr)
    {
        *updatedExisting = false;
    }

    const QString cleaned = sanitizeContent(content);
    if (!validateCleanedContent(cleaned, errorMessage))
    {
        return false;
    }

    const QString targetKey = normalizedComparisonKey(cleaned);
    if (targetKey.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("记忆内容不包含有效文字。");
        }
        return false;
    }

    QVector<LongTermMemoryItem> items = load();
    const QDateTime now = QDateTime::currentDateTime();

    for (int index = 0; index < items.size(); ++index)
    {
        LongTermMemoryItem &existing = items[index];
        if (normalizedComparisonKey(existing.content) != targetKey)
        {
            continue;
        }

        if (isUserConfirmedSource(existing.source) && source == LongTermMemorySource::Auto)
        {
            // 自动提炼条目绝不覆盖用户手动或显式指令写入的记忆
            if (savedItem != nullptr)
            {
                *savedItem = existing;
            }
            if (updatedExisting != nullptr)
            {
                *updatedExisting = true;
            }
            return true;
        }

        existing.content = cleaned;
        existing.category = category;
        if (sourcePriority(source) >= sourcePriority(existing.source))
        {
            existing.source = source;
        }
        existing.updatedAt = now;
        const LongTermMemoryItem resultItem = existing;
        if (!saveAll(items, errorMessage))
        {
            return false;
        }

        if (savedItem != nullptr)
        {
            *savedItem = resultItem;
        }
        if (updatedExisting != nullptr)
        {
            *updatedExisting = true;
        }
        return true;
    }

    if (items.size() >= m_maxItems)
    {
        int oldestAutoIndex = -1;
        qint64 oldestAutoTimestamp = std::numeric_limits<qint64>::max();
        for (int index = 0; index < items.size(); ++index)
        {
            if (items.at(index).source != LongTermMemorySource::Auto)
            {
                continue;
            }

            const qint64 ts = items.at(index).updatedAt.isValid()
                ? items.at(index).updatedAt.toMSecsSinceEpoch()
                : 0;
            if (oldestAutoIndex < 0 || ts < oldestAutoTimestamp)
            {
                oldestAutoIndex = index;
                oldestAutoTimestamp = ts;
            }
        }

        if (oldestAutoIndex < 0)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = source == LongTermMemorySource::Auto
                    ? QStringLiteral("长期记忆已满且均为用户确认条目，已跳过自动提炼条目。")
                    : QStringLiteral("长期记忆已达上限（%1 条），请先在设置 → 长期记忆中删除部分条目。")
                          .arg(m_maxItems);
            }
            return false;
        }

        items.removeAt(oldestAutoIndex);
    }

    LongTermMemoryItem newItem;
    newItem.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    newItem.content = cleaned;
    newItem.category = category;
    newItem.source = source;
    newItem.createdAt = now;
    newItem.updatedAt = now;
    items.prepend(newItem);

    if (!saveAll(items, errorMessage))
    {
        return false;
    }

    if (savedItem != nullptr)
    {
        *savedItem = newItem;
    }
    return true;
}

bool LongTermMemoryStore::updateItem(
    const QString &id,
    const QString &content,
    LongTermMemoryCategory category,
    LongTermMemoryItem *updatedItem,
    QString *errorMessage) const
{
    const QString targetId = id.trimmed();
    if (targetId.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("记忆条目标识无效。");
        }
        return false;
    }

    const QString cleaned = sanitizeContent(content);
    if (!validateCleanedContent(cleaned, errorMessage))
    {
        return false;
    }

    const QString targetKey = normalizedComparisonKey(cleaned);
    if (targetKey.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("记忆内容不包含有效文字。");
        }
        return false;
    }

    QVector<LongTermMemoryItem> items = load(errorMessage);
    int foundIndex = -1;
    for (int index = 0; index < items.size(); ++index)
    {
        if (items.at(index).id == targetId)
        {
            foundIndex = index;
            continue;
        }

        if (normalizedComparisonKey(items.at(index).content) == targetKey)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("已存在内容相同的长期记忆。");
            }
            return false;
        }
    }

    if (foundIndex < 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("没有找到要编辑的长期记忆条目。");
        }
        return false;
    }

    LongTermMemoryItem &item = items[foundIndex];
    item.content = cleaned;
    item.category = category;
    item.updatedAt = QDateTime::currentDateTime();
    const LongTermMemoryItem resultItem = item;

    if (!saveAll(items, errorMessage))
    {
        return false;
    }

    if (updatedItem != nullptr)
    {
        *updatedItem = resultItem;
    }
    return true;
}

bool LongTermMemoryStore::removeById(const QString &id, QString *errorMessage) const
{
    const QString targetId = id.trimmed();
    if (targetId.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("记忆条目标识无效。");
        }
        return false;
    }

    QVector<LongTermMemoryItem> items = load(errorMessage);
    int removedCount = 0;
    for (int index = items.size() - 1; index >= 0; --index)
    {
        if (items.at(index).id == targetId)
        {
            items.removeAt(index);
            ++removedCount;
        }
    }

    if (removedCount == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("没有找到要删除的长期记忆条目。");
        }
        return false;
    }

    return saveAll(items, errorMessage);
}

QVector<LongTermMemoryItem> LongTermMemoryStore::removeMatchingKeyword(
    const QString &keyword,
    QString *errorMessage) const
{
    const QString trimmedKeyword = keyword.trimmed();
    if (trimmedKeyword.isEmpty())
    {
        return {};
    }

    QVector<LongTermMemoryItem> items = load(errorMessage);
    QVector<LongTermMemoryItem> removed;
    for (int index = items.size() - 1; index >= 0; --index)
    {
        if (items.at(index).content.contains(trimmedKeyword, Qt::CaseInsensitive))
        {
            removed.prepend(items.at(index));
            items.removeAt(index);
        }
    }

    if (removed.isEmpty())
    {
        return {};
    }

    if (!saveAll(items, errorMessage))
    {
        return {};
    }

    return removed;
}

bool LongTermMemoryStore::clear(QString *errorMessage) const
{
    if (!QFile::exists(m_filePath))
    {
        return true;
    }

    if (!QFile::remove(m_filePath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法清空长期记忆文件。");
        }
        Logger::warning(QStringLiteral("Unable to remove long-term memory file."));
        return false;
    }

    return true;
}

QString LongTermMemoryStore::filePath() const
{
    return m_filePath;
}

int LongTermMemoryStore::maxItems() const
{
    return m_maxItems;
}

QString LongTermMemoryStore::sanitizeContent(const QString &content)
{
    QString cleaned = content.simplified();
    if (cleaned.size() > MaxContentChars)
    {
        cleaned = cleaned.left(MaxContentChars).trimmed();
    }
    return cleaned;
}

bool LongTermMemoryStore::containsSensitiveSecret(const QString &text)
{
    static const QRegularExpression TokenPattern(QStringLiteral(
        "(?:\\bsk-[A-Za-z0-9_-]{12,}|\\bAIza[0-9A-Za-z_-]{20,}|\\bghp_[A-Za-z0-9]{20,}"
        "|\\bgithub_pat_[A-Za-z0-9_]{20,}|\\bAKIA[0-9A-Z]{16}|Bearer\\s+[A-Za-z0-9._-]{16,})"));
    static const QRegularExpression AssignmentPattern(QStringLiteral(
        "(?:api[_\\s-]?key|access[_\\s-]?token|secret[_\\s-]?key|password|passwd|密码|口令|密钥|秘钥)\\s*[:：=是]\\s*\\S{4,}"),
        QRegularExpression::CaseInsensitiveOption);

    return TokenPattern.match(text).hasMatch() || AssignmentPattern.match(text).hasMatch();
}

QString LongTermMemoryStore::formatForSystemPrompt(
    const QVector<LongTermMemoryItem> &items,
    int maxChars)
{
    if (items.isEmpty() || maxChars <= 0)
    {
        return {};
    }

    QVector<LongTermMemoryItem> ordered;
    ordered.reserve(items.size());
    for (const LongTermMemoryItem &item : items)
    {
        if (sanitizeContent(item.content).size() >= MinContentChars)
        {
            ordered.append(item);
        }
    }

    if (ordered.isEmpty())
    {
        return {};
    }

    std::stable_sort(ordered.begin(), ordered.end(), [](const LongTermMemoryItem &left, const LongTermMemoryItem &right) {
        const bool leftConfirmed = isUserConfirmedSource(left.source);
        const bool rightConfirmed = isUserConfirmedSource(right.source);
        if (leftConfirmed != rightConfirmed)
        {
            return leftConfirmed;
        }

        const qint64 leftUpdated = left.updatedAt.isValid() ? left.updatedAt.toMSecsSinceEpoch() : -1;
        const qint64 rightUpdated = right.updatedAt.isValid() ? right.updatedAt.toMSecsSinceEpoch() : -1;
        return leftUpdated > rightUpdated;
    });

    const QString header = QStringLiteral(
        "[用户长期记忆]\n"
        "以下是用户此前确认保留的长期背景与偏好，仅在相关时自然参考，不要生硬复述：");

    if (header.size() + 4 >= maxChars)
    {
        return {};
    }

    QString block = header;
    int appendedCount = 0;
    for (const LongTermMemoryItem &item : ordered)
    {
        const QString line = QStringLiteral("- [%1] %2")
                                 .arg(longTermMemoryCategoryDisplayName(item.category), sanitizeContent(item.content));
        const int nextSize = block.size() + 1 + line.size();
        if (nextSize <= maxChars)
        {
            block += QLatin1Char('\n') + line;
            ++appendedCount;
            continue;
        }

        if (appendedCount == 0)
        {
            const int remaining = maxChars - block.size() - 1;
            if (remaining >= 12)
            {
                block += QLatin1Char('\n') + line.left(remaining - 3) + QStringLiteral("...");
                ++appendedCount;
            }
        }
        break;
    }

    return appendedCount > 0 ? block : QString();
}
