#include "CommandDispatcher.h"

QString userIntentTypeName(UserIntentType type)
{
    switch (type)
    {
    case UserIntentType::Chat:
        return QStringLiteral("Chat");
    case UserIntentType::Reminder:
        return QStringLiteral("Reminder");
    case UserIntentType::Weather:
        return QStringLiteral("Weather");
    }

    return QStringLiteral("Unknown");
}

CommandDispatchResult CommandDispatcher::dispatch(const QString &text) const
{
    const UserIntent intent = m_intentRouter.route(text);
    switch (intent.type)
    {
    case UserIntentType::Chat:
        return {CommandDispatchAction::Chat, intent, intent.text};
    case UserIntentType::Reminder:
        return {CommandDispatchAction::Reminder, intent, intent.text};
    case UserIntentType::Weather:
        return {CommandDispatchAction::Weather, intent, intent.text};
    }

    return {CommandDispatchAction::Chat, intent, intent.text};
}
