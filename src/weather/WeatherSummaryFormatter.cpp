#include "WeatherSummaryFormatter.h"

#include <QStringList>
#include <QtGlobal>

namespace
{
QString locationDisplayName(const WeatherLocation &location)
{
    QStringList parts;
    if (!location.cityName.trimmed().isEmpty())
    {
        parts.append(location.cityName.trimmed());
    }
    if (!location.adminName.trimmed().isEmpty() && location.adminName.trimmed() != location.cityName.trimmed())
    {
        parts.append(location.adminName.trimmed());
    }
    if (!location.countryName.trimmed().isEmpty() && location.countryName.trimmed() != location.adminName.trimmed())
    {
        parts.append(location.countryName.trimmed());
    }
    return parts.isEmpty() ? QStringLiteral("当前位置") : parts.join(QStringLiteral("，"));
}

QString candidateDisplayName(const WeatherLocationCandidate &candidate)
{
    QStringList parts;
    if (!candidate.cityName.trimmed().isEmpty())
    {
        parts.append(candidate.cityName.trimmed());
    }
    if (!candidate.adminName.trimmed().isEmpty() && candidate.adminName.trimmed() != candidate.cityName.trimmed())
    {
        parts.append(candidate.adminName.trimmed());
    }
    if (!candidate.countryName.trimmed().isEmpty() && candidate.countryName.trimmed() != candidate.adminName.trimmed())
    {
        parts.append(candidate.countryName.trimmed());
    }
    return parts.isEmpty() ? QStringLiteral("未知地点") : parts.join(QStringLiteral("，"));
}

QString sourcePrefix(const WeatherLocation &location)
{
    switch (location.source)
    {
    case WeatherLocationSource::SettingsDefault:
        return QStringLiteral("使用设置页默认城市：%1。\n").arg(locationDisplayName(location));
    case WeatherLocationSource::IpFallback:
        return QStringLiteral("根据公网 IP 判断城市为：%1。\n").arg(locationDisplayName(location));
    case WeatherLocationSource::ExplicitCity:
        return {};
    }

    return {};
}

QString ambiguityPrefix(const WeatherReport &report)
{
    if (!report.hasLocationAmbiguity || report.locationCandidates.size() <= 1)
    {
        return {};
    }

    QStringList otherCandidates;
    const qsizetype candidateLimit = qMin<qsizetype>(4, report.locationCandidates.size());
    for (qsizetype index = 1; index < candidateLimit; ++index)
    {
        otherCandidates.append(candidateDisplayName(report.locationCandidates.at(index)));
    }

    if (otherCandidates.isEmpty())
    {
        return QStringLiteral("可能存在同名城市，当前使用：%1。\n").arg(locationDisplayName(report.location));
    }

    return QStringLiteral("可能存在同名城市，当前使用：%1。其他候选包括：%2。\n")
        .arg(locationDisplayName(report.location))
        .arg(otherCandidates.join(QStringLiteral("；")));
}

QString temperatureText(double value)
{
    return QStringLiteral("%1℃").arg(QString::number(value, 'f', 1));
}

QString currentText(const WeatherReport &report)
{
    const WeatherCurrent &current = report.current;
    QStringList details;
    if (current.hasTemperature)
    {
        details.append(QStringLiteral("温度 %1").arg(temperatureText(current.temperatureC)));
    }
    if (current.hasApparentTemperature)
    {
        details.append(QStringLiteral("体感 %1").arg(temperatureText(current.apparentTemperatureC)));
    }
    if (current.hasHumidity)
    {
        details.append(QStringLiteral("湿度 %1%").arg(QString::number(current.humidityPercent, 'f', 0)));
    }
    if (current.hasWindSpeed)
    {
        const QString direction = current.hasWindDirection
            ? WeatherSummaryFormatter::windDirectionText(current.windDirectionDegree)
            : QStringLiteral("风");
        details.append(QStringLiteral("%1 %2 km/h").arg(direction, QString::number(current.windSpeedKmh, 'f', 1)));
    }
    if (current.hasPrecipitation)
    {
        details.append(QStringLiteral("降水 %1 mm").arg(QString::number(current.precipitationMm, 'f', 1)));
    }

    QString text = QStringLiteral("%1当前天气：%2")
        .arg(locationDisplayName(report.location), WeatherSummaryFormatter::weatherCodeText(current.weatherCode));
    if (!details.isEmpty())
    {
        text += QStringLiteral("，") + details.join(QStringLiteral("，"));
    }
    if (current.time.isValid())
    {
        text += QStringLiteral("。更新时间：%1").arg(current.time.toString(QStringLiteral("HH:mm")));
    }
    text += QStringLiteral("。");
    return text;
}

QString dailyText(const WeatherDailyForecast &daily, const QString &prefix)
{
    QStringList details;
    if (daily.hasTemperatureMax && daily.hasTemperatureMin)
    {
        details.append(QStringLiteral("%1-%2").arg(temperatureText(daily.temperatureMinC), temperatureText(daily.temperatureMaxC)));
    }
    else if (daily.hasTemperatureMax)
    {
        details.append(QStringLiteral("最高 %1").arg(temperatureText(daily.temperatureMaxC)));
    }
    else if (daily.hasTemperatureMin)
    {
        details.append(QStringLiteral("最低 %1").arg(temperatureText(daily.temperatureMinC)));
    }
    if (daily.hasPrecipitationProbability)
    {
        details.append(QStringLiteral("降水概率 %1%").arg(QString::number(daily.precipitationProbabilityPercent, 'f', 0)));
    }

    QString text = QStringLiteral("%1：%2").arg(prefix, WeatherSummaryFormatter::weatherCodeText(daily.weatherCode));
    if (!details.isEmpty())
    {
        text += QStringLiteral("，") + details.join(QStringLiteral("，"));
    }
    return text;
}

const WeatherDailyForecast *forecastForOffset(const QVector<WeatherDailyForecast> &forecasts, int offset)
{
    if (offset < 0 || offset >= forecasts.size())
    {
        return nullptr;
    }
    return &forecasts[offset];
}
}

QString WeatherSummaryFormatter::format(const WeatherReport &report) const
{
    QString message = sourcePrefix(report.location) + ambiguityPrefix(report);
    if (report.query.kind == WeatherQueryKind::Current)
    {
        if (!report.current.valid)
        {
            return message + QStringLiteral("天气数据缺少当前天气，暂时无法生成结果。");
        }
        return message + currentText(report);
    }

    if (report.query.kind == WeatherQueryKind::Daily)
    {
        const WeatherDailyForecast *daily = forecastForOffset(report.dailyForecasts, report.query.dateOffset);
        if (daily == nullptr)
        {
            return message + QStringLiteral("天气数据缺少目标日期预报，暂时无法生成结果。");
        }
        const QString dayName = report.query.dateOffset == 0
            ? QStringLiteral("今天")
            : (report.query.dateOffset == 1 ? QStringLiteral("明天") : QStringLiteral("后天"));
        return message + QStringLiteral("%1%2天气：%3。")
            .arg(locationDisplayName(report.location), dayName, dailyText(*daily, daily->date.toString(QStringLiteral("MM-dd"))).section(QStringLiteral("："), 1));
    }

    QStringList lines;
    const int count = qMin(report.query.forecastDays, report.dailyForecasts.size());
    for (int index = 0; index < count; ++index)
    {
        const WeatherDailyForecast &daily = report.dailyForecasts.at(index);
        lines.append(dailyText(daily, daily.date.toString(QStringLiteral("MM-dd"))));
    }

    if (lines.isEmpty())
    {
        return message + QStringLiteral("天气数据缺少未来预报，暂时无法生成结果。");
    }

    return message
        + QStringLiteral("%1未来 %2 天天气：\n%3。")
            .arg(locationDisplayName(report.location), QString::number(count), lines.join(QStringLiteral("\n")));
}

QString WeatherSummaryFormatter::weatherCodeText(int code)
{
    switch (code)
    {
    case 0:
        return QStringLiteral("晴");
    case 1:
        return QStringLiteral("大部晴朗");
    case 2:
        return QStringLiteral("局部多云");
    case 3:
        return QStringLiteral("阴");
    case 45:
    case 48:
        return QStringLiteral("雾");
    case 51:
    case 53:
    case 55:
        return QStringLiteral("毛毛雨");
    case 56:
    case 57:
        return QStringLiteral("冻毛毛雨");
    case 61:
        return QStringLiteral("小雨");
    case 63:
        return QStringLiteral("中雨");
    case 65:
        return QStringLiteral("大雨");
    case 66:
    case 67:
        return QStringLiteral("冻雨");
    case 71:
        return QStringLiteral("小雪");
    case 73:
        return QStringLiteral("中雪");
    case 75:
        return QStringLiteral("大雪");
    case 77:
        return QStringLiteral("雪粒");
    case 80:
        return QStringLiteral("小阵雨");
    case 81:
        return QStringLiteral("中阵雨");
    case 82:
        return QStringLiteral("强阵雨");
    case 85:
    case 86:
        return QStringLiteral("阵雪");
    case 95:
        return QStringLiteral("雷暴");
    case 96:
    case 99:
        return QStringLiteral("雷暴伴冰雹");
    default:
        return QStringLiteral("未知天气");
    }
}

QString WeatherSummaryFormatter::windDirectionText(double degree)
{
    const QStringList directions = {
        QStringLiteral("北风"),
        QStringLiteral("东北风"),
        QStringLiteral("东风"),
        QStringLiteral("东南风"),
        QStringLiteral("南风"),
        QStringLiteral("西南风"),
        QStringLiteral("西风"),
        QStringLiteral("西北风"),
    };
    const int index = static_cast<int>(qRound(degree / 45.0)) % directions.size();
    return directions.at(index);
}
