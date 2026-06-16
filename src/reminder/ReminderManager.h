#pragma once

#include "ReminderParser.h"
#include "ReminderStore.h"

#include <QTimer>
#include <QVector>

#include <functional>

class ReminderManager
{
public:
    using TriggeredCallback = std::function<void(const ReminderItem &)>;

    ReminderManager();

    void start();
    void setTriggeredCallback(TriggeredCallback callback);

    QVector<ReminderItem> allReminders() const;
    QVector<ReminderItem> pendingReminders() const;
    ReminderCommand parseCommand(const QString &text) const;
    void checkDueRemindersNow();

    bool createReminder(
        const QString &title,
        const QString &originalText,
        const QDateTime &remindAt,
        ReminderItem *createdItem = nullptr,
        QString *errorMessage = nullptr);
    bool createReminder(
        const QString &title,
        const QString &originalText,
        const QDateTime &remindAt,
        const ReminderRecurrence &recurrence,
        ReminderItem *createdItem = nullptr,
        QString *errorMessage = nullptr);
    bool snoozeReminder(
        const ReminderItem &sourceItem,
        int minutes,
        ReminderItem *createdItem = nullptr,
        QString *errorMessage = nullptr);
    bool cancelReminder(const QString &id, QString *errorMessage = nullptr);
    bool cancelReminderByQuery(const QString &query, ReminderItem *canceledItem = nullptr, QString *errorMessage = nullptr);
    bool clearFinishedReminders(QString *errorMessage = nullptr);
    bool pruneFinishedReminders(int retentionDays = 20, QString *errorMessage = nullptr);
    bool updateReminder(
        const QString &id,
        const QString &title,
        const QDateTime &remindAt,
        const ReminderRecurrence &recurrence,
        QString *errorMessage = nullptr);
    QString pendingReminderSummary() const;

private:
    void load();
    bool save(QString *errorMessage = nullptr) const;
    void processDueReminders();
    void scheduleNextReminder();
    QString nextReminderId() const;

    ReminderStore m_store;
    ReminderParser m_parser;
    QVector<ReminderItem> m_items;
    QTimer m_timer;
    QTimer m_guardTimer;
    TriggeredCallback m_triggeredCallback;
    bool m_started = false;
};
