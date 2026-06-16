#include "ReminderCommandHandler.h"

#include "ReminderManager.h"

ReminderCommandResult ReminderCommandHandler::handle(const QString &text, ReminderManager &manager)
{
    const ReminderCommand command = manager.parseCommand(text);
    switch (command.type)
    {
    case ReminderCommandType::Create:
    {
        ReminderItem item;
        QString errorMessage;
        if (!manager.createReminder(command.title, command.originalText, command.remindAt, command.recurrence, &item, &errorMessage))
        {
            return {false, errorMessage.isEmpty() ? QStringLiteral("创建提醒失败。") : errorMessage, {}};
        }

        if (reminderIsRecurring(item))
        {
            return {
                true,
                QStringLiteral("已设置重复提醒：%1，规则：%2，下一次：%3")
                    .arg(item.title, reminderRecurrenceDisplayText(item), reminderDisplayTime(item.remindAt)),
                item,
            };
        }

        return {
            true,
            QStringLiteral("已设置提醒：%1，时间：%2").arg(item.title, reminderDisplayTime(item.remindAt)),
            item,
        };
    }
    case ReminderCommandType::List:
        return {true, manager.pendingReminderSummary(), {}};
    case ReminderCommandType::Cancel:
    {
        ReminderItem item;
        QString errorMessage;
        if (!manager.cancelReminderByQuery(command.cancelQuery, &item, &errorMessage))
        {
            return {false, errorMessage.isEmpty() ? QStringLiteral("取消提醒失败。") : errorMessage, {}};
        }

        return {
            true,
            QStringLiteral("已取消提醒：%1（%2）").arg(item.title, reminderDisplayTime(item.remindAt)),
            item,
        };
    }
    case ReminderCommandType::Invalid:
        return {false, command.errorMessage.isEmpty() ? QStringLiteral("没有识别到有效提醒命令。") : command.errorMessage, {}};
    }

    return {false, QStringLiteral("没有识别到有效提醒命令。"), {}};
}
