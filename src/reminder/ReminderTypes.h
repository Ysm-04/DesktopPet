#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

enum class ReminderStatus
{
    Pending,
    Triggered,
    Canceled,
};

enum class ReminderCommandType
{
    Create,
    List,
    Cancel,
    Invalid,
};

enum class ReminderRecurrenceType
{
    None,
    Daily,
    Weekly,
    Monthly,
};

struct ReminderRecurrence
{
    ReminderRecurrenceType type = ReminderRecurrenceType::None;
    int interval = 1;
    int weekday = 0;
    int monthDay = 0;
    int hour = -1;
    int minute = -1;
    QDateTime lastTriggeredAt;
};

struct ReminderItem
{
    QString id;
    QString title;
    QString originalText;
    QDateTime remindAt;
    ReminderStatus status = ReminderStatus::Pending;
    QDateTime createdAt;
    QDateTime finishedAt;
    QString soundId;
    ReminderRecurrence recurrence;
};

struct ReminderCommand
{
    ReminderCommandType type = ReminderCommandType::Invalid;
    QString title;
    QString originalText;
    QDateTime remindAt;
    QString cancelQuery;
    QString errorMessage;
    ReminderRecurrence recurrence;
};

QString reminderStatusToString(ReminderStatus status);
ReminderStatus reminderStatusFromString(const QString &status);
QString reminderRecurrenceTypeToString(ReminderRecurrenceType type);
ReminderRecurrenceType reminderRecurrenceTypeFromString(const QString &type);
bool reminderIsRecurring(const ReminderItem &item);
bool reminderIsRecurring(const ReminderRecurrence &recurrence);
QString reminderDisplayTime(const QDateTime &dateTime);
QString reminderRecurrenceDisplayText(const ReminderItem &item);
QVector<ReminderItem> sortedReminders(QVector<ReminderItem> reminders);
