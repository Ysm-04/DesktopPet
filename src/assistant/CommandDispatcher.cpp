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
    case UserIntentType::FileOperation:
        return QStringLiteral("FileOperation");
    case UserIntentType::LaunchApp:
        return QStringLiteral("LaunchApp");
    }

    return QStringLiteral("Unknown");
}

CommandDispatchResult CommandDispatcher::dispatch(const QString &text) const
{
    const UserIntent intent = m_intentRouter.route(text);
    if (intent.type == UserIntentType::Chat)
    {
        return {CommandDispatchAction::Chat, intent, intent.text};
    }

    if (intent.type == UserIntentType::Reminder)
    {
        return {CommandDispatchAction::Reminder, intent, intent.text};
    }

    if (intent.type == UserIntentType::Weather)
    {
        return {CommandDispatchAction::Weather, intent, intent.text};
    }

    if (intent.type == UserIntentType::FileOperation)
    {
        return {CommandDispatchAction::FileOperation, intent, intent.text};
    }

    if (intent.type == UserIntentType::LaunchApp)
    {
        return {CommandDispatchAction::LaunchApp, intent, intent.text};
    }

    return {CommandDispatchAction::UnsupportedTool, intent, unsupportedToolMessage(intent.type)};
}

QString CommandDispatcher::unsupportedToolMessage(UserIntentType type) const
{
    switch (type)
    {
    case UserIntentType::Reminder:
        return QStringLiteral("提醒请求暂时无法处理，请稍后再试。");
    case UserIntentType::Weather:
        return QStringLiteral("天气查询请求暂时无法处理，请稍后再试。");
    case UserIntentType::FileOperation:
        return QStringLiteral("本地文件操作请求暂时无法处理，请稍后再试。");
    case UserIntentType::LaunchApp:
        return QStringLiteral("应用启动请求暂时无法处理，请稍后再试。");
    case UserIntentType::Chat:
        return QString();
    }

    return QStringLiteral("该请求暂时无法处理，请稍后再试。");
}
