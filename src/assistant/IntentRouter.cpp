#include "IntentRouter.h"

#include <QStringList>

namespace
{
bool containsAny(const QString &text, const QStringList &keywords)
{
    for (const QString &keyword : keywords)
    {
        if (text.contains(keyword, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

bool isReminderIntent(const QString &text)
{
    static const QStringList keywords = {
        QStringLiteral("提醒"),
        QStringLiteral("提醒我"),
        QStringLiteral("叫我"),
        QStringLiteral("闹钟"),
        QStringLiteral("到点"),
    };
    return containsAny(text, keywords);
}

bool isWeatherIntent(const QString &text)
{
    static const QStringList keywords = {
        QStringLiteral("天气"),
        QStringLiteral("气温"),
        QStringLiteral("温度"),
        QStringLiteral("冷不冷"),
        QStringLiteral("热不热"),
        QStringLiteral("下雨"),
        QStringLiteral("降雨"),
        QStringLiteral("带伞"),
        QStringLiteral("刮风"),
        QStringLiteral("风力"),
        QStringLiteral("湿度"),
        QStringLiteral("空气质量"),
        QStringLiteral("雾霾"),
        QStringLiteral("适合出门"),
        QStringLiteral("穿什么"),
        QStringLiteral("外面怎么样"),
    };
    return containsAny(text, keywords);
}
}

UserIntent IntentRouter::route(const QString &text) const
{
    const QString trimmedText = text.trimmed();
    if (isReminderIntent(trimmedText))
    {
        return {UserIntentType::Reminder, trimmedText};
    }
    if (isWeatherIntent(trimmedText))
    {
        return {UserIntentType::Weather, trimmedText};
    }
    return {UserIntentType::Chat, trimmedText};
}
