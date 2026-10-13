#include "WeatherManager.h"

#include "../util/Logger.h"
#include "WeatherLocationResolver.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

#include <utility>

namespace
{
constexpr int ForecastDays = 4;
constexpr int GeocodingCandidateCount = 5;
constexpr int IpLocationProviderCount = 2;
constexpr int IpLocationAttemptTimeoutMs = 5000;

QString normalizedProvider(const WeatherConfig &config)
{
    const QString provider = config.provider.trimmed().toLower();
    return provider.isEmpty() ? QStringLiteral("open-meteo") : provider;
}

QString normalizedLanguage(const WeatherConfig &config)
{
    const QString language = config.language.trimmed().toLower();
    return language.isEmpty() ? QStringLiteral("zh") : language;
}

QDateTime dateTimeFromOpenMeteo(const QString &text)
{
    QDateTime value = QDateTime::fromString(text, Qt::ISODate);
    if (value.isValid())
    {
        return value;
    }

    value = QDateTime::fromString(text, QStringLiteral("yyyy-MM-ddTHH:mm"));
    if (value.isValid())
    {
        return value;
    }

    return {};
}

bool jsonDouble(const QJsonObject &object, const QString &key, double *value)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
    {
        return false;
    }
    if (value != nullptr)
    {
        *value = jsonValue.toDouble();
    }
    return true;
}

bool jsonInt(const QJsonObject &object, const QString &key, int *value)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble())
    {
        return false;
    }
    if (value != nullptr)
    {
        *value = jsonValue.toInt();
    }
    return true;
}

QJsonArray arrayValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isArray() ? value.toArray() : QJsonArray();
}

WeatherLocation locationFromCandidate(const WeatherLocationCandidate &candidate, WeatherLocationSource source)
{
    WeatherLocation location;
    location.cityName = candidate.cityName;
    location.adminName = candidate.adminName;
    location.countryName = candidate.countryName;
    location.timezone = candidate.timezone;
    location.latitude = candidate.latitude;
    location.longitude = candidate.longitude;
    location.source = source;
    return location;
}

QVector<WeatherLocationCandidate> candidatesFromGeocodingResults(const QJsonArray &results)
{
    QVector<WeatherLocationCandidate> candidates;
    for (const QJsonValue &value : results)
    {
        if (!value.isObject())
        {
            continue;
        }

        const QJsonObject city = value.toObject();
        WeatherLocationCandidate candidate;
        if (!jsonDouble(city, QStringLiteral("latitude"), &candidate.latitude)
            || !jsonDouble(city, QStringLiteral("longitude"), &candidate.longitude))
        {
            continue;
        }

        candidate.cityName = city.value(QStringLiteral("name")).toString();
        candidate.adminName = city.value(QStringLiteral("admin1")).toString();
        candidate.countryName = city.value(QStringLiteral("country")).toString();
        candidate.timezone = city.value(QStringLiteral("timezone")).toString();
        candidate.countryCode = city.value(QStringLiteral("country_code")).toString().trimmed().toUpper();
        candidate.featureCode = city.value(QStringLiteral("feature_code")).toString().trimmed().toUpper();
        const QJsonValue population = city.value(QStringLiteral("population"));
        if (population.isDouble())
        {
            candidate.population = qRound64(population.toDouble());
        }
        candidates.append(candidate);
    }
    return candidates;
}
}

WeatherManager::WeatherManager()
{
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]() {
        if (m_ipLocationAttemptActive)
        {
            Logger::warning(QStringLiteral("Weather IP location provider timed out: index=%1")
                .arg(m_ipLocationProviderIndex));
            m_ipLocationAttemptActive = false;
            clearReply();
            startNextIpLocationRequest();
            return;
        }

        if (m_locationTestOnly)
        {
            finishLocationTestWithError(QStringLiteral("城市测试超时，请稍后再试。"));
            return;
        }

        finishWithError(QStringLiteral("天气查询超时，请稍后再试。"));
    });
}

WeatherManager::~WeatherManager()
{
    cancel();
}

bool WeatherManager::isBusy() const
{
    return m_busy;
}

void WeatherManager::queryWeather(const QString &text, const WeatherConfig &config, QueryCallback callback)
{
    if (isBusy())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("天气查询正在进行，请稍后。"), {}});
        }
        return;
    }

    m_query = m_parser.parse(text);
    if (!m_query.unsupportedReason.isEmpty())
    {
        if (callback)
        {
            callback({false, {}, m_query.unsupportedReason, {}});
        }
        return;
    }

    m_config = config;
    m_config.provider = normalizedProvider(m_config);
    m_config.language = normalizedLanguage(m_config);
    m_config.timeoutMs = qBound(3000, m_config.timeoutMs, 60000);
    m_callback = std::move(callback);
    m_locationTestCallback = nullptr;
    m_locationTestOnly = false;
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;
    m_busy = true;

    if (m_config.provider != QStringLiteral("open-meteo"))
    {
        finishWithError(QStringLiteral("当前天气源暂不支持：%1。").arg(m_config.provider));
        return;
    }

    if (!m_query.cityName.trimmed().isEmpty())
    {
        m_query.locationSource = WeatherLocationSource::ExplicitCity;
        startGeocodingRequest(m_query.cityName.trimmed(), WeatherLocationSource::ExplicitCity, false);
        return;
    }

    const QString defaultCity = m_config.defaultCityName.trimmed();
    if (!defaultCity.isEmpty())
    {
        m_query.locationSource = WeatherLocationSource::SettingsDefault;
        startGeocodingRequest(defaultCity, WeatherLocationSource::SettingsDefault, false);
        return;
    }

    if (m_config.autoLocateWhenNoDefault)
    {
        m_query.locationSource = WeatherLocationSource::IpFallback;
        startIpLocationRequest();
        return;
    }

    finishWithError(QStringLiteral("没有识别到城市。请在问题里写城市，或到设置页配置默认城市。"));
}

void WeatherManager::resolveLocationForTest(const QString &cityName, const WeatherConfig &config, LocationTestCallback callback)
{
    if (isBusy())
    {
        if (callback)
        {
            callback({false, QStringLiteral("城市测试正在进行，请稍后。"), {}, {}, false});
        }
        return;
    }

    const QString normalizedCityName = cityName.trimmed();
    if (normalizedCityName.isEmpty())
    {
        if (callback)
        {
            callback({false, QStringLiteral("请先填写默认城市。"), {}, {}, false});
        }
        return;
    }

    m_config = config;
    m_config.provider = normalizedProvider(m_config);
    m_config.language = normalizedLanguage(m_config);
    m_config.timeoutMs = qBound(3000, m_config.timeoutMs, 60000);
    m_query = {};
    m_query.cityName = normalizedCityName;
    m_query.locationSource = WeatherLocationSource::SettingsDefault;
    m_callback = nullptr;
    m_locationTestCallback = std::move(callback);
    m_locationTestOnly = true;
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;
    m_busy = true;

    if (m_config.provider != QStringLiteral("open-meteo"))
    {
        finishLocationTestWithError(QStringLiteral("当前天气源暂不支持：%1。").arg(m_config.provider));
        return;
    }

    startGeocodingRequest(normalizedCityName, WeatherLocationSource::SettingsDefault, true);
}

void WeatherManager::cancel()
{
    m_callback = nullptr;
    m_locationTestCallback = nullptr;
    m_locationTestOnly = false;
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;
    m_locationSearch = {};
    m_ipLocationAttemptActive = false;
    m_ipLocationProviderIndex = -1;
    clearReply();
    m_busy = false;
}

void WeatherManager::startGeocodingRequest(const QString &cityName, WeatherLocationSource source, bool locationTestOnly)
{
    m_location = {};
    m_location.source = source;
    m_locationTestOnly = locationTestOnly;
    m_locationSearch = WeatherLocationResolver::resolveSearch(cityName);
    if (m_locationSearch.queryName.trimmed().isEmpty())
    {
        finishGeocodingWithError(QStringLiteral("没有识别到有效城市名称。"));
        return;
    }

    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("name"), m_locationSearch.queryName);
    query.addQueryItem(QStringLiteral("count"), QString::number(GeocodingCandidateCount));
    query.addQueryItem(QStringLiteral("language"), m_config.language);
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    if (!m_locationSearch.countryCode.isEmpty())
    {
        query.addQueryItem(QStringLiteral("countryCode"), m_locationSearch.countryCode);
    }
    url.setQuery(query);

    startRequest(url, [this](QNetworkReply *reply) {
        finishGeocodingRequest(reply);
    });
}

void WeatherManager::startIpLocationRequest()
{
    m_location = {};
    m_location.source = WeatherLocationSource::IpFallback;
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;
    m_ipLocationProviderIndex = -1;
    m_ipLocationAttemptActive = false;
    startNextIpLocationRequest();
}

void WeatherManager::startNextIpLocationRequest()
{
    ++m_ipLocationProviderIndex;
    if (m_ipLocationProviderIndex >= IpLocationProviderCount)
    {
        m_ipLocationAttemptActive = false;
        finishWithError(QStringLiteral("公网 IP 定位服务暂不可用，请在设置页填写默认城市后重试。"));
        return;
    }

    const WeatherIpLocationProvider provider = m_ipLocationProviderIndex == 0
        ? WeatherIpLocationProvider::IpApiCo
        : WeatherIpLocationProvider::IpWhoIs;
    const QUrl url(provider == WeatherIpLocationProvider::IpApiCo
        ? QStringLiteral("https://ipapi.co/json/")
        : QStringLiteral("https://ipwho.is/"));

    m_ipLocationAttemptActive = true;
    startRequest(url, [this, provider](QNetworkReply *reply) {
        finishIpLocationRequest(reply, provider);
    }, qMin(m_config.timeoutMs, IpLocationAttemptTimeoutMs));
}

void WeatherManager::startForecastRequest()
{
    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(m_location.latitude, 'f', 6));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(m_location.longitude, 'f', 6));
    query.addQueryItem(QStringLiteral("current"), QStringLiteral("temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,precipitation"));
    query.addQueryItem(QStringLiteral("daily"), QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    query.addQueryItem(QStringLiteral("forecast_days"), QString::number(ForecastDays));
    url.setQuery(query);

    startRequest(url, [this](QNetworkReply *reply) {
        finishForecastRequest(reply);
    });
}

void WeatherManager::finishGeocodingRequest(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        Logger::warning(QStringLiteral("Weather geocoding request failed: networkError=%1 http=%2")
            .arg(static_cast<int>(reply->error()))
            .arg(httpStatus));
        finishGeocodingWithError(QStringLiteral("城市解析服务暂不可用，请稍后再试。"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        finishGeocodingWithError(QStringLiteral("城市解析结果格式无效。"));
        return;
    }

    const QJsonArray results = document.object().value(QStringLiteral("results")).toArray();
    m_locationCandidates = WeatherLocationResolver::selectRelevantCandidates(
        candidatesFromGeocodingResults(results),
        m_locationSearch);
    if (m_locationCandidates.isEmpty())
    {
        finishGeocodingWithError(QStringLiteral("没有找到匹配的城市。"));
        return;
    }

    m_hasLocationAmbiguity = WeatherLocationResolver::hasAmbiguity(m_locationCandidates);
    m_location = locationFromCandidate(m_locationCandidates.first(), m_location.source);

    if (m_locationTestOnly)
    {
        LocationTestResult result;
        result.success = true;
        result.selectedLocation = m_location;
        result.candidates = m_locationCandidates;
        result.hasAmbiguity = m_hasLocationAmbiguity;
        invokeLocationTestCallback(result);
        return;
    }

    Logger::info(QStringLiteral("Weather geocoding succeeded: source=%1 http=%2")
        .arg(QString::number(static_cast<int>(m_location.source)), QString::number(httpStatus)));
    startForecastRequest();
}

void WeatherManager::finishIpLocationRequest(QNetworkReply *reply, WeatherIpLocationProvider provider)
{
    m_ipLocationAttemptActive = false;
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        Logger::warning(QStringLiteral("Weather IP location request failed: provider=%1 networkError=%2 http=%3")
            .arg(provider == WeatherIpLocationProvider::IpApiCo ? QStringLiteral("ipapi") : QStringLiteral("ipwhois"))
            .arg(static_cast<int>(reply->error()))
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        startNextIpLocationRequest();
        return;
    }

    WeatherLocation location;
    if (!WeatherLocationResolver::parseIpLocationResponse(body, provider, &location))
    {
        Logger::warning(QStringLiteral("Weather IP location response was unusable: provider=%1")
            .arg(provider == WeatherIpLocationProvider::IpApiCo ? QStringLiteral("ipapi") : QStringLiteral("ipwhois")));
        startNextIpLocationRequest();
        return;
    }

    m_location = location;
    m_ipLocationProviderIndex = -1;
    Logger::info(QStringLiteral("Weather IP location succeeded: provider=%1")
        .arg(provider == WeatherIpLocationProvider::IpApiCo ? QStringLiteral("ipapi") : QStringLiteral("ipwhois")));
    startForecastRequest();
}

void WeatherManager::finishForecastRequest(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        Logger::warning(QStringLiteral("Weather forecast request failed: networkError=%1 http=%2")
            .arg(static_cast<int>(reply->error()))
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()));
        finishWithError(QStringLiteral("天气数据服务暂不可用，请稍后再试。"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        finishWithError(QStringLiteral("天气数据格式无效。"));
        return;
    }

    const QJsonObject root = document.object();
    WeatherReport report;
    report.query = m_query;
    report.location = m_location;
    report.dataSourceName = QStringLiteral("Open-Meteo");
    report.locationCandidates = m_locationCandidates;
    report.hasLocationAmbiguity = m_hasLocationAmbiguity;

    const QJsonObject currentObject = root.value(QStringLiteral("current")).toObject();
    if (!currentObject.isEmpty())
    {
        report.current.valid = true;
        report.current.time = dateTimeFromOpenMeteo(currentObject.value(QStringLiteral("time")).toString());
        report.current.hasTemperature = jsonDouble(currentObject, QStringLiteral("temperature_2m"), &report.current.temperatureC);
        report.current.hasApparentTemperature = jsonDouble(currentObject, QStringLiteral("apparent_temperature"), &report.current.apparentTemperatureC);
        report.current.hasHumidity = jsonDouble(currentObject, QStringLiteral("relative_humidity_2m"), &report.current.humidityPercent);
        report.current.hasWindSpeed = jsonDouble(currentObject, QStringLiteral("wind_speed_10m"), &report.current.windSpeedKmh);
        report.current.hasWindDirection = jsonDouble(currentObject, QStringLiteral("wind_direction_10m"), &report.current.windDirectionDegree);
        report.current.hasPrecipitation = jsonDouble(currentObject, QStringLiteral("precipitation"), &report.current.precipitationMm);
        jsonInt(currentObject, QStringLiteral("weather_code"), &report.current.weatherCode);
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
        report.dailyForecasts.append(forecast);
    }

    if (!report.current.valid && report.dailyForecasts.isEmpty())
    {
        finishWithError(QStringLiteral("天气数据缺少当前天气和未来预报。"));
        return;
    }

    finishWithReport(report);
}

void WeatherManager::startRequest(
    const QUrl &url,
    std::function<void(QNetworkReply *)> finishedHandler,
    int timeoutMs)
{
    clearReply();

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "QtDesktopPet/0.1");
    Logger::info(QStringLiteral("Weather request started: host=%1 path=%2").arg(url.host(), url.path()));

    m_currentReply = m_networkManager.get(request);
    m_replyFinishedConnection = QObject::connect(m_currentReply, &QNetworkReply::finished, [this, finishedHandler = std::move(finishedHandler)]() {
        if (m_currentReply.isNull())
        {
            return;
        }

        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        m_timeoutTimer.stop();
        QObject::disconnect(m_replyFinishedConnection);
        m_replyFinishedConnection = {};
        finishedHandler(reply);
        if (reply != nullptr)
        {
            reply->deleteLater();
        }
    });

    m_timeoutTimer.start(timeoutMs > 0 ? timeoutMs : m_config.timeoutMs);
}

void WeatherManager::finishWithError(const QString &message)
{
    m_ipLocationAttemptActive = false;
    m_ipLocationProviderIndex = -1;
    clearReply();
    WeatherQueryResult result;
    result.success = false;
    result.errorMessage = message.trimmed().isEmpty() ? QStringLiteral("天气查询失败。") : message.trimmed();
    invokeCallback(result);
}

void WeatherManager::finishGeocodingWithError(const QString &message)
{
    if (m_locationTestOnly)
    {
        finishLocationTestWithError(message);
        return;
    }

    finishWithError(message);
}

void WeatherManager::finishLocationTestWithError(const QString &message)
{
    clearReply();
    LocationTestResult result;
    result.success = false;
    result.errorMessage = message.trimmed().isEmpty() ? QStringLiteral("城市测试失败。") : message.trimmed();
    invokeLocationTestCallback(result);
}

void WeatherManager::finishWithReport(const WeatherReport &report)
{
    clearReply();
    WeatherQueryResult result;
    result.success = true;
    result.report = report;
    result.message = m_formatter.format(report);
    invokeCallback(result);
}

void WeatherManager::clearReply()
{
    m_timeoutTimer.stop();
    if (m_replyFinishedConnection)
    {
        QObject::disconnect(m_replyFinishedConnection);
        m_replyFinishedConnection = {};
    }

    QPointer<QNetworkReply> reply = m_currentReply;
    m_currentReply = nullptr;
    if (!reply.isNull())
    {
        reply->abort();
        reply->deleteLater();
    }
}

void WeatherManager::invokeCallback(const WeatherQueryResult &result)
{
    m_busy = false;
    QueryCallback callback = std::move(m_callback);
    m_callback = nullptr;
    if (callback)
    {
        callback(result);
    }
}

void WeatherManager::invokeLocationTestCallback(const LocationTestResult &result)
{
    m_busy = false;
    m_locationTestOnly = false;
    LocationTestCallback callback = std::move(m_locationTestCallback);
    m_locationTestCallback = nullptr;
    if (callback)
    {
        callback(result);
    }
}
