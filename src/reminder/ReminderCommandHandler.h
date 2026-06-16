#pragma once

#include "ReminderTypes.h"

#include <QString>

class ReminderManager;

struct ReminderCommandResult
{
    bool success = false;
    QString message;
    ReminderItem item;
};

class ReminderCommandHandler
{
public:
    static ReminderCommandResult handle(const QString &text, ReminderManager &manager);
};
