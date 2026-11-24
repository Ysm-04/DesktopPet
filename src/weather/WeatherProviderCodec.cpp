#include "WeatherProviderCodec.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int GeocodingCandidateCount = 5;
constexpr int OpenMeteoForecastDays = 4;
constexpr int MetNorwayMaxDays = 4;

bool parseJsonDouble(const QJsonObject &object, const QString &key, double *value)
{
    const QJsonValue jsonValue = object.value(key);
    double parsed = 0.0;
    if (jsonValue.isDouble())
    {
        parsed = jsonValue.toDouble();
    }
    else if (jsonValue.isString())
    {
        bool ok = false;
        parsed = jsonValue.toString().trimmed().toDouble(&ok);
        if (!ok)
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    if (!std::isfinite(parsed))
    {
        return false;
    }

    if (value != nullptr)
    {
        *value = parsed;
    }
    return true;
}

bool parseJsonInt(const QJsonObject &object, const QString &key, int *value)
{
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isDouble())
    {
        if (value != nullptr)
        {
            *value = jsonValue.toInt();
        }
        return true;
    }

    if (jsonValue.isString())
    {
        bool ok = false;
        const int parsed = jsonValue.toString().trimmed().toInt(&ok);
        if (!ok)
        {
            return false;
        }
        if (value != nullptr)
        {
            *value = parsed;
        }
        return true;
    }

    return false;
}

QJsonArray arrayValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray();
}

QString firstNonEmpty(const QStringList &values)
{
    for (const QString &value : values)
    {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty())
        {
            return trimmed;
        }
    }
    return {};
}

QString firstValueArrayString(const QJsonObject &object, const QString &key)
{
    const QJsonArray array = arrayValue(object, key);
    if (array.isEmpty() || !array.first().isObject())
    {
        return {};
    }
    return array.first().toObject().value(QStringLiteral("value")).toString().trimmed();
}

bool containsCjk(const QString &text)
{
    for (const QChar ch : text)
    {
        const ushort code = ch.unicode();
        if (code >= 0x4E00 && code <= 0x9FFF)
        {
            return true;
        }
    }
    return false;
}

bool isAsciiOnly(const QString &text)
{
    for (const QChar ch : text)
    {
        if (ch.unicode() > 127)
        {
            return false;
        }
    }
    return true;
}

QString primaryLocalizedName(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
    {
        return {};
    }

    const int semicolonIndex = text.indexOf(QLatin1Char(';'));
    if (semicolonIndex >= 0)
    {
        const QString firstPart = text.left(semicolonIndex).trimmed();
        if (!firstPart.isEmpty())
        {
            text = firstPart;
        }
    }

    const int slashIndex = text.indexOf(QLatin1Char('/'));
    if (slashIndex > 0)
    {
        const QString leftPart = text.left(slashIndex).trimmed();
        const QString rightPart = text.mid(slashIndex + 1).trimmed();
        if (!leftPart.isEmpty()
            && leftPart.size() == rightPart.size()
            && containsCjk(leftPart)
            && containsCjk(rightPart))
        {
            text = leftPart;
        }
    }

    return text;
}

QString foldDiacriticsToAscii(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar ch : decomposed)
    {
        if (ch.category() != QChar::Mark_NonSpacing && ch.category() != QChar::Mark_SpacingCombining)
        {
            folded.append(ch);
        }
    }
    return folded;
}

QString normalizeNameForMatch(QString name)
{
    name = primaryLocalizedName(name);
    name = foldDiacriticsToAscii(name).toLower();
    name.remove(QRegularExpression(QStringLiteral("[\\s,，、.．·'’\"-]+")));
    if (containsCjk(name) && name.size() > 2)
    {
        const QChar last = name.at(name.size() - 1);
        if (last == QLatin1Char(0x5E02)
            || last == QLatin1Char(0x53BF)
            || last == QLatin1Char(0x533A)
            || last == QLatin1Char(0x7701))
        {
            name.chop(1);
        }
    }
    return name;
}

bool matchesSingleNormalizedName(const QString &queryNorm, const QString &candidateNorm)
{
    if (queryNorm.isEmpty() || candidateNorm.isEmpty())
    {
        return false;
    }
    if (queryNorm == candidateNorm)
    {
        return true;
    }
    if (queryNorm.size() < 2 || candidateNorm.size() < 2)
    {
        return false;
    }
    return candidateNorm.contains(queryNorm) || queryNorm.contains(candidateNorm);
}

QDateTime dateTimeFromOpenMeteo(const QString &text)
{
    QDateTime value = QDateTime::fromString(text, Qt::ISODate);
    if (value.isValid())
    {
        return value;
    }

    value = QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm"));
    return value.isValid() ? value : QDateTime();
}

QDateTime dateTimeFromWttr(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QLocale english(QLocale::English, QLocale::UnitedStates);
    static const QStringList formats = {
        QStringLiteral("yyyy-MM-dd hh:mm AP"),
        QStringLiteral("yyyy-MM-dd h:mm AP"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
    };
    for (const QString &format : formats)
    {
        const QDateTime parsed = english.toDateTime(trimmed, format);
        if (parsed.isValid())
        {
            return parsed;
        }
    }

    return QDateTime::fromString(trimmed, Qt::ISODate);
}

QDateTime localDateTimeFromUtc(const QDateTime &utcTime, const WeatherLocation &location)
{
    if (!utcTime.isValid())
    {
        return {};
    }

    if (!location.timezone.trimmed().isEmpty())
    {
        const QTimeZone timeZone(location.timezone.trimmed().toUtf8());
        if (timeZone.isValid())
        {
            return utcTime.toTimeZone(timeZone);
        }
    }

    if (location.countryName.contains(QStringLiteral("中国"))
        || location.countryName.contains(QStringLiteral("China"), Qt::CaseInsensitive))
    {
        return utcTime.toOffsetFromUtc(8 * 3600);
    }

    if (qAbs(location.longitude) > 0.001 || qAbs(location.latitude) > 0.001)
    {
        const int offsetSeconds = qRound(location.longitude / 15.0) * 3600;
        return utcTime.toOffsetFromUtc(offsetSeconds);
    }

    return utcTime.toLocalTime();
}

int mapWwoCodeToWmo(int code)
{
    switch (code)
    {
    case 113:
        return 0;
    case 116:
        return 2;
    case 119:
    case 122:
        return 3;
    case 143:
    case 248:
    case 260:
        return 45;
    case 176:
    case 263:
    case 266:
        return 51;
    case 185:
    case 281:
    case 284:
        return 56;
    case 293:
    case 296:
        return 61;
    case 299:
    case 302:
    case 356:
        return 63;
    case 305:
    case 308:
    case 359:
        return 65;
    case 182:
    case 311:
    case 314:
    case 317:
    case 320:
    case 362:
    case 365:
        return 66;
    case 179:
    case 323:
    case 326:
        return 71;
    case 227:
    case 329:
    case 332:
        return 73;
    case 230:
    case 335:
    case 338:
        return 75;
    case 350:
    case 374:
    case 377:
        return 77;
    case 353:
        return 80;
    case 368:
    case 371:
        return 85;
    case 200:
    case 386:
    case 389:
        return 95;
    case 392:
    case 395:
        return 96;
    default:
        return -1;
    }
}

int mapMetSymbolToWmo(QString symbolCode)
{
    symbolCode = symbolCode.trimmed().toLower();
    if (symbolCode.isEmpty())
    {
        return -1;
    }

    for (const QString &suffix : {QStringLiteral("_day"), QStringLiteral("_night"), QStringLiteral("_polartwilight")})
    {
        if (symbolCode.endsWith(suffix))
        {
            symbolCode.chop(suffix.size());
            break;
        }
    }

    if (symbolCode.contains(QStringLiteral("thunder")))
    {
        return 95;
    }
    if (symbolCode == QStringLiteral("clearsky"))
    {
        return 0;
    }
    if (symbolCode == QStringLiteral("fair"))
    {
        return 1;
    }
    if (symbolCode == QStringLiteral("partlycloudy"))
    {
        return 2;
    }
    if (symbolCode == QStringLiteral("cloudy"))
    {
        return 3;
    }
    if (symbolCode == QStringLiteral("fog"))
    {
        return 45;
    }
    if (symbolCode == QStringLiteral("lightrainshowers"))
    {
        return 80;
    }
    if (symbolCode == QStringLiteral("rainshowers"))
    {
        return 81;
    }
    if (symbolCode == QStringLiteral("heavyrainshowers"))
    {
        return 82;
    }
    if (symbolCode == QStringLiteral("lightrain"))
    {
        return 61;
    }
    if (symbolCode == QStringLiteral("rain"))
    {
        return 63;
    }
    if (symbolCode == QStringLiteral("heavyrain"))
    {
        return 65;
    }
    if (symbolCode.contains(QStringLiteral("sleet")))
    {
        return 66;
    }
    if (symbolCode == QStringLiteral("lightsnow"))
    {
        return 71;
    }
    if (symbolCode == QStringLiteral("snow"))
    {
        return 73;
    }
    if (symbolCode == QStringLiteral("heavysnow"))
    {
        return 75;
    }
    if (symbolCode.contains(QStringLiteral("snowshowers")))
    {
        return 85;
    }
    return -1;
}

QString featureCodeFromOsmType(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized == QStringLiteral("country") || normalized == QStringLiteral("capital"))
    {
        return QStringLiteral("PPLC");
    }
    if (normalized == QStringLiteral("state") || normalized == QStringLiteral("province"))
    {
        return QStringLiteral("PPLA");
    }
    if (normalized == QStringLiteral("city")
        || normalized == QStringLiteral("region")
        || normalized == QStringLiteral("prefecture")
        || normalized == QStringLiteral("administrative"))
    {
        return QStringLiteral("PPLA2");
    }
    if (normalized == QStringLiteral("county")
        || normalized == QStringLiteral("district")
        || normalized == QStringLiteral("town")
        || normalized == QStringLiteral("municipality"))
    {
        return QStringLiteral("PPLA3");
    }
    return QStringLiteral("PPL");
}

int osmPlacePriority(const QString &keyOrCategory, const QString &type)
{
    const QString category = keyOrCategory.trimmed().toLower();
    const QString normalizedType = type.trimmed().toLower();
    if (category == QStringLiteral("place")
        && (normalizedType == QStringLiteral("city")
            || normalizedType == QStringLiteral("town")
            || normalizedType == QStringLiteral("county")
            || normalizedType == QStringLiteral("state")
            || normalizedType == QStringLiteral("municipality")))
    {
        return 4;
    }
    if (category == QStringLiteral("boundary") && normalizedType == QStringLiteral("administrative"))
    {
        return 3;
    }
    if (category == QStringLiteral("place"))
    {
        return 2;
    }
    if (category == QStringLiteral("boundary"))
    {
        return 1;
    }
    return 0;
}

bool parseOpenMeteoGeocoding(const QByteArray &payload, QVector<WeatherLocationCandidate> *candidates)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    const QJsonArray results = document.object().value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject city = value.toObject();
        WeatherLocationCandidate candidate;
        if (!parseJsonDouble(city, QStringLiteral("latitude"), &candidate.latitude)
            || !parseJsonDouble(city, QStringLiteral("longitude"), &candidate.longitude))
        {
            continue;
        }

        candidate.cityName = city.value(QStringLiteral("name")).toString().trimmed();
        candidate.adminName = city.value(QStringLiteral("admin1")).toString().trimmed();
        candidate.countryName = city.value(QStringLiteral("country")).toString().trimmed();
        candidate.timezone = city.value(QStringLiteral("timezone")).toString().trimmed();
        candidate.countryCode = city.value(QStringLiteral("country_code")).toString().trimmed().toUpper();
        candidate.featureCode = city.value(QStringLiteral("feature_code")).toString().trimmed().toUpper();
        double population = 0.0;
        if (parseJsonDouble(city, QStringLiteral("population"), &population))
        {
            candidate.population = qRound64(population);
        }
        candidates->append(candidate);
    }

    return true;
}

bool parseNominatimGeocoding(const QByteArray &payload, QVector<WeatherLocationCandidate> *candidates)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        return false;
    }

    struct RankedCandidate
    {
        int priority = 0;
        WeatherLocationCandidate candidate;
    };
    QVector<RankedCandidate> ranked;

    const QJsonArray results = document.array();
    for (const QJsonValue &value : results)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject item = value.toObject();
        WeatherLocationCandidate candidate;
        if (!parseJsonDouble(item, QStringLiteral("lat"), &candidate.latitude)
            || !parseJsonDouble(item, QStringLiteral("lon"), &candidate.longitude))
        {
            continue;
        }

        const QString placeType = firstNonEmpty({
            item.value(QStringLiteral("addresstype")).toString(),
            item.value(QStringLiteral("type")).toString(),
        });
        if (placeType.compare(QStringLiteral("continent"), Qt::CaseInsensitive) == 0)
        {
            continue;
        }

        const QJsonObject address = item.value(QStringLiteral("address")).toObject();
        candidate.cityName = primaryLocalizedName(firstNonEmpty({
            item.value(QStringLiteral("name")).toString(),
            address.value(QStringLiteral("city")).toString(),
            address.value(QStringLiteral("town")).toString(),
            address.value(QStringLiteral("county")).toString(),
            address.value(QStringLiteral("municipality")).toString(),
            item.value(QStringLiteral("display_name")).toString().section(QLatin1Char(','), 0, 0),
        }));
        if (candidate.cityName.isEmpty())
        {
            continue;
        }

        candidate.adminName = primaryLocalizedName(firstNonEmpty({
            address.value(QStringLiteral("state")).toString(),
            address.value(QStringLiteral("region")).toString(),
            address.value(QStringLiteral("province")).toString(),
        }));
        candidate.countryName = primaryLocalizedName(address.value(QStringLiteral("country")).toString());
        candidate.countryCode = address.value(QStringLiteral("country_code")).toString().trimmed().toUpper();
        candidate.featureCode = featureCodeFromOsmType(placeType);

        ranked.append({
            osmPlacePriority(item.value(QStringLiteral("category")).toString(), placeType),
            candidate,
        });
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const RankedCandidate &left, const RankedCandidate &right) {
        return left.priority > right.priority;
    });
    for (const RankedCandidate &entry : ranked)
    {
        candidates->append(entry.candidate);
    }
    return true;
}

bool parsePhotonGeocoding(const QByteArray &payload, QVector<WeatherLocationCandidate> *candidates)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    struct RankedCandidate
    {
        int priority = 0;
        WeatherLocationCandidate candidate;
    };
    QVector<RankedCandidate> ranked;

    const QJsonArray features = arrayValue(document.object(), QStringLiteral("features"));
    for (const QJsonValue &value : features)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject feature = value.toObject();
        const QJsonArray coordinates = arrayValue(feature.value(QStringLiteral("geometry")).toObject(), QStringLiteral("coordinates"));
        if (coordinates.size() < 2 || !coordinates.at(0).isDouble() || !coordinates.at(1).isDouble())
        {
            continue;
        }

        const double longitude = coordinates.at(0).toDouble();
        const double latitude = coordinates.at(1).toDouble();
        if (!std::isfinite(latitude) || !std::isfinite(longitude))
        {
            continue;
        }

        const QJsonObject props = feature.value(QStringLiteral("properties")).toObject();
        WeatherLocationCandidate candidate;
        candidate.latitude = latitude;
        candidate.longitude = longitude;
        candidate.cityName = firstNonEmpty({
            props.value(QStringLiteral("name")).toString(),
            props.value(QStringLiteral("city")).toString(),
            props.value(QStringLiteral("county")).toString(),
            props.value(QStringLiteral("state")).toString(),
        });
        if (candidate.cityName.isEmpty())
        {
            continue;
        }

        candidate.adminName = props.value(QStringLiteral("state")).toString().trimmed();
        candidate.countryName = props.value(QStringLiteral("country")).toString().trimmed();
        candidate.countryCode = props.value(QStringLiteral("countrycode")).toString().trimmed().toUpper();
        const QString osmValue = firstNonEmpty({
            props.value(QStringLiteral("type")).toString(),
            props.value(QStringLiteral("osm_value")).toString(),
        });
        candidate.featureCode = featureCodeFromOsmType(osmValue);

        ranked.append({
            osmPlacePriority(props.value(QStringLiteral("osm_key")).toString(), osmValue),
            candidate,
        });
    }

    std::stable_sort(ranked.begin(), ranked.end(), [](const RankedCandidate &left, const RankedCandidate &right) {
        return left.priority > right.priority;
    });
    for (const RankedCandidate &entry : ranked)
    {
        candidates->append(entry.candidate);
    }
    return true;
}

bool validateReportForQuery(const WeatherReport &report, const WeatherQuery &query, QString *errorMessage)
{
    if (query.kind == WeatherQueryKind::Current)
    {
        if (!report.current.valid)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("天气数据缺少当前天气。");
            }
            return false;
        }
        return true;
    }

    if (query.kind == WeatherQueryKind::Daily)
    {
        if (query.dateOffset < 0 || query.dateOffset >= report.dailyForecasts.size())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("天气数据缺少目标日期预报。");
            }
            return false;
        }
        return true;
    }

    const int totalDays = static_cast<int>(report.dailyForecasts.size());
    const int startIndex = qBound(0, query.dateOffset, totalDays);
    if (totalDays - startIndex <= 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("天气数据缺少未来预报。");
        }
        return false;
    }
    return true;
}

bool parseOpenMeteoForecast(
    const QByteArray &payload,
    const WeatherQuery &query,
    const WeatherLocation &location,
    WeatherReport *report,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("天气数据格式无效。");
        }
        return false;
    }

    const QJsonObject root = document.object();
    WeatherReport parsed;
    parsed.query = query;
    parsed.location = location;
    parsed.dataSourceName = QStringLiteral("Open-Meteo");

    const QJsonObject currentObject = root.value(QStringLiteral("current")).toObject();
    if (!currentObject.isEmpty())
    {
        parsed.current.valid = true;
        parsed.current.time = dateTimeFromOpenMeteo(currentObject.value(QStringLiteral("time")).toString());
        parsed.current.hasTemperature = parseJsonDouble(currentObject, QStringLiteral("temperature_2m"), &parsed.current.temperatureC);
        parsed.current.hasApparentTemperature = parseJsonDouble(currentObject, QStringLiteral("apparent_temperature"), &parsed.current.apparentTemperatureC);
        parsed.current.hasHumidity = parseJsonDouble(currentObject, QStringLiteral("relative_humidity_2m"), &parsed.current.humidityPercent);
        parsed.current.hasWindSpeed = parseJsonDouble(currentObject, QStringLiteral("wind_speed_10m"), &parsed.current.windSpeedKmh);
        parsed.current.hasWindDirection = parseJsonDouble(currentObject, QStringLiteral("wind_direction_10m"), &parsed.current.windDirectionDegree);
        parsed.current.hasPrecipitation = parseJsonDouble(currentObject, QStringLiteral("precipitation"), &parsed.current.precipitationMm);
        parseJsonInt(currentObject, QStringLiteral("weather_code"), &parsed.current.weatherCode);
    }

    const QJsonObject dailyObject = root.value(QStringLiteral("daily")).toObject();
    const QJsonArray dates = arrayValue(dailyObject, QStringLiteral("time"));
    const QJsonArray codes = arrayValue(dailyObject, QStringLiteral("weather_code"));
    const QJsonArray maxTemperatures = arrayValue(dailyObject, QStringLiteral("temperature_2m_max"));
    const QJsonArray minTemperatures = arrayValue(dailyObject, QStringLiteral("temperature_2m_min"));
    const QJsonArray precipitationProbabilities = arrayValue(dailyObject, QStringLiteral("precipitation_probability_max"));
    for (int index = 0; index < dates.size(); ++index)
    {
        WeatherDailyForecast forecast;
        forecast.date = QDate::fromString(dates.at(index).toString(), Qt::ISODate);
        if (!forecast.date.isValid())
        {
            continue;
        }

        if (index < codes.size() && codes.at(index).isDouble())
        {
            forecast.weatherCode = codes.at(index).toInt();
        }
        if (index < maxTemperatures.size() && maxTemperatures.at(index).isDouble())
        {
            forecast.temperatureMaxC = maxTemperatures.at(index).toDouble();
            forecast.hasTemperatureMax = true;
        }
        if (index < minTemperatures.size() && minTemperatures.at(index).isDouble())
        {
            forecast.temperatureMinC = minTemperatures.at(index).toDouble();
            forecast.hasTemperatureMin = true;
        }
        if (index < precipitationProbabilities.size() && precipitationProbabilities.at(index).isDouble())
        {
            forecast.precipitationProbabilityPercent = precipitationProbabilities.at(index).toDouble();
            forecast.hasPrecipitationProbability = true;
        }
        parsed.dailyForecasts.append(forecast);
    }

    if (!validateReportForQuery(parsed, query, errorMessage))
    {
        return false;
    }

    *report = parsed;
    return true;
}

bool parseWttrForecast(
    const QByteArray &payload,
    const WeatherQuery &query,
    const WeatherLocation &location,
    WeatherReport *report,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("备用天气数据格式无效。");
        }
        return false;
    }

    const QJsonObject root = document.object();
    WeatherReport parsed;
    parsed.query = query;
    parsed.location = location;
    parsed.dataSourceName = QStringLiteral("wttr.in");

    if (parsed.location.cityName.trimmed().isEmpty())
    {
        const QJsonArray nearestArea = arrayValue(root, QStringLiteral("nearest_area"));
        if (!nearestArea.isEmpty() && nearestArea.first().isObject())
        {
            const QJsonObject area = nearestArea.first().toObject();
            parsed.location.cityName = firstValueArrayString(area, QStringLiteral("areaName"));
            parsed.location.adminName = firstValueArrayString(area, QStringLiteral("region"));
            parsed.location.countryName = firstValueArrayString(area, QStringLiteral("country"));
            parseJsonDouble(area, QStringLiteral("latitude"), &parsed.location.latitude);
            parseJsonDouble(area, QStringLiteral("longitude"), &parsed.location.longitude);
        }
    }

    const QJsonArray currentArray = arrayValue(root, QStringLiteral("current_condition"));
    if (!currentArray.isEmpty() && currentArray.first().isObject())
    {
        const QJsonObject currentObject = currentArray.first().toObject();
        parsed.current.valid = true;
        parsed.current.time = dateTimeFromWttr(currentObject.value(QStringLiteral("localObsDateTime")).toString());
        parsed.current.hasTemperature = parseJsonDouble(currentObject, QStringLiteral("temp_C"), &parsed.current.temperatureC);
        parsed.current.hasApparentTemperature = parseJsonDouble(currentObject, QStringLiteral("FeelsLikeC"), &parsed.current.apparentTemperatureC);
        parsed.current.hasHumidity = parseJsonDouble(currentObject, QStringLiteral("humidity"), &parsed.current.humidityPercent);
        parsed.current.hasWindSpeed = parseJsonDouble(currentObject, QStringLiteral("windspeedKmph"), &parsed.current.windSpeedKmh);
        parsed.current.hasWindDirection = parseJsonDouble(currentObject, QStringLiteral("winddirDegree"), &parsed.current.windDirectionDegree);
        parsed.current.hasPrecipitation = parseJsonDouble(currentObject, QStringLiteral("precipMM"), &parsed.current.precipitationMm);
        int wwoCode = -1;
        if (parseJsonInt(currentObject, QStringLiteral("weatherCode"), &wwoCode))
        {
            parsed.current.weatherCode = mapWwoCodeToWmo(wwoCode);
        }
    }

    const QJsonArray days = arrayValue(root, QStringLiteral("weather"));
    for (const QJsonValue &dayValue : days)
    {
        if (!dayValue.isObject())
        {
            continue;
        }

        const QJsonObject dayObject = dayValue.toObject();
        WeatherDailyForecast forecast;
        forecast.date = QDate::fromString(dayObject.value(QStringLiteral("date")).toString(), Qt::ISODate);
        if (!forecast.date.isValid())
        {
            continue;
        }

        forecast.hasTemperatureMax = parseJsonDouble(dayObject, QStringLiteral("maxtempC"), &forecast.temperatureMaxC);
        forecast.hasTemperatureMin = parseJsonDouble(dayObject, QStringLiteral("mintempC"), &forecast.temperatureMinC);

        const QJsonArray hourly = arrayValue(dayObject, QStringLiteral("hourly"));
        int chosenWwoCode = -1;
        bool hasChosenMiddayCode = false;
        double maxRainChance = 0.0;
        bool hasRainChance = false;
        for (int index = 0; index < hourly.size(); ++index)
        {
            if (!hourly.at(index).isObject())
            {
                continue;
            }
            const QJsonObject slot = hourly.at(index).toObject();
            double rainChance = 0.0;
            if (parseJsonDouble(slot, QStringLiteral("chanceofrain"), &rainChance))
            {
                maxRainChance = hasRainChance ? qMax(maxRainChance, rainChance) : rainChance;
                hasRainChance = true;
            }

            int slotWwoCode = -1;
            if (parseJsonInt(slot, QStringLiteral("weatherCode"), &slotWwoCode))
            {
                const QString slotTime = slot.value(QStringLiteral("time")).toString().trimmed();
                if (slotTime == QStringLiteral("1200")
                    || (!hasChosenMiddayCode && index == static_cast<int>(hourly.size()) / 2)
                    || chosenWwoCode < 0)
                {
                    chosenWwoCode = slotWwoCode;
                    if (slotTime == QStringLiteral("1200"))
                    {
                        hasChosenMiddayCode = true;
                    }
                }
            }
        }

        if (chosenWwoCode >= 0)
        {
            forecast.weatherCode = mapWwoCodeToWmo(chosenWwoCode);
        }
        if (hasRainChance)
        {
            forecast.precipitationProbabilityPercent = maxRainChance;
            forecast.hasPrecipitationProbability = true;
        }

        parsed.dailyForecasts.append(forecast);
    }

    if (!validateReportForQuery(parsed, query, errorMessage))
    {
        return false;
    }

    *report = parsed;
    return true;
}

QString extractMetSymbolCode(const QJsonObject &dataObject, double *precipitationMm, bool *hasPrecipitation)
{
    for (const QString &bucketKey : {
        QStringLiteral("next_1_hours"),
        QStringLiteral("next_6_hours"),
        QStringLiteral("next_12_hours"),
    })
    {
        const QJsonObject bucket = dataObject.value(bucketKey).toObject();
        if (bucket.isEmpty())
        {
            continue;
        }

        if (hasPrecipitation != nullptr && !(*hasPrecipitation))
        {
            const QJsonObject details = bucket.value(QStringLiteral("details")).toObject();
            if (parseJsonDouble(details, QStringLiteral("precipitation_amount"), precipitationMm))
            {
                *hasPrecipitation = true;
            }
        }

        const QString symbol = bucket.value(QStringLiteral("summary")).toObject()
            .value(QStringLiteral("symbol_code")).toString().trimmed();
        if (!symbol.isEmpty())
        {
            return symbol;
        }
    }

    return {};
}

bool parseMetNorwayForecast(
    const QByteArray &payload,
    const WeatherQuery &query,
    const WeatherLocation &location,
    WeatherReport *report,
    QString *errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("MET Norway 天气数据格式无效。");
        }
        return false;
    }

    const QJsonObject properties = document.object().value(QStringLiteral("properties")).toObject();
    const QJsonArray timeseries = arrayValue(properties, QStringLiteral("timeseries"));
    if (timeseries.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("MET Norway 天气序列为空。");
        }
        return false;
    }

    WeatherReport parsed;
    parsed.query = query;
    parsed.location = location;
    parsed.dataSourceName = QStringLiteral("MET Norway");

    struct DayAccumulator
    {
        QDate date;
        double minTemp = 0.0;
        double maxTemp = 0.0;
        bool hasTemp = false;
        int weatherCode = -1;
        bool hasMiddayCode = false;
    };
    QVector<DayAccumulator> dayBuckets;

    for (const QJsonValue &entryValue : timeseries)
    {
        if (!entryValue.isObject())
        {
            continue;
        }

        const QJsonObject entry = entryValue.toObject();
        const QDateTime utcTime = QDateTime::fromString(entry.value(QStringLiteral("time")).toString(), Qt::ISODate);
        if (!utcTime.isValid())
        {
            continue;
        }
        const QDateTime localTime = localDateTimeFromUtc(utcTime, location);
        const QJsonObject data = entry.value(QStringLiteral("data")).toObject();
        const QJsonObject instantDetails = data.value(QStringLiteral("instant")).toObject()
            .value(QStringLiteral("details")).toObject();

        double precipitationMm = 0.0;
        bool hasPrecipitation = false;
        const QString symbolCode = extractMetSymbolCode(data, &precipitationMm, &hasPrecipitation);
        const int wmoCode = mapMetSymbolToWmo(symbolCode);

        if (!parsed.current.valid)
        {
            parsed.current.valid = true;
            parsed.current.time = localTime;
            parsed.current.hasTemperature = parseJsonDouble(instantDetails, QStringLiteral("air_temperature"), &parsed.current.temperatureC);
            parsed.current.hasHumidity = parseJsonDouble(instantDetails, QStringLiteral("relative_humidity"), &parsed.current.humidityPercent);
            double windMs = 0.0;
            if (parseJsonDouble(instantDetails, QStringLiteral("wind_speed"), &windMs))
            {
                parsed.current.windSpeedKmh = windMs * 3.6;
                parsed.current.hasWindSpeed = true;
            }
            parsed.current.hasWindDirection = parseJsonDouble(instantDetails, QStringLiteral("wind_from_direction"), &parsed.current.windDirectionDegree);
            if (hasPrecipitation)
            {
                parsed.current.precipitationMm = precipitationMm;
                parsed.current.hasPrecipitation = true;
            }
            parsed.current.weatherCode = wmoCode;
        }

        const QDate localDate = localTime.date();
        if (!localDate.isValid())
        {
            continue;
        }

        int bucketIndex = -1;
        for (int index = 0; index < dayBuckets.size(); ++index)
        {
            if (dayBuckets.at(index).date == localDate)
            {
                bucketIndex = index;
                break;
            }
        }

        if (bucketIndex < 0)
        {
            if (dayBuckets.size() >= MetNorwayMaxDays)
            {
                continue;
            }
            DayAccumulator created;
            created.date = localDate;
            dayBuckets.append(created);
            bucketIndex = dayBuckets.size() - 1;
        }

        DayAccumulator &bucket = dayBuckets[bucketIndex];
        double airTemperature = 0.0;
        if (parseJsonDouble(instantDetails, QStringLiteral("air_temperature"), &airTemperature))
        {
            if (!bucket.hasTemp)
            {
                bucket.minTemp = airTemperature;
                bucket.maxTemp = airTemperature;
                bucket.hasTemp = true;
            }
            else
            {
                bucket.minTemp = qMin(bucket.minTemp, airTemperature);
                bucket.maxTemp = qMax(bucket.maxTemp, airTemperature);
            }
        }

        if (wmoCode >= 0)
        {
            const int hour = localTime.time().hour();
            const bool isMidday = hour >= 11 && hour <= 15;
            if (isMidday || (!bucket.hasMiddayCode && bucket.weatherCode < 0))
            {
                bucket.weatherCode = wmoCode;
                if (isMidday)
                {
                    bucket.hasMiddayCode = true;
                }
            }
        }
    }

    for (const DayAccumulator &bucket : dayBuckets)
    {
        WeatherDailyForecast daily;
        daily.date = bucket.date;
        daily.weatherCode = bucket.weatherCode;
        if (bucket.hasTemp)
        {
            daily.temperatureMinC = bucket.minTemp;
            daily.temperatureMaxC = bucket.maxTemp;
            daily.hasTemperatureMin = true;
            daily.hasTemperatureMax = true;
        }
        parsed.dailyForecasts.append(daily);
    }

    if (!validateReportForQuery(parsed, query, errorMessage))
    {
        return false;
    }

    *report = parsed;
    return true;
}
}

namespace WeatherProviderCodec
{
QString normalizedProviderSetting(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    if (normalized == QStringLiteral("wttr") || normalized == QStringLiteral("wttr.in"))
    {
        return QStringLiteral("wttr");
    }
    if (normalized == QStringLiteral("met-no")
        || normalized == QStringLiteral("met.no")
        || normalized == QStringLiteral("yr")
        || normalized == QStringLiteral("yr.no"))
    {
        return QStringLiteral("met-no");
    }
    return QStringLiteral("open-meteo");
}

QString geocodingProviderName(WeatherGeocodingProvider provider)
{
    switch (provider)
    {
    case WeatherGeocodingProvider::OpenMeteo:
        return QStringLiteral("Open-Meteo");
    case WeatherGeocodingProvider::Nominatim:
        return QStringLiteral("Nominatim");
    case WeatherGeocodingProvider::Photon:
        return QStringLiteral("Photon");
    }

    return QStringLiteral("Open-Meteo");
}

QString forecastProviderName(WeatherForecastProvider provider)
{
    switch (provider)
    {
    case WeatherForecastProvider::OpenMeteo:
        return QStringLiteral("Open-Meteo");
    case WeatherForecastProvider::Wttr:
        return QStringLiteral("wttr.in");
    case WeatherForecastProvider::MetNorway:
        return QStringLiteral("MET Norway");
    }

    return QStringLiteral("Open-Meteo");
}

QVector<WeatherGeocodingProvider> geocodingFallbackOrder()
{
    return {
        WeatherGeocodingProvider::OpenMeteo,
        WeatherGeocodingProvider::Nominatim,
        WeatherGeocodingProvider::Photon,
    };
}

QVector<WeatherForecastProvider> forecastFallbackOrder(
    const QString &preferredProvider,
    const WeatherQuery &query)
{
    const QString normalized = normalizedProviderSetting(preferredProvider);
    const bool needsFourthDay = query.kind == WeatherQueryKind::Daily && query.dateOffset >= 3;

    if (normalized == QStringLiteral("wttr"))
    {
        if (needsFourthDay)
        {
            return {
                WeatherForecastProvider::OpenMeteo,
                WeatherForecastProvider::MetNorway,
            };
        }
        return {
            WeatherForecastProvider::Wttr,
            WeatherForecastProvider::OpenMeteo,
            WeatherForecastProvider::MetNorway,
        };
    }

    if (normalized == QStringLiteral("met-no"))
    {
        if (needsFourthDay)
        {
            return {
                WeatherForecastProvider::MetNorway,
                WeatherForecastProvider::OpenMeteo,
            };
        }
        return {
            WeatherForecastProvider::MetNorway,
            WeatherForecastProvider::OpenMeteo,
            WeatherForecastProvider::Wttr,
        };
    }

    if (needsFourthDay)
    {
        return {
            WeatherForecastProvider::OpenMeteo,
            WeatherForecastProvider::MetNorway,
        };
    }

    return {
        WeatherForecastProvider::OpenMeteo,
        WeatherForecastProvider::Wttr,
        WeatherForecastProvider::MetNorway,
    };
}

QVector<WeatherForecastProvider> applyForecastProviderDemotions(
    const QVector<WeatherForecastProvider> &order,
    const QVector<WeatherForecastProvider> &demotedProviders)
{
    if (order.isEmpty() || demotedProviders.isEmpty())
    {
        return order;
    }

    QVector<WeatherForecastProvider> sorted = order;
    std::stable_sort(sorted.begin(), sorted.end(), [&demotedProviders](
        WeatherForecastProvider left,
        WeatherForecastProvider right) {
        const bool leftDemoted = demotedProviders.contains(left);
        const bool rightDemoted = demotedProviders.contains(right);
        return !leftDemoted && rightDemoted;
    });
    return sorted;
}

bool canUseDirectWttrQuery(const WeatherQuery &query)
{
    return !(query.kind == WeatherQueryKind::Daily && query.dateOffset >= 3);
}

QUrl buildGeocodingUrl(
    WeatherGeocodingProvider provider,
    const WeatherLocationSearch &search,
    const QString &language)
{
    const QString cleanName = search.queryName.trimmed();
    const QString cleanCountryCode = search.countryCode.trimmed();
    const QString cleanLanguage = language.trimmed().isEmpty() ? QStringLiteral("zh") : language.trimmed().toLower();

    if (provider == WeatherGeocodingProvider::OpenMeteo)
    {
        QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("name"), cleanName);
        query.addQueryItem(QStringLiteral("count"), QString::number(GeocodingCandidateCount));
        query.addQueryItem(QStringLiteral("language"), cleanLanguage);
        query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        if (!cleanCountryCode.isEmpty())
        {
            query.addQueryItem(QStringLiteral("countryCode"), cleanCountryCode.toUpper());
        }
        url.setQuery(query);
        return url;
    }

    if (provider == WeatherGeocodingProvider::Nominatim)
    {
        QUrl url(QStringLiteral("https://nominatim.openstreetmap.org/search"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), cleanName);
        query.addQueryItem(QStringLiteral("format"), QStringLiteral("jsonv2"));
        query.addQueryItem(QStringLiteral("addressdetails"), QStringLiteral("1"));
        query.addQueryItem(QStringLiteral("limit"), QString::number(GeocodingCandidateCount));
        query.addQueryItem(QStringLiteral("accept-language"), cleanLanguage);
        if (!cleanCountryCode.isEmpty())
        {
            query.addQueryItem(QStringLiteral("countrycodes"), cleanCountryCode.toLower());
        }
        url.setQuery(query);
        return url;
    }

    QUrl url(QStringLiteral("https://photon.komoot.io/api/"));
    QUrlQuery query;
    const QString photonText = cleanCountryCode.isEmpty()
        ? cleanName
        : (cleanName + QLatin1Char(' ') + cleanCountryCode.toUpper());
    query.addQueryItem(QStringLiteral("q"), photonText);
    query.addQueryItem(QStringLiteral("limit"), QString::number(GeocodingCandidateCount));
    url.setQuery(query);
    return url;
}

QUrl buildForecastUrl(
    WeatherForecastProvider provider,
    const WeatherLocation &location)
{
    if (provider == WeatherForecastProvider::OpenMeteo)
    {
        QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("latitude"), QString::number(location.latitude, 'f', 6));
        query.addQueryItem(QStringLiteral("longitude"), QString::number(location.longitude, 'f', 6));
        query.addQueryItem(QStringLiteral("current"), QStringLiteral("temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,precipitation"));
        query.addQueryItem(QStringLiteral("daily"), QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"));
        query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
        query.addQueryItem(QStringLiteral("forecast_days"), QString::number(OpenMeteoForecastDays));
        url.setQuery(query);
        return url;
    }

    if (provider == WeatherForecastProvider::Wttr)
    {
        QUrl url;
        url.setScheme(QStringLiteral("https"));
        url.setHost(QStringLiteral("wttr.in"));
        url.setPath(QStringLiteral("/%1,%2")
            .arg(QString::number(location.latitude, 'f', 4), QString::number(location.longitude, 'f', 4)));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("format"), QStringLiteral("j1"));
        query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
        url.setQuery(query);
        return url;
    }

    QUrl url(QStringLiteral("https://api.met.no/weatherapi/locationforecast/2.0/compact"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("lat"), QString::number(location.latitude, 'f', 4));
    query.addQueryItem(QStringLiteral("lon"), QString::number(location.longitude, 'f', 4));
    url.setQuery(query);
    return url;
}

QUrl buildDirectWttrUrl(const WeatherLocationSearch &search)
{
    const QString cleanName = search.queryName.trimmed();
    const QString cleanCountryCode = search.countryCode.trimmed().toUpper();
    const QString locationQuery = cleanCountryCode.isEmpty()
        ? cleanName
        : (cleanName + QLatin1Char(',') + cleanCountryCode);

    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QStringLiteral("wttr.in"));
    url.setPath(QLatin1Char('/') + locationQuery);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("j1"));
    query.addQueryItem(QStringLiteral("lang"), QStringLiteral("zh"));
    url.setQuery(query);
    return url;
}

bool parseGeocodingResponse(
    WeatherGeocodingProvider provider,
    const QByteArray &payload,
    QVector<WeatherLocationCandidate> *candidates)
{
    if (candidates == nullptr)
    {
        return false;
    }
    candidates->clear();

    switch (provider)
    {
    case WeatherGeocodingProvider::OpenMeteo:
        return parseOpenMeteoGeocoding(payload, candidates);
    case WeatherGeocodingProvider::Nominatim:
        return parseNominatimGeocoding(payload, candidates);
    case WeatherGeocodingProvider::Photon:
        return parsePhotonGeocoding(payload, candidates);
    }

    return false;
}

QVector<WeatherLocationCandidate> filterRelevantGeocodingCandidates(
    WeatherGeocodingProvider provider,
    const QVector<WeatherLocationCandidate> &candidates,
    const WeatherLocationSearch &search)
{
    if (provider == WeatherGeocodingProvider::OpenMeteo || candidates.isEmpty())
    {
        return candidates;
    }

    const QString normQuery = normalizeNameForMatch(search.queryName);
    const QString normOriginal = normalizeNameForMatch(search.originalName);
    const bool queryHasCjk = containsCjk(normQuery) || containsCjk(normOriginal);
    const bool queryIsAscii = !normQuery.isEmpty() && isAsciiOnly(normQuery);

    if (normQuery.isEmpty() && normOriginal.isEmpty())
    {
        return candidates;
    }

    QVector<WeatherLocationCandidate> filtered;
    for (const WeatherLocationCandidate &candidate : candidates)
    {
        const QString normCity = normalizeNameForMatch(candidate.cityName);
        const QString normAdmin = normalizeNameForMatch(candidate.adminName);
        const bool candHasCjk = containsCjk(normCity) || containsCjk(normAdmin);

        bool matched = false;
        if (candHasCjk && queryHasCjk)
        {
            if (containsCjk(normQuery) && matchesSingleNormalizedName(normQuery, normCity))
            {
                matched = true;
            }
            else if (containsCjk(normOriginal) && matchesSingleNormalizedName(normOriginal, normCity))
            {
                matched = true;
            }
        }
        else if (!candHasCjk && queryIsAscii)
        {
            if (matchesSingleNormalizedName(normQuery, normCity))
            {
                matched = true;
            }
        }
        else if (candHasCjk && !queryHasCjk)
        {
            matched = true;
        }

        if (matched)
        {
            filtered.append(candidate);
        }
    }

    return filtered;
}

bool parseForecastResponse(
    WeatherForecastProvider provider,
    const QByteArray &payload,
    const WeatherQuery &query,
    const WeatherLocation &location,
    WeatherReport *report,
    QString *errorMessage)
{
    if (report == nullptr)
    {
        return false;
    }

    switch (provider)
    {
    case WeatherForecastProvider::OpenMeteo:
        return parseOpenMeteoForecast(payload, query, location, report, errorMessage);
    case WeatherForecastProvider::Wttr:
        return parseWttrForecast(payload, query, location, report, errorMessage);
    case WeatherForecastProvider::MetNorway:
        return parseMetNorwayForecast(payload, query, location, report, errorMessage);
    }

    return false;
}
}
