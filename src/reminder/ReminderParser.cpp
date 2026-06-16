#include "ReminderParser.h"

#include <QDate>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>

namespace
{
bool containsAny(const QString &text, const QStringList &keywords)
{
    for (const QString &keyword : keywords)
    {
        if (text.contains(keyword))
        {
            return true;
        }
    }

    return false;
}

int chineseDigitValue(QChar value)
{
    if (value == QLatin1Char('0') || value == QChar(0x3007) || value == QStringLiteral("零").at(0))
    {
        return 0;
    }
    if (value == QStringLiteral("一").at(0))
    {
        return 1;
    }
    if (value == QStringLiteral("二").at(0) || value == QStringLiteral("两").at(0))
    {
        return 2;
    }
    if (value == QStringLiteral("三").at(0))
    {
        return 3;
    }
    if (value == QStringLiteral("四").at(0))
    {
        return 4;
    }
    if (value == QStringLiteral("五").at(0))
    {
        return 5;
    }
    if (value == QStringLiteral("六").at(0))
    {
        return 6;
    }
    if (value == QStringLiteral("七").at(0))
    {
        return 7;
    }
    if (value == QStringLiteral("八").at(0))
    {
        return 8;
    }
    if (value == QStringLiteral("九").at(0))
    {
        return 9;
    }

    return -1;
}

int parseSmallInteger(QString value)
{
    value = value.trimmed();
    value.remove(QStringLiteral("个"));
    if (value.isEmpty())
    {
        return -1;
    }

    bool ok = false;
    const int numericValue = value.toInt(&ok);
    if (ok)
    {
        return numericValue;
    }

    const int tenIndex = value.indexOf(QStringLiteral("十"));
    if (tenIndex >= 0)
    {
        const QString left = value.left(tenIndex);
        const QString right = value.mid(tenIndex + 1);
        const int tens = left.isEmpty() ? 1 : parseSmallInteger(left);
        const int ones = right.isEmpty() ? 0 : parseSmallInteger(right);
        if (tens < 0 || ones < 0)
        {
            return -1;
        }
        return tens * 10 + ones;
    }

    if (value.size() == 1)
    {
        return chineseDigitValue(value.at(0));
    }

    return -1;
}

QString cleanedText(QString text)
{
    return text
        .replace(QChar(0xff0c), QStringLiteral(" "))
        .replace(QChar(0x3002), QStringLiteral(" "))
        .replace(QChar(0xff1a), QStringLiteral(":"))
        .replace(QChar(0xff1b), QStringLiteral(" "))
        .trimmed();
}

int adjustedHour(const QString &period, int hour)
{
    if (hour < 0)
    {
        return -1;
    }

    if (period == QStringLiteral("下午") || period == QStringLiteral("晚上"))
    {
        return hour < 12 ? hour + 12 : hour;
    }

    if (period == QStringLiteral("中午"))
    {
        return hour < 11 ? hour + 12 : hour;
    }

    if (period == QStringLiteral("凌晨") && hour == 12)
    {
        return 0;
    }

    return hour;
}

int weekdayFromText(const QString &text)
{
    const QString normalized = text.trimmed();
    if (normalized == QStringLiteral("一") || normalized == QStringLiteral("1"))
    {
        return 1;
    }
    if (normalized == QStringLiteral("二") || normalized == QStringLiteral("2"))
    {
        return 2;
    }
    if (normalized == QStringLiteral("三") || normalized == QStringLiteral("3"))
    {
        return 3;
    }
    if (normalized == QStringLiteral("四") || normalized == QStringLiteral("4"))
    {
        return 4;
    }
    if (normalized == QStringLiteral("五") || normalized == QStringLiteral("5"))
    {
        return 5;
    }
    if (normalized == QStringLiteral("六") || normalized == QStringLiteral("6"))
    {
        return 6;
    }
    if (normalized == QStringLiteral("日")
        || normalized == QStringLiteral("天")
        || normalized == QStringLiteral("7"))
    {
        return 7;
    }

    return -1;
}

struct ReminderDateResolution
{
    QDate date;
    bool explicitDate = false;
};

struct ParsedReminderTime
{
    int hour = -1;
    int minute = -1;
    QString expression;
};

ReminderDateResolution resolveReminderDate(const QString &text, const QDate &currentDate)
{
    QRegularExpressionMatch match;

    const QRegularExpression nextWeekExpression(QStringLiteral("下周\\s*([一二三四五六日天1-7])"));
    match = nextWeekExpression.match(text);
    if (match.hasMatch())
    {
        const int targetWeekday = weekdayFromText(match.captured(1));
        if (targetWeekday > 0)
        {
            const int daysToNextMonday = 8 - currentDate.dayOfWeek();
            return {currentDate.addDays(daysToNextMonday + targetWeekday - 1), true};
        }
    }

    const QRegularExpression monthDayExpression(QStringLiteral("(\\d{1,2})\\s*月\\s*(\\d{1,2})\\s*(?:日|号)?"));
    match = monthDayExpression.match(text);
    if (match.hasMatch())
    {
        const int month = match.captured(1).toInt();
        const int day = match.captured(2).toInt();
        QDate date(currentDate.year(), month, day);
        if (date.isValid() && date < currentDate)
        {
            date = date.addYears(1);
        }
        return {date, true};
    }

    const QRegularExpression slashDateExpression(QStringLiteral("(\\d{1,2})\\s*/\\s*(\\d{1,2})"));
    match = slashDateExpression.match(text);
    if (match.hasMatch())
    {
        const int month = match.captured(1).toInt();
        const int day = match.captured(2).toInt();
        QDate date(currentDate.year(), month, day);
        if (date.isValid() && date < currentDate)
        {
            date = date.addYears(1);
        }
        return {date, true};
    }

    if (text.contains(QStringLiteral("后天")))
    {
        return {currentDate.addDays(2), true};
    }

    if (text.contains(QStringLiteral("明天")))
    {
        return {currentDate.addDays(1), true};
    }

    if (text.contains(QStringLiteral("今天")))
    {
        return {currentDate, true};
    }

    return {currentDate, false};
}

QString removeFirst(const QString &text, const QString &part)
{
    QString result = text;
    const int index = result.indexOf(part);
    if (index >= 0)
    {
        result.remove(index, part.size());
    }
    return result;
}

ReminderCommand invalidRecurringCommand(const QString &text, const QString &message)
{
    return {ReminderCommandType::Invalid, {}, text, {}, {}, message};
}

ReminderCommand createReminderCommand(
    const QString &title,
    const QString &originalText,
    const QDateTime &remindAt,
    const ReminderRecurrence &recurrence = {})
{
    ReminderCommand command;
    command.type = ReminderCommandType::Create;
    command.title = title;
    command.originalText = originalText;
    command.remindAt = remindAt;
    command.recurrence = recurrence;
    return command;
}

ParsedReminderTime parsedTimeFromPointMatch(const QRegularExpressionMatch &match, int periodIndex, int hourIndex, int minuteIndex)
{
    const QString period = match.captured(periodIndex);
    const int hour = adjustedHour(period, parseSmallInteger(match.captured(hourIndex)));
    const int minute = match.captured(minuteIndex).isEmpty() ? 0 : match.captured(minuteIndex).toInt();
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
        return {};
    }

    return {hour, minute, match.captured(0)};
}

ParsedReminderTime parsedTimeFromColonMatch(const QRegularExpressionMatch &match, int hourIndex, int minuteIndex)
{
    const int hour = match.captured(hourIndex).toInt();
    const int minute = match.captured(minuteIndex).toInt();
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
        return {};
    }

    return {hour, minute, match.captured(0)};
}

ParsedReminderTime parseTimeExpression(const QString &text)
{
    QRegularExpressionMatch match;
    const QRegularExpression pointExpression(QStringLiteral("(上午|早上|下午|晚上|中午|凌晨)?\\s*([0-9]{1,2}|[一二两三四五六七八九十]+)\\s*点\\s*(?:(\\d{1,2})\\s*分?)?"));
    match = pointExpression.match(text);
    if (match.hasMatch())
    {
        return parsedTimeFromPointMatch(match, 1, 2, 3);
    }

    const QRegularExpression colonExpression(QStringLiteral("([01]?\\d|2[0-3])\\s*[:：]\\s*([0-5]\\d)"));
    match = colonExpression.match(text);
    if (match.hasMatch())
    {
        return parsedTimeFromColonMatch(match, 1, 2);
    }

    return {};
}

QDateTime nextDailyOccurrence(const QDateTime &now, const QTime &time)
{
    QDateTime next(now.date(), time);
    if (next <= now)
    {
        next = next.addDays(1);
    }
    return next;
}

QDateTime nextWeeklyOccurrence(const QDateTime &now, int weekday, const QTime &time)
{
    int daysToAdd = weekday - now.date().dayOfWeek();
    QDateTime next(now.date().addDays(daysToAdd), time);
    if (daysToAdd < 0 || next <= now)
    {
        next = next.addDays(7);
    }
    return next;
}

QDateTime nextMonthlyOccurrence(const QDateTime &now, int monthDay, const QTime &time)
{
    if (monthDay < 1 || monthDay > 31)
    {
        return {};
    }

    QDate monthCursor(now.date().year(), now.date().month(), 1);
    for (int attempt = 0; attempt < 240; ++attempt)
    {
        const QDate date(monthCursor.year(), monthCursor.month(), monthDay);
        if (date.isValid())
        {
            const QDateTime next(date, time);
            if (next > now)
            {
                return next;
            }
        }
        monthCursor = monthCursor.addMonths(1);
    }

    return {};
}

bool containsUnsupportedRecurrence(const QString &text)
{
    static const QRegularExpression intervalExpression(QStringLiteral("每\\s*([0-9]+|[一二两三四五六七八九十]+)\\s*(天|周|星期|月)"));
    return containsAny(text, {
        QStringLiteral("每年"),
        QStringLiteral("工作日"),
        QStringLiteral("重复"),
        QStringLiteral("每隔"),
        QStringLiteral("隔天"),
        QStringLiteral("每季度"),
        QStringLiteral("每季"),
        QStringLiteral("每小时"),
        QStringLiteral("每分钟"),
        QStringLiteral("农历"),
    }) || intervalExpression.match(text).hasMatch();
}
}

ReminderCommand ReminderParser::parse(const QString &text, const QDateTime &now) const
{
    const QString normalized = cleanedText(text);
    if (normalized.isEmpty())
    {
        return {ReminderCommandType::Invalid, {}, {}, {}, {}, QStringLiteral("提醒内容为空。")};
    }

    if (containsAny(normalized, {QStringLiteral("提醒列表"), QStringLiteral("查看提醒"), QStringLiteral("我的提醒"), QStringLiteral("有哪些提醒")}))
    {
        return {ReminderCommandType::List, {}, normalized};
    }

    if (normalized.contains(QStringLiteral("取消提醒")) || normalized.startsWith(QStringLiteral("取消")))
    {
        QString query = normalized;
        query.remove(QStringLiteral("取消提醒"));
        if (query == normalized)
        {
            query.remove(QStringLiteral("取消"));
            query.remove(QStringLiteral("提醒"));
        }
        query = query.trimmed();
        if (query.isEmpty())
        {
            return {ReminderCommandType::Invalid, {}, normalized, {}, {}, QStringLiteral("请说明要取消哪条提醒。")};
        }
        return {ReminderCommandType::Cancel, {}, normalized, {}, query};
    }

    return parseCreateCommand(normalized, now);
}

ReminderCommand ReminderParser::parseCreateCommand(const QString &text, const QDateTime &now) const
{
    const QDate currentDate = now.date();

    const ReminderCommand recurringCommand = parseRecurringCreateCommand(text, now);
    if (recurringCommand.type != ReminderCommandType::Invalid || !recurringCommand.errorMessage.isEmpty())
    {
        return recurringCommand;
    }

    if (containsUnsupportedRecurrence(text))
    {
        return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("暂不支持该重复提醒规则，目前支持每天、每周、每月。")};
    }

    const QRegularExpression relativeMinutesExpression(QStringLiteral("([0-9]+|[一二两三四五六七八九十]+)\\s*分钟后"));
    QRegularExpressionMatch match = relativeMinutesExpression.match(text);
    if (match.hasMatch())
    {
        const int minutes = parseSmallInteger(match.captured(1));
        if (minutes <= 0)
        {
            return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("提醒时间必须晚于当前时间。")};
        }

        const QString timeExpression = match.captured(0);
        return {
            ReminderCommandType::Create,
            extractTitle(text, timeExpression),
            text,
            now.addSecs(minutes * 60),
            {},
            {},
        };
    }

    const QRegularExpression relativeOneAndHalfHourExpression(QStringLiteral("([0-9]+|[一二两三四五六七八九十]+)\\s*(?:个)?半小时后"));
    match = relativeOneAndHalfHourExpression.match(text);
    if (match.hasMatch())
    {
        const int hours = parseSmallInteger(match.captured(1));
        if (hours < 0)
        {
            return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("没有识别到有效提醒时间。")};
        }

        const QString timeExpression = match.captured(0);
        return {
            ReminderCommandType::Create,
            extractTitle(text, timeExpression),
            text,
            now.addSecs(hours * 60 * 60 + 30 * 60),
            {},
            {},
        };
    }

    if (text.contains(QStringLiteral("半小时后")))
    {
        const QString timeExpression = QStringLiteral("半小时后");
        return {
            ReminderCommandType::Create,
            extractTitle(text, timeExpression),
            text,
            now.addSecs(30 * 60),
            {},
            {},
        };
    }

    const QRegularExpression relativeHoursExpression(QStringLiteral("([0-9]+|[一二两三四五六七八九十]+)\\s*(?:个)?小时后"));
    match = relativeHoursExpression.match(text);
    if (match.hasMatch())
    {
        const int hours = parseSmallInteger(match.captured(1));
        if (hours <= 0)
        {
            return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("提醒时间必须晚于当前时间。")};
        }

        const QString timeExpression = match.captured(0);
        return {
            ReminderCommandType::Create,
            extractTitle(text, timeExpression),
            text,
            now.addSecs(hours * 60 * 60),
            {},
            {},
        };
    }

    const ReminderDateResolution dateResolution = resolveReminderDate(text, currentDate);
    if (!dateResolution.date.isValid())
    {
        return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("没有识别到有效提醒日期。")};
    }

    const QRegularExpression clockExpression(QStringLiteral("(上午|早上|下午|晚上|中午|凌晨)?\\s*([0-9]{1,2}|[一二两三四五六七八九十]+)\\s*点\\s*(?:(\\d{1,2})\\s*分?)?"));
    match = clockExpression.match(text);
    if (match.hasMatch())
    {
        const QString period = match.captured(1);
        int hour = adjustedHour(period, parseSmallInteger(match.captured(2)));
        const int minute = match.captured(3).isEmpty() ? 0 : match.captured(3).toInt();
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
        {
            return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("没有识别到有效提醒时间。")};
        }

        QDateTime remindAt(dateResolution.date, QTime(hour, minute));
        if (remindAt <= now)
        {
            if (dateResolution.explicitDate)
            {
                return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("提醒时间必须晚于当前时间。")};
            }
            remindAt = remindAt.addDays(1);
        }

        return {
            ReminderCommandType::Create,
            extractTitle(text, match.captured(0)),
            text,
            remindAt,
            {},
            {},
        };
    }

    const QRegularExpression colonClockExpression(QStringLiteral("(明天)?\\s*(?:在)?\\s*([01]?\\d|2[0-3])\\s*[:：]\\s*([0-5]\\d)"));
    match = colonClockExpression.match(text);
    if (match.hasMatch())
    {
        QDateTime remindAt(dateResolution.date, QTime(match.captured(2).toInt(), match.captured(3).toInt()));
        if (remindAt <= now)
        {
            if (dateResolution.explicitDate)
            {
                return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("提醒时间必须晚于当前时间。")};
            }
            remindAt = remindAt.addDays(1);
        }

        return {
            ReminderCommandType::Create,
            extractTitle(text, match.captured(0)),
            text,
            remindAt,
            {},
            {},
        };
    }

    return {ReminderCommandType::Invalid, {}, text, {}, {}, QStringLiteral("没有识别到提醒时间。支持如“10分钟后提醒我喝水”“明天9点提醒我开会”。")};
}

ReminderCommand ReminderParser::parseRecurringCreateCommand(const QString &text, const QDateTime &now) const
{
    QRegularExpressionMatch match;

    if (text.contains(QStringLiteral("每月"))
        && (text.contains(QStringLiteral("最后")) || text.contains(QStringLiteral("月末"))))
    {
        return invalidRecurringCommand(text, QStringLiteral("暂不支持每月最后一天提醒，目前支持每月具体日期。"));
    }

    if (text.contains(QStringLiteral("每天")) || text.contains(QStringLiteral("每日")))
    {
        const ParsedReminderTime parsedTime = parseTimeExpression(text);
        if (parsedTime.hour < 0)
        {
            return invalidRecurringCommand(text, QStringLiteral("没有识别到重复提醒时间。支持如“每天9点提醒我打卡”。"));
        }

        const QTime time(parsedTime.hour, parsedTime.minute);
        ReminderRecurrence recurrence;
        recurrence.type = ReminderRecurrenceType::Daily;
        recurrence.interval = 1;
        recurrence.hour = parsedTime.hour;
        recurrence.minute = parsedTime.minute;
        return createReminderCommand(
            extractTitle(text, parsedTime.expression),
            text,
            nextDailyOccurrence(now, time),
            recurrence);
    }

    if (text.contains(QStringLiteral("每周")) || text.contains(QStringLiteral("每星期")))
    {
        const QRegularExpression weeklyExpression(QStringLiteral("(?:每周|每星期)\\s*([一二三四五六日天1-7])"));
        match = weeklyExpression.match(text);
        const int weekday = match.hasMatch() ? weekdayFromText(match.captured(1)) : -1;
        const ParsedReminderTime parsedTime = parseTimeExpression(text);

        if (weekday < 1 || parsedTime.hour < 0)
        {
            return invalidRecurringCommand(text, QStringLiteral("没有识别到重复提醒时间。支持如“每周一上午10点提醒我周会”。"));
        }

        const QTime time(parsedTime.hour, parsedTime.minute);
        ReminderRecurrence recurrence;
        recurrence.type = ReminderRecurrenceType::Weekly;
        recurrence.interval = 1;
        recurrence.weekday = weekday;
        recurrence.hour = parsedTime.hour;
        recurrence.minute = parsedTime.minute;
        return createReminderCommand(
            extractTitle(text, parsedTime.expression),
            text,
            nextWeeklyOccurrence(now, weekday, time),
            recurrence);
    }

    if (text.contains(QStringLiteral("每月")))
    {
        const QRegularExpression monthlyExpression(QStringLiteral("每月\\s*([0-9]{1,2}|[一二两三四五六七八九十]+)\\s*(?:日|号)?"));
        match = monthlyExpression.match(text);
        const int monthDay = match.hasMatch() ? parseSmallInteger(match.captured(1)) : -1;
        const ParsedReminderTime parsedTime = parseTimeExpression(text);

        if (monthDay < 1 || monthDay > 31 || parsedTime.hour < 0)
        {
            return invalidRecurringCommand(text, QStringLiteral("没有识别到重复提醒时间。支持如“每月3号9点提醒我交报告”。"));
        }

        const QTime time(parsedTime.hour, parsedTime.minute);
        const QDateTime remindAt = nextMonthlyOccurrence(now, monthDay, time);
        if (!remindAt.isValid())
        {
            return invalidRecurringCommand(text, QStringLiteral("没有找到有效的每月提醒日期。"));
        }

        ReminderRecurrence recurrence;
        recurrence.type = ReminderRecurrenceType::Monthly;
        recurrence.interval = 1;
        recurrence.monthDay = monthDay;
        recurrence.hour = parsedTime.hour;
        recurrence.minute = parsedTime.minute;
        return createReminderCommand(
            extractTitle(text, parsedTime.expression),
            text,
            remindAt,
            recurrence);
    }

    return {};
}

QString ReminderParser::extractTitle(QString text, const QString &timeExpression) const
{
    text = removeFirst(text, timeExpression);
    text.remove(QRegularExpression(QStringLiteral("\\d{1,2}\\s*月\\s*\\d{1,2}\\s*(?:日|号)?")));
    text.remove(QRegularExpression(QStringLiteral("\\d{1,2}\\s*/\\s*\\d{1,2}")));
    text.remove(QRegularExpression(QStringLiteral("下周\\s*[一二三四五六日天1-7]")));
    text.remove(QRegularExpression(QStringLiteral("每周\\s*[一二三四五六日天1-7]")));
    text.remove(QRegularExpression(QStringLiteral("每星期\\s*[一二三四五六日天1-7]")));
    text.remove(QRegularExpression(QStringLiteral("每月\\s*([0-9]{1,2}|[一二两三四五六七八九十]+)\\s*(?:日|号)?")));

    const QStringList tokensToRemove = {
        QStringLiteral("提醒我"),
        QStringLiteral("提醒"),
        QStringLiteral("叫我"),
        QStringLiteral("到点"),
        QStringLiteral("的时候"),
        QStringLiteral("请"),
        QStringLiteral("帮我"),
        QStringLiteral("今天"),
        QStringLiteral("明天"),
        QStringLiteral("后天"),
        QStringLiteral("每天"),
        QStringLiteral("每日"),
        QStringLiteral("每周"),
        QStringLiteral("每星期"),
        QStringLiteral("每月"),
    };

    for (const QString &token : tokensToRemove)
    {
        text.remove(token);
    }

    text = text.trimmed();
    while (text.startsWith(QChar(0x3000)) || text.startsWith(QLatin1Char(' ')) || text.startsWith(QLatin1Char(',')) || text.startsWith(QChar(0xff0c)))
    {
        text.remove(0, 1);
        text = text.trimmed();
    }

    return text.isEmpty() ? QStringLiteral("提醒") : text;
}
