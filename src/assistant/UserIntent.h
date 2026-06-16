#pragma once

#include <QString>

enum class UserIntentType
{
    Chat,
    Reminder,
    Weather,
    FileOperation,
    LaunchApp,
};

struct UserIntent
{
    UserIntentType type = UserIntentType::Chat;
    QString text;
};

QString userIntentTypeName(UserIntentType type);
