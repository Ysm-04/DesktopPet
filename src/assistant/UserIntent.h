#pragma once

#include <QString>

enum class UserIntentType
{
    Chat,
    Reminder,
    Weather,
};

struct UserIntent
{
    UserIntentType type = UserIntentType::Chat;
    QString text;
};

QString userIntentTypeName(UserIntentType type);
