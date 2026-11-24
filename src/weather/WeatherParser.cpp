#include "WeatherParser.h"

#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace
{
int chineseDigitValue(QChar ch)
{
    switch (ch.unicode())
    {
    case u'零':
    case u'〇':
        return 0;
    case u'一':
        return 1;
    case u'二':
    case u'两':
        return 2;
    case u'三':
        return 3;
    case u'四':
        return 4;
    case u'五':
        return 5;
    case u'六':
        return 6;
    case u'七':
        return 7;
    case u'八':
        return 8;
    case u'九':
        return 9;
    default:
        return -1;
    }
}

int parseChineseOrArabicInt(const QString &rawToken)
{
    const QString token = rawToken.trimmed();
    if (token.isEmpty())
    {
        return 0;
    }

    bool ok = false;
    const int numeric = token.toInt(&ok);
    if (ok)
    {
        return numeric;
    }

    if (token == QStringLiteral("十"))
    {
        return 10;
    }

    const int tenIndex = token.indexOf(QLatin1Char('十'));
    if (tenIndex >= 0)
    {
        const QString tensPart = token.left(tenIndex);
        const QString onesPart = token.mid(tenIndex + 1);
        const int tens = tensPart.isEmpty()
            ? 1
            : (tensPart.size() == 1 ? chineseDigitValue(tensPart.at(0)) : -1);
        const int ones = onesPart.isEmpty()
            ? 0
            : (onesPart.size() == 1 ? chineseDigitValue(onesPart.at(0)) : -1);
        if (tens >= 1 && ones >= 0)
        {
            return tens * 10 + ones;
        }
        return 0;
    }

    if (token.size() == 1)
    {
        const int single = chineseDigitValue(token.at(0));
        return single >= 0 ? single : 0;
    }

    return 0;
}

const QRegularExpression &rangeForecastRegex()
{
    static const QRegularExpression regex(
        QStringLiteral("(?:未来|最近|接下来)\\s*([0-9]+|[一二两三四五六七八九十]+)\\s*天"));
    return regex;
}

const QRegularExpression &looseDayCountRegex()
{
    static const QRegularExpression regex(
        QStringLiteral("([0-9]+|[四五六七八九十]+)\\s*天"));
    return regex;
}

bool containsAnyPhrase(const QString &text, const QStringList &phrases)
{
    for (const QString &phrase : phrases)
    {
        if (text.contains(phrase, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

QStringList sortedByLengthDescending(QStringList phrases)
{
    std::sort(phrases.begin(), phrases.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    return phrases;
}

void removePhrasesOrdered(QString *text, const QStringList &phrases)
{
    if (text == nullptr)
    {
        return;
    }

    const QStringList ordered = sortedByLengthDescending(phrases);
    for (const QString &phrase : ordered)
    {
        text->remove(phrase, Qt::CaseInsensitive);
    }
}

QString trimEdgeSeparators(QString text)
{
    static const QRegularExpression edgeSeparators(QStringLiteral("^[\\s,，]+|[\\s,，]+$"));
    text.remove(edgeSeparators);
    return text.trimmed();
}

bool looksLikeUnparsedSentence(const QString &cityName)
{
    static const QStringList interrogatives = {
        QStringLiteral("为什么"),
        QStringLiteral("怎么"),
        QStringLiteral("什么"),
        QStringLiteral("哪里"),
        QStringLiteral("哪个"),
        QStringLiteral("谁"),
        QStringLiteral("多少"),
    };
    if (containsAnyPhrase(cityName, interrogatives))
    {
        return true;
    }

    if (cityName.contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]")))
        && cityName.size() > 24)
    {
        return true;
    }

    return false;
}

QString extractedCityName(QString text)
{
    text = text.trimmed();
    text.replace(QRegularExpression(QStringLiteral("[。！？、.!?；;：:]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));

    text.remove(rangeForecastRegex());
    text.remove(QRegularExpression(QStringLiteral(
        "(?:今明(?:两天|2天|天)|明后(?:两天|2天|天)|"
        "今天白天|今天晚上|今天早[上晨]?|明天白天|明天晚上|明天早[上晨]?|"
        "后天白天|后天晚上|大后天|今晚|明早|后天|明天|明日|今天|今日|"
        "现在|当前|实时|此刻|目前)")));

    static const QStringList removablePhrases = {
        // 口语客套与动作短语
        QStringLiteral("帮我查一下"),
        QStringLiteral("帮我查下"),
        QStringLiteral("帮我查查"),
        QStringLiteral("帮我看一下"),
        QStringLiteral("帮我看下"),
        QStringLiteral("帮我看看"),
        QStringLiteral("给我查一下"),
        QStringLiteral("给我查下"),
        QStringLiteral("给我查查"),
        QStringLiteral("给我看一下"),
        QStringLiteral("给我看下"),
        QStringLiteral("给我看看"),
        QStringLiteral("麻烦帮我"),
        QStringLiteral("请问一下"),
        QStringLiteral("查询一下"),
        QStringLiteral("搜索一下"),
        QStringLiteral("了解一下"),
        QStringLiteral("帮我查"),
        QStringLiteral("帮我看"),
        QStringLiteral("给我查"),
        QStringLiteral("给我看"),
        QStringLiteral("查询下"),
        QStringLiteral("查一下"),
        QStringLiteral("看一下"),
        QStringLiteral("搜一下"),
        QStringLiteral("了解下"),
        QStringLiteral("想问一下"),
        QStringLiteral("问一下"),
        QStringLiteral("能不能"),
        QStringLiteral("请问"),
        QStringLiteral("麻烦"),
        QStringLiteral("帮我"),
        QStringLiteral("帮忙"),
        QStringLiteral("给我"),
        QStringLiteral("告诉我"),
        QStringLiteral("想知道"),
        QStringLiteral("想问下"),
        QStringLiteral("查询"),
        QStringLiteral("查看"),
        QStringLiteral("查下"),
        QStringLiteral("查查"),
        QStringLiteral("看下"),
        QStringLiteral("看看"),
        QStringLiteral("搜索"),
        QStringLiteral("搜下"),
        QStringLiteral("一下子"),
        QStringLiteral("一下"),
        QStringLiteral("请查"),
        QStringLiteral("请看"),
        QStringLiteral("问下"),
        QStringLiteral("可以"),
        QStringLiteral("我想"),
        QStringLiteral("我要"),
        QStringLiteral("请"),

        // 天气与气象疑问短语
        QStringLiteral("适不适合出门"),
        QStringLiteral("需要带伞吗"),
        QStringLiteral("用不用带伞"),
        QStringLiteral("可能会下雨"),
        QStringLiteral("可能会下雪"),
        QStringLiteral("可能会刮风"),
        QStringLiteral("会不会下雨"),
        QStringLiteral("会不会下雪"),
        QStringLiteral("会不会刮风"),
        QStringLiteral("湿度怎么样"),
        QStringLiteral("湿度大不大"),
        QStringLiteral("天气怎么样"),
        QStringLiteral("气温怎么样"),
        QStringLiteral("温度怎么样"),
        QStringLiteral("外面怎么样"),
        QStringLiteral("外边怎么样"),
        QStringLiteral("室外怎么样"),
        QStringLiteral("还会下雨"),
        QStringLiteral("还会下雪"),
        QStringLiteral("还会刮风"),
        QStringLiteral("还会降温"),
        QStringLiteral("还会升温"),
        QStringLiteral("可能下雨"),
        QStringLiteral("可能下雪"),
        QStringLiteral("可能刮风"),
        QStringLiteral("要不要带伞"),
        QStringLiteral("要带伞吗"),
        QStringLiteral("带不带伞"),
        QStringLiteral("暖和不暖和"),
        QStringLiteral("会下雨吗"),
        QStringLiteral("下不下雨"),
        QStringLiteral("会下雪吗"),
        QStringLiteral("下不下雪"),
        QStringLiteral("会刮风吗"),
        QStringLiteral("刮不刮风"),
        QStringLiteral("适合出门吗"),
        QStringLiteral("天气情况"),
        QStringLiteral("天气状况"),
        QStringLiteral("天气预报"),
        QStringLiteral("天气如何"),
        QStringLiteral("气温如何"),
        QStringLiteral("气温多少"),
        QStringLiteral("温度如何"),
        QStringLiteral("温度多少"),
        QStringLiteral("湿度大吗"),
        QStringLiteral("湿度多少"),
        QStringLiteral("风大不大"),
        QStringLiteral("暴雨预警"),
        QStringLiteral("台风预警"),
        QStringLiteral("高温预警"),
        QStringLiteral("寒潮预警"),
        QStringLiteral("大风预警"),
        QStringLiteral("天气预警"),
        QStringLiteral("穿衣指数"),
        QStringLiteral("能出门吗"),
        QStringLiteral("需不需要"),
        QStringLiteral("空气质量"),
        QStringLiteral("还会冷"),
        QStringLiteral("还会热"),
        QStringLiteral("会下雨"),
        QStringLiteral("会下雪"),
        QStringLiteral("会刮风"),
        QStringLiteral("会降温"),
        QStringLiteral("会升温"),
        QStringLiteral("会变冷"),
        QStringLiteral("会变热"),
        QStringLiteral("要下雨"),
        QStringLiteral("要下雪"),
        QStringLiteral("要刮风"),
        QStringLiteral("要带伞"),
        QStringLiteral("要降温"),
        QStringLiteral("要升温"),
        QStringLiteral("下大雨"),
        QStringLiteral("下中雨"),
        QStringLiteral("下小雨"),
        QStringLiteral("下暴雨"),
        QStringLiteral("下大雪"),
        QStringLiteral("下中雪"),
        QStringLiteral("下小雪"),
        QStringLiteral("下暴雪"),
        QStringLiteral("雨夹雪"),
        QStringLiteral("下雨吗"),
        QStringLiteral("有雨吗"),
        QStringLiteral("有雨嘛"),
        QStringLiteral("下雪吗"),
        QStringLiteral("有雪吗"),
        QStringLiteral("有雪嘛"),
        QStringLiteral("刮风吗"),
        QStringLiteral("有风吗"),
        QStringLiteral("暖和吗"),
        QStringLiteral("暖和嘛"),
        QStringLiteral("凉快吗"),
        QStringLiteral("凉快嘛"),
        QStringLiteral("风大吗"),
        QStringLiteral("风大嘛"),
        QStringLiteral("冷不冷"),
        QStringLiteral("热不热"),
        QStringLiteral("适合出门"),
        QStringLiteral("穿什么"),
        QStringLiteral("多少度"),
        QStringLiteral("怎么样"),
        QStringLiteral("好不好"),
        QStringLiteral("大不大"),
        QStringLiteral("高不高"),
        QStringLiteral("低不低"),
        QStringLiteral("有没有"),
        QStringLiteral("会不会"),
        QStringLiteral("要不要"),
        QStringLiteral("用不用"),
        QStringLiteral("天气"),
        QStringLiteral("预报"),
        QStringLiteral("气温"),
        QStringLiteral("温度"),
        QStringLiteral("降雨"),
        QStringLiteral("降水"),
        QStringLiteral("下雨"),
        QStringLiteral("下雪"),
        QStringLiteral("降雪"),
        QStringLiteral("带伞"),
        QStringLiteral("刮风"),
        QStringLiteral("风力"),
        QStringLiteral("风速"),
        QStringLiteral("湿度"),
        QStringLiteral("雾霾"),
        QStringLiteral("预警"),
        QStringLiteral("AQI"),
        QStringLiteral("PM2.5"),
        QStringLiteral("能出门"),
        QStringLiteral("降温"),
        QStringLiteral("升温"),
        QStringLiteral("变冷"),
        QStringLiteral("变热"),
        QStringLiteral("转晴"),
        QStringLiteral("晴天"),
        QStringLiteral("阴天"),
        QStringLiteral("多云"),
        QStringLiteral("会冷"),
        QStringLiteral("会热"),
        QStringLiteral("会晴"),
        QStringLiteral("会阴"),
        QStringLiteral("会多云"),
        QStringLiteral("有雨"),
        QStringLiteral("有雪"),
        QStringLiteral("有风"),
        QStringLiteral("有雾"),
        QStringLiteral("几度"),
        QStringLiteral("冷吗"),
        QStringLiteral("冷嘛"),
        QStringLiteral("冷么"),
        QStringLiteral("热吗"),
        QStringLiteral("热嘛"),
        QStringLiteral("热么"),
        QStringLiteral("如何"),
        QStringLiteral("好吗"),
        QStringLiteral("好嘛"),
        QStringLiteral("需要"),
        QStringLiteral("到底"),
        QStringLiteral("究竟"),
        QStringLiteral("是否"),
        QStringLiteral("情况"),
        QStringLiteral("状况"),

        // 本地/泛指位置代词（清洗后留空以触发默认城市/IP回退）
        QStringLiteral("所在城市"),
        QStringLiteral("所在地区"),
        QStringLiteral("当前城市"),
        QStringLiteral("当前位置"),
        QStringLiteral("我这边"),
        QStringLiteral("我这里"),
        QStringLiteral("我这儿"),
        QStringLiteral("我这"),
        QStringLiteral("这边"),
        QStringLiteral("这里"),
        QStringLiteral("这儿"),
        QStringLiteral("本地"),
        QStringLiteral("当地"),
        QStringLiteral("外面"),
        QStringLiteral("外边"),
        QStringLiteral("室外"),
        QStringLiteral("户外"),
        QStringLiteral("附近"),
        QStringLiteral("出门"),
    };

    removePhrasesOrdered(&text, removablePhrases);

    // 循环清理边界助词/语气词及句首动词介词，避免破坏地名内部字符
    static const QRegularExpression edgeParticles(
        QStringLiteral("(?:吗|嘛|么|呢|啊|呀|吧|哦|噢|哈|呐)(?=\\s|$|[,，])"));
    static const QRegularExpression trailingPrepositionOrPossessive(QStringLiteral("(?:的|在)(?=\\s|$|[,，])"));
    static const QRegularExpression leadingUserMovePrefix(
        QStringLiteral("^(?:我|我们|咱|咱们)\\s*(?:在|去|到|来|回)"));
    static const QRegularExpression leadingPrepositionOrAction(
        QStringLiteral("^(?:在|去|到|关于|对于|的|查|看|搜|问)"));

    for (int pass = 0; pass < 4; ++pass)
    {
        const QString before = text;
        text = trimEdgeSeparators(text);
        text.remove(edgeParticles);
        text.remove(trailingPrepositionOrPossessive);
        text = trimEdgeSeparators(text);
        text.remove(leadingUserMovePrefix);
        if (!text.startsWith(QStringLiteral("查干")))
        {
            text.remove(leadingPrepositionOrAction);
        }
        text = trimEdgeSeparators(text);
        if (text == before)
        {
            break;
        }
    }

    if (text.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
    {
        text = text.simplified();
        text.replace(QRegularExpression(QStringLiteral("\\s*([,，])\\s*")), QStringLiteral("\\1 "));
        return trimEdgeSeparators(text);
    }

    text.remove(QLatin1Char(' '));
    text = trimEdgeSeparators(text);

    static const QStringList pureFunctionRemainders = {
        QStringLiteral("会"),
        QStringLiteral("要"),
        QStringLiteral("能"),
        QStringLiteral("有"),
        QStringLiteral("还"),
        QStringLiteral("可能"),
        QStringLiteral("大概"),
        QStringLiteral("应该"),
        QStringLiteral("是不是"),
        QStringLiteral("会不会"),
        QStringLiteral("要不要"),
        QStringLiteral("能不能"),
        QStringLiteral("有没有"),
        QStringLiteral("还会"),
        QStringLiteral("可能会"),
        QStringLiteral("应该会"),
        QStringLiteral("我"),
        QStringLiteral("我们"),
        QStringLiteral("咱"),
        QStringLiteral("咱们"),
        QStringLiteral("吧"),
        QStringLiteral("吗"),
        QStringLiteral("嘛"),
        QStringLiteral("么"),
        QStringLiteral("呢"),
        QStringLiteral("啊"),
        QStringLiteral("呀"),
        QStringLiteral("哦"),
        QStringLiteral("噢"),
        QStringLiteral("哈"),
        QStringLiteral("呐"),
    };
    if (pureFunctionRemainders.contains(text))
    {
        return {};
    }

    return text;
}

QString unsupportedMultiDayMessage()
{
    return QStringLiteral("基础天气最多支持未来 3 天预报，更长周期预报可开启联网开关后再试。");
}
}

WeatherQuery WeatherParser::parse(const QString &text) const
{
    WeatherQuery query;
    query.originalText = text.trimmed();
    if (query.originalText.isEmpty())
    {
        query.unsupportedReason = QStringLiteral("天气查询内容为空。");
        return query;
    }

    if (query.originalText.contains(QStringLiteral("空气质量"), Qt::CaseInsensitive)
        || query.originalText.contains(QStringLiteral("AQI"), Qt::CaseInsensitive)
        || query.originalText.contains(QStringLiteral("PM2.5"), Qt::CaseInsensitive)
        || query.originalText.contains(QStringLiteral("雾霾"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("基础天气暂不支持空气质量或雾霾查询，可开启联网开关后再试。");
        return query;
    }

    if (query.originalText.contains(QStringLiteral("预警"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("基础天气暂不支持天气预警查询，可开启联网开关后再试。");
        return query;
    }

    if (query.originalText.contains(QStringLiteral("穿衣指数"), Qt::CaseInsensitive)
        || query.originalText.contains(QStringLiteral("穿什么"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("基础天气暂不支持穿衣建议查询，可开启联网开关后再试。");
        return query;
    }

    const QRegularExpressionMatch explicitRangeMatch = rangeForecastRegex().match(query.originalText);
    if (explicitRangeMatch.hasMatch())
    {
        const int days = parseChineseOrArabicInt(explicitRangeMatch.captured(1));
        if (days > 3)
        {
            query.unsupportedReason = unsupportedMultiDayMessage();
            return query;
        }
        if (days >= 1)
        {
            query.kind = WeatherQueryKind::Range;
            query.dateOffset = 0;
            query.forecastDays = days;
        }
    }
    else
    {
        static const QStringList multiWeekKeywords = {
            QStringLiteral("未来一周"),
            QStringLiteral("未来1周"),
            QStringLiteral("未来一星期"),
            QStringLiteral("未来1星期"),
            QStringLiteral("未来两周"),
            QStringLiteral("未来2周"),
            QStringLiteral("本周"),
            QStringLiteral("这周"),
            QStringLiteral("这一周"),
            QStringLiteral("下周"),
            QStringLiteral("下一周"),
            QStringLiteral("这一星期"),
            QStringLiteral("一周"),
            QStringLiteral("半个月"),
            QStringLiteral("一个月"),
        };
        if (containsAnyPhrase(query.originalText, multiWeekKeywords))
        {
            query.unsupportedReason = unsupportedMultiDayMessage();
            return query;
        }

        const QRegularExpressionMatch looseDayMatch = looseDayCountRegex().match(query.originalText);
        if (looseDayMatch.hasMatch() && parseChineseOrArabicInt(looseDayMatch.captured(1)) > 3)
        {
            query.unsupportedReason = unsupportedMultiDayMessage();
            return query;
        }
    }

    if (query.kind != WeatherQueryKind::Range)
    {
        if (query.originalText.contains(QStringLiteral("今明两天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("今明2天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("今明天"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Range;
            query.dateOffset = 0;
            query.forecastDays = 2;
        }
        else if (query.originalText.contains(QStringLiteral("明后两天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("明后2天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("明后天"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Range;
            query.dateOffset = 1;
            query.forecastDays = 2;
        }
        else if (query.originalText.contains(QStringLiteral("大后天"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Daily;
            query.dateOffset = 3;
            query.forecastDays = 1;
        }
        else if (query.originalText.contains(QStringLiteral("后天"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Daily;
            query.dateOffset = 2;
            query.forecastDays = 1;
        }
        else if (query.originalText.contains(QStringLiteral("明天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("明日"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Daily;
            query.dateOffset = 1;
            query.forecastDays = 1;
        }
        else if (query.originalText.contains(QStringLiteral("今天"), Qt::CaseInsensitive)
            || query.originalText.contains(QStringLiteral("今日"), Qt::CaseInsensitive))
        {
            query.kind = WeatherQueryKind::Daily;
            query.dateOffset = 0;
            query.forecastDays = 1;
        }
    }

    query.cityName = extractedCityName(query.originalText);
    query.forecastDays = qBound(1, query.forecastDays, 3);
    if (!query.cityName.isEmpty() && looksLikeUnparsedSentence(query.cityName))
    {
        query.cityName.clear();
        query.unsupportedReason = QStringLiteral("没有识别到有效的天气查询城市，请尝试直接输入“城市 + 天气”，或开启联网开关查询。");
    }
    return query;
}
