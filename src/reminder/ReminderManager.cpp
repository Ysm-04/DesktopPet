#include "ReminderManager.h"

#include "../util/Logger.h"

#include <QDate>
#include <QDateTime>
#include <QRandomGenerator>
#include <QStringList>
#include <QTime>
#include <QtGlobal>

#include <utility>

namespace
{
constexpr qint64 MinimumTimerIntervalMs = 1000;
constexpr qint64 MaximumTimerIntervalMs = 24 * 60 * 60 * 1000;
constexpr int GuardTimerIntervalMs = 60 * 1000;
constexpr int DefaultHistoryRetentionDays = 20;

bool isPending(const ReminderItem &item)
{
    return item.status == ReminderStatus::Pending;
}

bool isFinished(const ReminderItem &item)
{
    return item.status == ReminderStatus::Triggered || item.status == ReminderStatus::Canceled;
}

QDateTime finishedReferenceTime(const ReminderItem &item)
{
    return item.finishedAt.isValid() ? item.finishedAt : item.remindAt;
}

bool textMatchesReminder(const ReminderItem &item, const QString &query)
{
    const QString normalizedQuery = query.trimmed();
    if (normalizedQuery.isEmpty())
    {
        return false;
    }

    return item.id.compare(normalizedQuery, Qt::CaseInsensitive) == 0
        || item.title.contains(normalizedQuery, Qt::CaseInsensitive)
        || item.originalText.contains(normalizedQuery, Qt::CaseInsensitive);
}

QTime recurrenceTime(const ReminderItem &item)
{
    const int hour = item.recurrence.hour >= 0 ? item.recurrence.hour : item.remindAt.time().hour();
    const int minute = item.recurrence.minute >= 0 ? item.recurrence.minute : item.remindAt.time().minute();
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
        return {};
    }

    return QTime(hour, minute);
}

QDateTime nextDailyOccurrence(const ReminderItem &item, const QDateTime &after)
{
    const QTime time = recurrenceTime(item);
    if (!time.isValid())
    {
        return {};
    }

    const int interval = qMax(1, item.recurrence.interval);
    QDateTime next(after.date(), time);
    while (next <= after)
    {
        next = next.addDays(interval);
    }
    return next;
}

QDateTime nextWeeklyOccurrence(const ReminderItem &item, const QDateTime &after)
{
    const QTime time = recurrenceTime(item);
    if (!time.isValid() || item.recurrence.weekday < 1 || item.recurrence.weekday > 7)
    {
        return {};
    }

    const int intervalWeeks = qMax(1, item.recurrence.interval);
    int daysToAdd = item.recurrence.weekday - after.date().dayOfWeek();
    QDateTime next(after.date().addDays(daysToAdd), time);
    while (daysToAdd < 0 || next <= after)
    {
        next = next.addDays(7 * intervalWeeks);
        daysToAdd += 7 * intervalWeeks;
    }
    return next;
}

QDateTime nextMonthlyOccurrence(const ReminderItem &item, const QDateTime &after)
{
    const QTime time = recurrenceTime(item);
    if (!time.isValid() || item.recurrence.monthDay < 1 || item.recurrence.monthDay > 31)
    {
        return {};
    }

    const int intervalMonths = qMax(1, item.recurrence.interval);
    QDate monthCursor(after.date().year(), after.date().month(), 1);
    for (int attempt = 0; attempt < 240; ++attempt)
    {
        const QDate date(monthCursor.year(), monthCursor.month(), item.recurrence.monthDay);
        if (date.isValid())
        {
            const QDateTime next(date, time);
            if (next > after)
            {
                return next;
            }
        }
        monthCursor = monthCursor.addMonths(intervalMonths);
    }

    return {};
}

QDateTime nextRecurringOccurrence(const ReminderItem &item, const QDateTime &after)
{
    switch (item.recurrence.type)
    {
    case ReminderRecurrenceType::Daily:
        return nextDailyOccurrence(item, after);
    case ReminderRecurrenceType::Weekly:
        return nextWeeklyOccurrence(item, after);
    case ReminderRecurrenceType::Monthly:
        return nextMonthlyOccurrence(item, after);
    case ReminderRecurrenceType::None:
        return {};
    }

    return {};
}

bool normalizeRecurrence(ReminderRecurrence *recurrence, QString *errorMessage)
{
    if (recurrence == nullptr || recurrence->type == ReminderRecurrenceType::None)
    {
        if (recurrence != nullptr)
        {
            *recurrence = {};
        }
        return true;
    }

    recurrence->interval = qMax(1, recurrence->interval);
    if (recurrence->hour < 0
        || recurrence->hour > 23
        || recurrence->minute < 0
        || recurrence->minute > 59)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("重复提醒时间无效。");
        }
        return false;
    }

    switch (recurrence->type)
    {
    case ReminderRecurrenceType::Daily:
        recurrence->weekday = 0;
        recurrence->monthDay = 0;
        return true;
    case ReminderRecurrenceType::Weekly:
        recurrence->monthDay = 0;
        if (recurrence->weekday < 1 || recurrence->weekday > 7)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("每周提醒的星期无效。");
            }
            return false;
        }
        return true;
    case ReminderRecurrenceType::Monthly:
        recurrence->weekday = 0;
        if (recurrence->monthDay < 1 || recurrence->monthDay > 31)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("每月提醒的日期无效。");
            }
            return false;
        }
        return true;
    case ReminderRecurrenceType::None:
        *recurrence = {};
        return true;
    }

    *recurrence = {};
    return true;
}
}

ReminderManager::ReminderManager()
{
    QObject::connect(&m_timer, &QTimer::timeout, [this]() {
        processDueReminders();
    });
    QObject::connect(&m_guardTimer, &QTimer::timeout, [this]() {
        checkDueRemindersNow();
    });
    m_timer.setSingleShot(true);
    load();
}

void ReminderManager::start()
{
    if (!m_started)
    {
        m_started = true;
    }

    if (!m_guardTimer.isActive())
    {
        m_guardTimer.start(GuardTimerIntervalMs);
    }

    checkDueRemindersNow();
}

void ReminderManager::setTriggeredCallback(TriggeredCallback callback)
{
    m_triggeredCallback = std::move(callback);
}

QVector<ReminderItem> ReminderManager::allReminders() const
{
    return sortedReminders(m_items);
}

QVector<ReminderItem> ReminderManager::pendingReminders() const
{
    QVector<ReminderItem> result;
    for (const ReminderItem &item : m_items)
    {
        if (isPending(item))
        {
            result.append(item);
        }
    }
    return sortedReminders(result);
}

ReminderCommand ReminderManager::parseCommand(const QString &text) const
{
    return m_parser.parse(text);
}

void ReminderManager::checkDueRemindersNow()
{
    if (!m_started)
    {
        return;
    }

    processDueReminders();
    QString errorMessage;
    if (!pruneFinishedReminders(DefaultHistoryRetentionDays, &errorMessage))
    {
        Logger::warning(QStringLiteral("Failed to prune old reminder history: ") + errorMessage);
    }
}

bool ReminderManager::createReminder(
    const QString &title,
    const QString &originalText,
    const QDateTime &remindAt,
    ReminderItem *createdItem,
    QString *errorMessage)
{
    return createReminder(title, originalText, remindAt, ReminderRecurrence{}, createdItem, errorMessage);
}

bool ReminderManager::createReminder(
    const QString &title,
    const QString &originalText,
    const QDateTime &remindAt,
    const ReminderRecurrence &recurrence,
    ReminderItem *createdItem,
    QString *errorMessage)
{
    if (!remindAt.isValid() || remindAt <= QDateTime::currentDateTime())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提醒时间必须晚于当前时间。");
        }
        return false;
    }

    ReminderRecurrence normalizedRecurrence = recurrence;
    if (!normalizeRecurrence(&normalizedRecurrence, errorMessage))
    {
        return false;
    }

    ReminderItem item;
    item.id = nextReminderId();
    item.title = title.trimmed().isEmpty() ? QStringLiteral("提醒") : title.trimmed();
    item.originalText = originalText;
    item.remindAt = remindAt;
    item.status = ReminderStatus::Pending;
    item.createdAt = QDateTime::currentDateTime();
    item.soundId.clear();
    item.recurrence = normalizedRecurrence;
    if (!reminderIsRecurring(item.recurrence))
    {
        item.recurrence = {};
    }

    m_items.append(item);
    if (!save(errorMessage))
    {
        m_items.removeLast();
        return false;
    }

    if (createdItem != nullptr)
    {
        *createdItem = item;
    }

    scheduleNextReminder();
    return true;
}

bool ReminderManager::snoozeReminder(
    const ReminderItem &sourceItem,
    int minutes,
    ReminderItem *createdItem,
    QString *errorMessage)
{
    if (minutes <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("稍后提醒时间必须晚于当前时间。");
        }
        return false;
    }

    const QString originalText = sourceItem.originalText.trimmed().isEmpty()
        ? sourceItem.title
        : sourceItem.originalText;
    return createReminder(
        sourceItem.title,
        originalText,
        QDateTime::currentDateTime().addSecs(minutes * 60),
        ReminderRecurrence{},
        createdItem,
        errorMessage);
}

bool ReminderManager::cancelReminder(const QString &id, QString *errorMessage)
{
    const QString normalizedId = id.trimmed();
    for (ReminderItem &item : m_items)
    {
        if (item.id == normalizedId && isPending(item))
        {
            const QDateTime previousFinishedAt = item.finishedAt;
            item.status = ReminderStatus::Canceled;
            item.finishedAt = QDateTime::currentDateTime();
            const bool saved = save(errorMessage);
            if (!saved)
            {
                item.status = ReminderStatus::Pending;
                item.finishedAt = previousFinishedAt;
            }
            scheduleNextReminder();
            return saved;
        }
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("没有找到可取消的提醒。");
    }
    return false;
}

bool ReminderManager::cancelReminderByQuery(const QString &query, ReminderItem *canceledItem, QString *errorMessage)
{
    QVector<int> matches;
    for (int index = 0; index < m_items.size(); ++index)
    {
        if (isPending(m_items.at(index)) && textMatchesReminder(m_items.at(index), query))
        {
            matches.append(index);
        }
    }

    if (matches.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("没有找到匹配的待提醒事项。");
        }
        return false;
    }

    if (matches.size() > 1)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("找到多条匹配提醒，请在设置页中选择具体提醒取消。");
        }
        return false;
    }

    ReminderItem &item = m_items[matches.first()];
    const QDateTime previousFinishedAt = item.finishedAt;
    item.status = ReminderStatus::Canceled;
    item.finishedAt = QDateTime::currentDateTime();

    const bool saved = save(errorMessage);
    if (!saved)
    {
        item.status = ReminderStatus::Pending;
        item.finishedAt = previousFinishedAt;
    }
    else if (canceledItem != nullptr)
    {
        *canceledItem = item;
    }
    scheduleNextReminder();
    return saved;
}

bool ReminderManager::clearFinishedReminders(QString *errorMessage)
{
    return pruneFinishedReminders(DefaultHistoryRetentionDays, errorMessage);
}

bool ReminderManager::pruneFinishedReminders(int retentionDays, QString *errorMessage)
{
    QVector<ReminderItem> previousItems = m_items;
    const int normalizedRetentionDays = qMax(0, retentionDays);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-normalizedRetentionDays);
    for (int index = m_items.size() - 1; index >= 0; --index)
    {
        const ReminderItem &item = m_items.at(index);
        if (isFinished(item) && finishedReferenceTime(item) < cutoff)
        {
            m_items.removeAt(index);
        }
    }

    if (m_items.size() == previousItems.size())
    {
        return true;
    }

    if (!save(errorMessage))
    {
        m_items = previousItems;
        return false;
    }

    scheduleNextReminder();
    return true;
}

bool ReminderManager::updateReminder(
    const QString &id,
    const QString &title,
    const QDateTime &remindAt,
    const ReminderRecurrence &recurrence,
    QString *errorMessage)
{
    const QString normalizedId = id.trimmed();
    for (ReminderItem &item : m_items)
    {
        if (item.id != normalizedId || !isPending(item))
        {
            continue;
        }

        const ReminderItem previousItem = item;
        const QDateTime now = QDateTime::currentDateTime();
        ReminderRecurrence normalizedRecurrence = recurrence;
        if (!normalizeRecurrence(&normalizedRecurrence, errorMessage))
        {
            return false;
        }

        item.title = title.trimmed().isEmpty() ? QStringLiteral("提醒") : title.trimmed();
        item.finishedAt = {};
        if (reminderIsRecurring(normalizedRecurrence))
        {
            item.recurrence = normalizedRecurrence;
            item.remindAt = nextRecurringOccurrence(item, now);
            if (!item.remindAt.isValid())
            {
                item = previousItem;
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("没有找到有效的重复提醒时间。");
                }
                return false;
            }
        }
        else
        {
            if (!remindAt.isValid() || remindAt <= now)
            {
                item = previousItem;
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("提醒时间必须晚于当前时间。");
                }
                return false;
            }

            item.remindAt = remindAt;
            item.recurrence = {};
        }

        if (!save(errorMessage))
        {
            item = previousItem;
            return false;
        }

        scheduleNextReminder();
        return true;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = QStringLiteral("没有找到可编辑的待提醒事项。");
    }
    return false;
}

QString ReminderManager::pendingReminderSummary() const
{
    const QVector<ReminderItem> reminders = pendingReminders();
    if (reminders.isEmpty())
    {
        return QStringLiteral("当前没有待提醒事项。");
    }

    QStringList lines;
    lines.append(QStringLiteral("当前待提醒："));
    for (const ReminderItem &item : reminders)
    {
        lines.append(QStringLiteral("%1：%2（%3，下一次：%4）")
            .arg(item.id, item.title, reminderRecurrenceDisplayText(item), reminderDisplayTime(item.remindAt)));
    }
    return lines.join(QChar('\n'));
}

void ReminderManager::load()
{
    QString errorMessage;
    m_items = m_store.load(&errorMessage);
    if (!errorMessage.isEmpty())
    {
        Logger::warning(QStringLiteral("Failed to load reminders: ") + errorMessage);
    }
}

bool ReminderManager::save(QString *errorMessage) const
{
    return m_store.save(m_items, errorMessage);
}

void ReminderManager::processDueReminders()
{
    const QDateTime now = QDateTime::currentDateTime();
    QVector<ReminderItem> triggeredItems;
    const QVector<ReminderItem> previousItems = m_items;
    for (int index = 0; index < m_items.size(); ++index)
    {
        ReminderItem &item = m_items[index];
        if (isPending(item) && item.remindAt <= now)
        {
            ReminderItem triggeredItem = item;
            triggeredItem.status = ReminderStatus::Triggered;
            triggeredItem.finishedAt = now;
            triggeredItem.recurrence.lastTriggeredAt = now;
            if (reminderIsRecurring(item))
            {
                const QDateTime nextOccurrence = nextRecurringOccurrence(item, now);
                if (!nextOccurrence.isValid())
                {
                    Logger::warning(QStringLiteral("Failed to compute next recurring reminder occurrence: id=%1").arg(item.id));
                    item.status = ReminderStatus::Triggered;
                    item.finishedAt = now;
                }
                else
                {
                    item.recurrence.lastTriggeredAt = now;
                    item.remindAt = nextOccurrence;
                    item.finishedAt = {};
                }
            }
            else
            {
                item.status = ReminderStatus::Triggered;
                item.finishedAt = now;
            }

            triggeredItems.append(triggeredItem);
        }
    }

    if (!triggeredItems.isEmpty())
    {
        QString errorMessage;
        if (!save(&errorMessage))
        {
            Logger::warning(QStringLiteral("Failed to save triggered reminders: ") + errorMessage);
            m_items = previousItems;
            scheduleNextReminder();
            return;
        }

        for (const ReminderItem &item : triggeredItems)
        {
            if (m_triggeredCallback)
            {
                m_triggeredCallback(item);
            }
        }
    }

    scheduleNextReminder();
}

void ReminderManager::scheduleNextReminder()
{
    m_timer.stop();
    if (!m_started)
    {
        return;
    }

    QDateTime nextReminderTime;
    for (const ReminderItem &item : m_items)
    {
        if (!isPending(item))
        {
            continue;
        }

        if (!nextReminderTime.isValid() || item.remindAt < nextReminderTime)
        {
            nextReminderTime = item.remindAt;
        }
    }

    if (!nextReminderTime.isValid())
    {
        return;
    }

    const qint64 delayMs = QDateTime::currentDateTime().msecsTo(nextReminderTime);
    const int timerInterval = static_cast<int>(qBound<qint64>(MinimumTimerIntervalMs, delayMs, MaximumTimerIntervalMs));
    m_timer.start(timerInterval);
}

QString ReminderManager::nextReminderId() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const quint32 randomValue = QRandomGenerator::global()->bounded(10000U);
    return QStringLiteral("reminder_%1_%2").arg(timestamp, QString::number(randomValue).rightJustified(4, QLatin1Char('0')));
}
