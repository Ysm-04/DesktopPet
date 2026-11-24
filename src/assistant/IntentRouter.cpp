#include "IntentRouter.h"

#include <QRegularExpression>
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

bool hasReminderAddressingExclusion(const QString &text)
{
    static const QStringList addressingExclusions = {
        QStringLiteral("叫我什么"),
        QStringLiteral("叫我啥"),
        QStringLiteral("应该叫我"),
        QStringLiteral("该叫我"),
        QStringLiteral("怎么叫我"),
        QStringLiteral("如何叫我"),
        QStringLiteral("别叫我"),
        QStringLiteral("不要叫我"),
        QStringLiteral("不用叫我"),
        QStringLiteral("可以叫我"),
        QStringLiteral("就叫我"),
        QStringLiteral("改叫我"),
        QStringLiteral("称呼我"),
        QStringLiteral("叫我一声"),
    };
    return containsAny(text, addressingExclusions);
}

bool hasReminderTimeContext(const QString &text)
{
    static const QStringList timeOrScheduleKeywords = {
        QStringLiteral("分钟后"),
        QStringLiteral("小时后"),
        QStringLiteral("秒后"),
        QStringLiteral("天后"),
        QStringLiteral("半小时"),
        QStringLiteral("一刻钟"),
        QStringLiteral("每天"),
        QStringLiteral("每日"),
        QStringLiteral("每周"),
        QStringLiteral("每星期"),
        QStringLiteral("下周"),
        QStringLiteral("每月"),
        QStringLiteral("工作日"),
        QStringLiteral("农历"),
        QStringLiteral("定时"),
        QStringLiteral("准时"),
        QStringLiteral("起床"),
        QStringLiteral("叫醒"),
        QStringLiteral("到时间"),
        QStringLiteral("到时候"),
        QStringLiteral("今晚"),
        QStringLiteral("明早"),
        QStringLiteral("明晚"),
    };
    if (containsAny(text, timeOrScheduleKeywords))
    {
        return true;
    }

    static const QRegularExpression colonTimePattern(
        QStringLiteral("\\d{1,2}\\s*[:：]\\s*\\d{1,2}"));
    static const QRegularExpression clockHourPattern(
        QStringLiteral("(?:\\d{1,2}|[一二两三四五六七八九十]+)\\s*点(?:\\s*(?:半|钟|\\d{1,2}\\s*分?|[一二三四五六七八九十]+\\s*分?))?"));
    static const QRegularExpression relativeDurationPattern(
        QStringLiteral("(?:\\d+|[一二两三四五六七八九十半]+)\\s*(?:个?小时|分钟|分|秒)\\s*(?:后|以后|之后)"));
    static const QRegularExpression monthDayPattern(
        QStringLiteral("\\d{1,2}\\s*(?:月|/)\\s*\\d{1,2}"));

    return colonTimePattern.match(text).hasMatch()
        || clockHourPattern.match(text).hasMatch()
        || relativeDurationPattern.match(text).hasMatch()
        || monthDayPattern.match(text).hasMatch();
}

bool isReminderIntent(const QString &text)
{
    static const QStringList strongKeywords = {
        QStringLiteral("提醒"),
        QStringLiteral("闹钟"),
    };
    if (containsAny(text, strongKeywords))
    {
        return true;
    }

    if (text.contains(QStringLiteral("叫我"), Qt::CaseInsensitive))
    {
        if (hasReminderAddressingExclusion(text))
        {
            return false;
        }
        return hasReminderTimeContext(text);
    }

    if (text.contains(QStringLiteral("到点"), Qt::CaseInsensitive))
    {
        return hasReminderTimeContext(text);
    }

    return false;
}

bool hasWeatherExclusion(const QString &text)
{
    static const QStringList creativeOrKnowledgeKeywords = {
        QStringLiteral("写一首"),
        QStringLiteral("写一段"),
        QStringLiteral("写篇"),
        QStringLiteral("写个"),
        QStringLiteral("作文"),
        QStringLiteral("翻译"),
        QStringLiteral("解释一下"),
        QStringLiteral("解释下"),
        QStringLiteral("什么是"),
        QStringLiteral("什么叫"),
        QStringLiteral("为什么会"),
        QStringLiteral("原理"),
        QStringLiteral("成因"),
        QStringLiteral("形成原因"),
        QStringLiteral("成语"),
        QStringLiteral("诗词"),
        QStringLiteral("歌词"),
        QStringLiteral("心情"),
        QStringLiteral("比喻"),
    };
    if (containsAny(text, creativeOrKnowledgeKeywords))
    {
        return true;
    }

    const bool hasExplicitWeatherTopic =
        text.contains(QStringLiteral("天气"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("气温"), Qt::CaseInsensitive);
    if (!hasExplicitWeatherTopic)
    {
        static const QStringList nonWeatherTempKeywords = {
            QStringLiteral("CPU"),
            QStringLiteral("GPU"),
            QStringLiteral("显卡"),
            QStringLiteral("主板"),
            QStringLiteral("硬盘"),
            QStringLiteral("内存"),
            QStringLiteral("电池"),
            QStringLiteral("手机"),
            QStringLiteral("电脑"),
            QStringLiteral("散热"),
            QStringLiteral("水温"),
            QStringLiteral("体温"),
            QStringLiteral("发烧"),
            QStringLiteral("烧水"),
            QStringLiteral("沸腾"),
            QStringLiteral("沸点"),
            QStringLiteral("冰点"),
            QStringLiteral("熔点"),
            QStringLiteral("油温"),
            QStringLiteral("炉温"),
            QStringLiteral("参数"),
            QStringLiteral("大模型"),
            QStringLiteral("模型"),
            QStringLiteral("temperature"),
            QStringLiteral("加湿器"),
            QStringLiteral("除湿"),
            QStringLiteral("烤箱"),
            QStringLiteral("冰箱"),
            QStringLiteral("热水器"),
            QStringLiteral("地暖"),
            QStringLiteral("空调温度"),
            QStringLiteral("室内温度"),
            QStringLiteral("室温"),
            QStringLiteral("调到"),
            QStringLiteral("设多少"),
        };
        if (containsAny(text, nonWeatherTempKeywords))
        {
            return true;
        }
    }

    static const QStringList weatherContextForOutfit = {
        QStringLiteral("天气"),
        QStringLiteral("气温"),
        QStringLiteral("冷不冷"),
        QStringLiteral("热不热"),
        QStringLiteral("下雨"),
        QStringLiteral("下雪"),
    };
    if (!containsAny(text, weatherContextForOutfit))
    {
        static const QStringList nonWeatherOutfitKeywords = {
            QStringLiteral("面试"),
            QStringLiteral("约会"),
            QStringLiteral("婚礼"),
            QStringLiteral("年会"),
            QStringLiteral("晚宴"),
            QStringLiteral("聚会"),
            QStringLiteral("拍照"),
            QStringLiteral("职场"),
            QStringLiteral("穿搭风格"),
        };
        if (containsAny(text, nonWeatherOutfitKeywords))
        {
            return true;
        }
    }

    return false;
}

bool isWeatherIntent(const QString &text)
{
    if (hasWeatherExclusion(text))
    {
        return false;
    }

    static const QStringList strongKeywords = {
        QStringLiteral("天气"),
        QStringLiteral("气温"),
        QStringLiteral("冷不冷"),
        QStringLiteral("热不热"),
        QStringLiteral("冷吗"),
        QStringLiteral("冷嘛"),
        QStringLiteral("冷么"),
        QStringLiteral("热吗"),
        QStringLiteral("热嘛"),
        QStringLiteral("热么"),
        QStringLiteral("下雨"),
        QStringLiteral("下不下雨"),
        QStringLiteral("有雨吗"),
        QStringLiteral("有雨嘛"),
        QStringLiteral("降雨"),
        QStringLiteral("降水"),
        QStringLiteral("下雪"),
        QStringLiteral("下不下雪"),
        QStringLiteral("有雪吗"),
        QStringLiteral("有雪嘛"),
        QStringLiteral("降雪"),
        QStringLiteral("带伞"),
        QStringLiteral("刮风"),
        QStringLiteral("风力"),
        QStringLiteral("风速"),
        QStringLiteral("空气质量"),
        QStringLiteral("AQI"),
        QStringLiteral("PM2.5"),
        QStringLiteral("雾霾"),
        QStringLiteral("天气预警"),
        QStringLiteral("暴雨预警"),
        QStringLiteral("台风预警"),
        QStringLiteral("高温预警"),
        QStringLiteral("寒潮预警"),
        QStringLiteral("大风预警"),
        QStringLiteral("适合出门"),
        QStringLiteral("外面怎么样"),
    };
    if (containsAny(text, strongKeywords))
    {
        return true;
    }

    if (text.contains(QStringLiteral("温度"), Qt::CaseInsensitive)
        || text.contains(QStringLiteral("湿度"), Qt::CaseInsensitive))
    {
        static const QStringList tempOrHumidityContexts = {
            QStringLiteral("今天"),
            QStringLiteral("今日"),
            QStringLiteral("明天"),
            QStringLiteral("明日"),
            QStringLiteral("后天"),
            QStringLiteral("大后天"),
            QStringLiteral("现在"),
            QStringLiteral("当前"),
            QStringLiteral("实时"),
            QStringLiteral("未来"),
            QStringLiteral("这周"),
            QStringLiteral("本周"),
            QStringLiteral("下周"),
            QStringLiteral("外面"),
            QStringLiteral("外边"),
            QStringLiteral("室外"),
            QStringLiteral("出门"),
            QStringLiteral("本地"),
            QStringLiteral("当地"),
            QStringLiteral("多少度"),
            QStringLiteral("几度"),
            QStringLiteral("怎么样"),
            QStringLiteral("如何"),
            QStringLiteral("高不高"),
            QStringLiteral("低不低"),
            QStringLiteral("大不大"),
        };
        if (containsAny(text, tempOrHumidityContexts))
        {
            return true;
        }

        QString compact = text.trimmed();
        compact.remove(QRegularExpression(QStringLiteral("[\\s，。！？,.!?；;：:]+")));
        if ((compact.endsWith(QStringLiteral("温度")) || compact.endsWith(QStringLiteral("湿度")))
            && compact.size() > 2
            && compact.size() <= 8)
        {
            return true;
        }
    }

    if (text.contains(QStringLiteral("穿什么"), Qt::CaseInsensitive))
    {
        static const QStringList outfitWeatherContexts = {
            QStringLiteral("今天"),
            QStringLiteral("今日"),
            QStringLiteral("明天"),
            QStringLiteral("明日"),
            QStringLiteral("后天"),
            QStringLiteral("大后天"),
            QStringLiteral("现在"),
            QStringLiteral("外面"),
            QStringLiteral("外边"),
            QStringLiteral("室外"),
            QStringLiteral("出门"),
            QStringLiteral("冷"),
            QStringLiteral("热"),
            QStringLiteral("穿衣指数"),
        };
        return containsAny(text, outfitWeatherContexts);
    }

    return false;
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
