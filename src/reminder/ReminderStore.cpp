#include "ReminderStore.h"

#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

namespace
{
const QString ReminderStoreFileName = QStringLiteral("reminders.json");

QJsonObject recurrenceToObject(const ReminderRecurrence &recurrence)
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), reminderRecurrenceTypeToString(recurrence.type));
    object.insert(QStringLiteral("interval"), qMax(1, recurrence.interval));
    object.insert(QStringLiteral("weekday"), recurrence.weekday);
    object.insert(QStringLiteral("monthDay"), recurrence.monthDay);
    object.insert(QStringLiteral("hour"), recurrence.hour);
    object.insert(QStringLiteral("minute"), recurrence.minute);
    if (recurrence.lastTriggeredAt.isValid())
    {
        object.insert(QStringLiteral("lastTriggeredAt"), recurrence.lastTriggeredAt.toString(Qt::ISODate));
    }
    return object;
}

ReminderRecurrence recurrenceFromObject(const QJsonObject &object)
{
    ReminderRecurrence recurrence;
    recurrence.type = reminderRecurrenceTypeFromString(object.value(QStringLiteral("type")).toString());
    recurrence.interval = qMax(1, object.value(QStringLiteral("interval")).toInt(1));
    recurrence.weekday = object.value(QStringLiteral("weekday")).toInt(0);
    recurrence.monthDay = object.value(QStringLiteral("monthDay")).toInt(0);
    recurrence.hour = object.value(QStringLiteral("hour")).toInt(-1);
    recurrence.minute = object.value(QStringLiteral("minute")).toInt(-1);
    recurrence.lastTriggeredAt = QDateTime::fromString(
        object.value(QStringLiteral("lastTriggeredAt")).toString(),
        Qt::ISODate);

    if (recurrence.type == ReminderRecurrenceType::None)
    {
        recurrence = {};
    }

    return recurrence;
}

bool recurrenceIsValid(const ReminderRecurrence &recurrence)
{
    if (recurrence.type == ReminderRecurrenceType::None)
    {
        return true;
    }

    if (recurrence.interval < 1
        || recurrence.hour < 0
        || recurrence.hour > 23
        || recurrence.minute < 0
        || recurrence.minute > 59)
    {
        return false;
    }

    if (recurrence.type == ReminderRecurrenceType::Weekly)
    {
        return recurrence.weekday >= 1 && recurrence.weekday <= 7;
    }

    if (recurrence.type == ReminderRecurrenceType::Monthly)
    {
        return recurrence.monthDay >= 1 && recurrence.monthDay <= 31;
    }

    return recurrence.type == ReminderRecurrenceType::Daily;
}

QJsonObject reminderToObject(const ReminderItem &item)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("title"), item.title);
    object.insert(QStringLiteral("originalText"), item.originalText);
    object.insert(QStringLiteral("remindAt"), item.remindAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("status"), reminderStatusToString(item.status));
    object.insert(QStringLiteral("createdAt"), item.createdAt.toString(Qt::ISODate));
    if (item.finishedAt.isValid())
    {
        object.insert(QStringLiteral("finishedAt"), item.finishedAt.toString(Qt::ISODate));
    }
    object.insert(QStringLiteral("soundId"), item.soundId);
    object.insert(QStringLiteral("recurrence"), recurrenceToObject(item.recurrence));
    return object;
}

ReminderItem reminderFromObject(const QJsonObject &object)
{
    ReminderItem item;
    item.id = object.value(QStringLiteral("id")).toString().trimmed();
    item.title = object.value(QStringLiteral("title")).toString().trimmed();
    item.originalText = object.value(QStringLiteral("originalText")).toString();
    item.remindAt = QDateTime::fromString(object.value(QStringLiteral("remindAt")).toString(), Qt::ISODate);
    item.status = reminderStatusFromString(object.value(QStringLiteral("status")).toString());
    item.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    item.finishedAt = QDateTime::fromString(object.value(QStringLiteral("finishedAt")).toString(), Qt::ISODate);
    item.soundId = object.value(QStringLiteral("soundId")).toString().trimmed();
    item.recurrence = recurrenceFromObject(object.value(QStringLiteral("recurrence")).toObject());
    if (!recurrenceIsValid(item.recurrence))
    {
        Logger::warning(QStringLiteral("Invalid reminder recurrence downgraded to one-shot: id=%1").arg(item.id));
        item.recurrence = {};
    }
    return item;
}
}

QVector<ReminderItem> ReminderStore::load(QString *errorMessage) const
{
    QFile file(storePath());
    if (!file.exists())
    {
        return {};
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取提醒文件。");
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenStore(storePath());
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提醒文件损坏，已备份并使用空提醒列表。");
        }
        return {};
    }

    QVector<ReminderItem> items;
    const QJsonArray reminders = document.object().value(QStringLiteral("reminders")).toArray();
    for (const QJsonValue &value : reminders)
    {
        if (!value.isObject())
        {
            continue;
        }

        ReminderItem item = reminderFromObject(value.toObject());
        if (item.id.isEmpty() || !item.remindAt.isValid())
        {
            continue;
        }

        if (item.title.isEmpty())
        {
            item.title = QStringLiteral("提醒");
        }

        if (!item.createdAt.isValid())
        {
            item.createdAt = item.remindAt;
        }

        if (!item.finishedAt.isValid()
            && (item.status == ReminderStatus::Triggered || item.status == ReminderStatus::Canceled))
        {
            item.finishedAt = item.remindAt;
        }

        items.append(item);
    }

    return sortedReminders(items);
}

bool ReminderStore::save(const QVector<ReminderItem> &items, QString *errorMessage) const
{
    QDir directory(configDirectoryPath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建提醒配置目录。");
        }
        return false;
    }

    QJsonArray reminders;
    for (const ReminderItem &item : items)
    {
        reminders.append(reminderToObject(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("reminders"), reminders);

    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入提醒文件。");
        }
        return false;
    }

    const QJsonDocument document(root);
    const QByteArray payload = document.toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入提醒文件不完整。");
        }
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交提醒文件失败。");
        }
        return false;
    }

    return true;
}

QString ReminderStore::storePath() const
{
    return QDir(configDirectoryPath()).filePath(ReminderStoreFileName);
}

QString ReminderStore::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return path.isEmpty() ? QDir::currentPath() : path;
}

void ReminderStore::backupBrokenStore(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(QStringLiteral("reminders.broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            QStringLiteral("reminders.broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (!file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Failed to back up broken reminders file: ") + filePath);
    }
}
