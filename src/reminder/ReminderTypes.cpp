#include "ReminderTypes.h"

#include <algorithm>

namespace
{
QString twoDigit(int value)
{
    return QString::number(value).rightJustified(2, QLatin1Char('0'));
}

QString weekdayDisplayText(int weekday)
{
    switch (weekday)
    {
    case 1:
        return QStringLiteral("一");
    case 2:
        return QStringLiteral("二");
    case 3:
        return QStringLiteral("三");
    case 4:
        return QStringLiteral("四");
    case 5:
        return QStringLiteral("五");
    case 6:
        return QStringLiteral("六");
    case 7:
        return QStringLiteral("日");
    default:
        return QStringLiteral("?");
    }
}

QString recurrenceTimeText(const ReminderItem &item)
{
    const int hour = item.recurrence.hour >= 0 ? item.recurrence.hour : item.remindAt.time().hour();
    const int minute = item.recurrence.minute >= 0 ? item.recurrence.minute : item.remindAt.time().minute();
    return QStringLiteral("%1:%2").arg(twoDigit(hour), twoDigit(minute));
}
}

QString reminderStatusToString(ReminderStatus status)
{
    switch (status)
    {
    case ReminderStatus::Pending:
        return QStringLiteral("pending");
    case ReminderStatus::Triggered:
        return QStringLiteral("triggered");
    case ReminderStatus::Canceled:
        return QStringLiteral("canceled");
    }

    return QStringLiteral("pending");
}

ReminderStatus reminderStatusFromString(const QString &status)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized == QStringLiteral("triggered"))
    {
        return ReminderStatus::Triggered;
    }

    if (normalized == QStringLiteral("canceled"))
    {
        return ReminderStatus::Canceled;
    }

    return ReminderStatus::Pending;
}

QString reminderRecurrenceTypeToString(ReminderRecurrenceType type)
{
    switch (type)
    {
    case ReminderRecurrenceType::None:
        return QStringLiteral("none");
    case ReminderRecurrenceType::Daily:
        return QStringLiteral("daily");
    case ReminderRecurrenceType::Weekly:
        return QStringLiteral("weekly");
    case ReminderRecurrenceType::Monthly:
        return QStringLiteral("monthly");
    }

    return QStringLiteral("none");
}

ReminderRecurrenceType reminderRecurrenceTypeFromString(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("daily"))
    {
        return ReminderRecurrenceType::Daily;
    }

    if (normalized == QStringLiteral("weekly"))
    {
        return ReminderRecurrenceType::Weekly;
    }

    if (normalized == QStringLiteral("monthly"))
    {
        return ReminderRecurrenceType::Monthly;
    }

    return ReminderRecurrenceType::None;
}

bool reminderIsRecurring(const ReminderItem &item)
{
    return reminderIsRecurring(item.recurrence);
}

bool reminderIsRecurring(const ReminderRecurrence &recurrence)
{
    return recurrence.type != ReminderRecurrenceType::None;
}

QString reminderDisplayTime(const QDateTime &dateTime)
{
    return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString reminderRecurrenceDisplayText(const ReminderItem &item)
{
    switch (item.recurrence.type)
    {
    case ReminderRecurrenceType::None:
        return QStringLiteral("一次");
    case ReminderRecurrenceType::Daily:
        return QStringLiteral("每天 %1").arg(recurrenceTimeText(item));
    case ReminderRecurrenceType::Weekly:
        return QStringLiteral("每周%1 %2").arg(weekdayDisplayText(item.recurrence.weekday), recurrenceTimeText(item));
    case ReminderRecurrenceType::Monthly:
        return QStringLiteral("每月%1日 %2").arg(item.recurrence.monthDay).arg(recurrenceTimeText(item));
    }

    return QStringLiteral("一次");
}

QVector<ReminderItem> sortedReminders(QVector<ReminderItem> reminders)
{
    std::sort(reminders.begin(), reminders.end(), [](const ReminderItem &left, const ReminderItem &right) {
        if (left.remindAt == right.remindAt)
        {
            return left.createdAt < right.createdAt;
        }

        return left.remindAt < right.remindAt;
    });
    return reminders;
}
