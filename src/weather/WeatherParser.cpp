#include "WeatherParser.h"

#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace
{
QString simplifiedDigits(QString text)
{
    const QList<QPair<QString, QString>> replacements = {
        {QStringLiteral("一"), QStringLiteral("1")},
        {QStringLiteral("二"), QStringLiteral("2")},
        {QStringLiteral("两"), QStringLiteral("2")},
        {QStringLiteral("三"), QStringLiteral("3")},
    };
    for (const auto &replacement : replacements)
    {
        text.replace(replacement.first, replacement.second);
    }
    return text;
}

void removeAll(QString *text, const QStringList &patterns)
{
    for (const QString &pattern : patterns)
    {
        text->remove(pattern, Qt::CaseInsensitive);
    }
}

QString extractedCityName(QString text)
{
    text = simplifiedDigits(text.trimmed());
    text.remove(QRegularExpression(QStringLiteral("[，。！？、,.!?；;：:]+")));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));

    removeAll(&text, {
        QStringLiteral("请问"),
        QStringLiteral("帮我"),
        QStringLiteral("帮忙"),
        QStringLiteral("查询"),
        QStringLiteral("查一下"),
        QStringLiteral("查查"),
        QStringLiteral("看看"),
        QStringLiteral("看一下"),
        QStringLiteral("搜索一下"),
        QStringLiteral("搜一下"),
        QStringLiteral("一下"),
        QStringLiteral("今天"),
        QStringLiteral("现在"),
        QStringLiteral("当前"),
        QStringLiteral("实时"),
        QStringLiteral("明天"),
        QStringLiteral("后天"),
        QStringLiteral("未来1天"),
        QStringLiteral("未来2天"),
        QStringLiteral("未来3天"),
        QStringLiteral("未来一天"),
        QStringLiteral("未来二天"),
        QStringLiteral("未来两天"),
        QStringLiteral("未来三天"),
        QStringLiteral("天气怎么样"),
        QStringLiteral("天气如何"),
        QStringLiteral("天气"),
        QStringLiteral("气温"),
        QStringLiteral("温度"),
        QStringLiteral("冷不冷"),
        QStringLiteral("热不热"),
        QStringLiteral("会不会下雨"),
        QStringLiteral("会下雨吗"),
        QStringLiteral("下雨吗"),
        QStringLiteral("下雨"),
        QStringLiteral("降雨"),
        QStringLiteral("降水"),
        QStringLiteral("带伞"),
        QStringLiteral("风力"),
        QStringLiteral("风速"),
        QStringLiteral("湿度"),
        QStringLiteral("空气质量"),
        QStringLiteral("AQI"),
        QStringLiteral("aqi"),
        QStringLiteral("预警"),
        QStringLiteral("穿衣指数"),
        QStringLiteral("穿什么"),
        QStringLiteral("适合出门吗"),
        QStringLiteral("适合出门"),
        QStringLiteral("外面怎么样"),
        QStringLiteral("怎么样"),
        QStringLiteral("如何"),
        QStringLiteral("吗"),
        QStringLiteral("呢"),
        QStringLiteral("的"),
    });

    text = text.trimmed();
    if (!text.contains(QRegularExpression(QStringLiteral("[A-Za-z]"))))
    {
        text.remove(QChar(' '));
    }
    return text;
}

int requestedRangeDays(const QString &text)
{
    QString normalized = simplifiedDigits(text);
    const QRegularExpression rangeExpression(QStringLiteral("未来\\s*([123])\\s*天"));
    const QRegularExpressionMatch match = rangeExpression.match(normalized);
    if (match.hasMatch())
    {
        return match.captured(1).toInt();
    }
    return 0;
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
        || query.originalText.contains(QStringLiteral("AQI"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("天气 v1 暂不支持空气质量查询，目前只能查询基础天气。");
        return query;
    }

    if (query.originalText.contains(QStringLiteral("预警"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("天气 v1 暂不支持天气预警查询，目前只能查询基础天气。");
        return query;
    }

    if (query.originalText.contains(QStringLiteral("穿衣指数"), Qt::CaseInsensitive)
        || query.originalText.contains(QStringLiteral("穿什么"), Qt::CaseInsensitive))
    {
        query.unsupportedReason = QStringLiteral("天气 v1 暂不支持穿衣指数查询，目前只能查询基础天气。");
        return query;
    }

    const int rangeDays = requestedRangeDays(query.originalText);
    if (rangeDays > 0)
    {
        query.kind = WeatherQueryKind::Range;
        query.forecastDays = rangeDays;
        query.dateOffset = 0;
    }
    else if (query.originalText.contains(QStringLiteral("后天"), Qt::CaseInsensitive))
    {
        query.kind = WeatherQueryKind::Daily;
        query.dateOffset = 2;
        query.forecastDays = 1;
    }
    else if (query.originalText.contains(QStringLiteral("明天"), Qt::CaseInsensitive))
    {
        query.kind = WeatherQueryKind::Daily;
        query.dateOffset = 1;
        query.forecastDays = 1;
    }
    else if (query.originalText.contains(QStringLiteral("今天"), Qt::CaseInsensitive))
    {
        query.kind = WeatherQueryKind::Daily;
        query.dateOffset = 0;
        query.forecastDays = 1;
    }

    query.cityName = extractedCityName(query.originalText);
    query.forecastDays = qBound(1, query.forecastDays, 3);
    return query;
}
