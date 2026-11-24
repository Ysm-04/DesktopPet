#include "WeatherManager.h"

#include "../util/Logger.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QtGlobal>

#include <utility>

namespace
{
constexpr int IpLocationProviderCount = 2;
constexpr int IpLocationAttemptTimeoutMs = 5000;
constexpr int MinAttemptTimeoutMs = 3000;
constexpr int FastFallbackAttemptCapMs = 5500;
constexpr int MaxSingleAttemptTimeoutMs = 10000;
constexpr int ForecastDemoteTimeoutThreshold = 2;
constexpr qint64 ForecastDemoteDurationSeconds = 300;
constexpr qint64 GeocodeCacheTtlSeconds = 6 * 3600;
constexpr int MaxGeocodeCacheEntries = 64;

QString normalizedLanguage(const WeatherConfig &config)
{
    const QString language = config.language.trimmed().toLower();
    return language.isEmpty() ? QStringLiteral("zh") : language;
}

QString geocodeCacheKey(const WeatherLocationSearch &search)
{
    return search.queryName.trimmed().toLower()
        + QLatin1Char('|')
        + search.countryCode.trimmed().toUpper();
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

QString stageName(int stageValue)
{
    switch (stageValue)
    {
    case 1:
        return QStringLiteral("ip-location");
    case 2:
        return QStringLiteral("geocoding");
    case 3:
        return QStringLiteral("forecast");
    case 4:
        return QStringLiteral("direct-wttr");
    default:
        return QStringLiteral("idle");
    }
}
}

WeatherManager::WeatherManager()
{
    m_timeoutTimer.setSingleShot(true);
    QObject::connect(&m_timeoutTimer, &QTimer::timeout, [this]() {
        handleAttemptTimeout();
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
    m_config.provider = WeatherProviderCodec::normalizedProviderSetting(m_config.provider);
    m_config.language = normalizedLanguage(m_config);
    m_config.timeoutMs = qBound(MinAttemptTimeoutMs, m_config.timeoutMs, 60000);
    m_callback = std::move(callback);
    m_locationTestCallback = nullptr;
    m_locationTestOnly = false;
    resetRequestState();
    m_busy = true;

    QString targetCity;
    WeatherLocationSource targetSource = WeatherLocationSource::ExplicitCity;
    if (!m_query.cityName.trimmed().isEmpty())
    {
        targetCity = m_query.cityName.trimmed();
        targetSource = WeatherLocationSource::ExplicitCity;
    }
    else if (!m_config.defaultCityName.trimmed().isEmpty())
    {
        targetCity = m_config.defaultCityName.trimmed();
        targetSource = WeatherLocationSource::SettingsDefault;
    }
    else if (m_config.autoLocateWhenNoDefault)
    {
        m_query.locationSource = WeatherLocationSource::IpFallback;
        startIpLocationRequest();
        return;
    }
    else
    {
        finishWithError(QStringLiteral("没有识别到城市。请在问题里写城市，或到设置页配置默认城市。"));
        return;
    }

    m_query.locationSource = targetSource;
    m_location = {};
    m_location.source = targetSource;
    m_locationSearch = WeatherLocationResolver::resolveSearch(targetCity);
    if (m_locationSearch.queryName.trimmed().isEmpty())
    {
        finishWithError(QStringLiteral("没有识别到有效城市名称。"));
        return;
    }

    m_allowDirectWttrFallback = WeatherProviderCodec::canUseDirectWttrQuery(m_query);
    if (tryApplyCachedGeocode(m_locationSearch, targetSource))
    {
        Logger::info(QStringLiteral("Weather geocoding cache hit: source=%1")
            .arg(static_cast<int>(targetSource)));
        startForecastPipeline();
        return;
    }

    if (m_config.provider == QStringLiteral("wttr") && m_allowDirectWttrFallback)
    {
        m_directWttrTriedFirst = true;
        startDirectWttrRequest();
        return;
    }

    startGeocodingPipeline();
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
    m_config.provider = WeatherProviderCodec::normalizedProviderSetting(m_config.provider);
    m_config.language = normalizedLanguage(m_config);
    m_config.timeoutMs = qBound(MinAttemptTimeoutMs, m_config.timeoutMs, 60000);
    m_query = {};
    m_query.cityName = normalizedCityName;
    m_query.locationSource = WeatherLocationSource::SettingsDefault;
    m_callback = nullptr;
    m_locationTestCallback = std::move(callback);
    m_locationTestOnly = true;
    resetRequestState();
    m_busy = true;

    m_location = {};
    m_location.source = WeatherLocationSource::SettingsDefault;
    m_locationSearch = WeatherLocationResolver::resolveSearch(normalizedCityName);
    if (m_locationSearch.queryName.trimmed().isEmpty())
    {
        finishLocationTestWithError(QStringLiteral("没有识别到有效城市名称。"));
        return;
    }

    startGeocodingPipeline();
}

void WeatherManager::cancel()
{
    m_callback = nullptr;
    m_locationTestCallback = nullptr;
    m_locationTestOnly = false;
    resetRequestState();
    clearReply();
    m_busy = false;
}

void WeatherManager::startGeocodingPipeline()
{
    m_geocodingOrder = WeatherProviderCodec::geocodingFallbackOrder();
    m_geocodingAttemptIndex = -1;
    m_hadGeocodingNotFound = false;
    m_hadGeocodingHttpSuccess = false;
    startNextGeocodingAttempt();
}

void WeatherManager::startNextGeocodingAttempt()
{
    ++m_geocodingAttemptIndex;
    if (m_geocodingAttemptIndex >= m_geocodingOrder.size())
    {
        if (!m_locationTestOnly
            && m_allowDirectWttrFallback
            && !m_directWttrTriedFirst
            && !m_hadGeocodingHttpSuccess)
        {
            Logger::warning(QStringLiteral("All weather geocoding providers failed; falling back to direct wttr.in lookup."));
            startDirectWttrRequest();
            return;
        }

        finishGeocodingWithError(m_hadGeocodingNotFound
            ? QStringLiteral("没有找到匹配的城市。")
            : QStringLiteral("所有城市解析服务均超时或暂不可用，请检查网络或稍后再试。"));
        return;
    }

    const WeatherGeocodingProvider provider = m_geocodingOrder.at(m_geocodingAttemptIndex);
    const bool hasMoreAttempts = (m_geocodingAttemptIndex + 1 < m_geocodingOrder.size())
        || (!m_locationTestOnly && m_allowDirectWttrFallback && !m_directWttrTriedFirst && !m_hadGeocodingHttpSuccess);
    const QUrl url = WeatherProviderCodec::buildGeocodingUrl(provider, m_locationSearch, m_config.language);

    m_stage = RequestStage::Geocoding;
    m_activeProviderLabel = WeatherProviderCodec::geocodingProviderName(provider);
    m_activeHost = url.host();
    startRequest(url, [this, provider](QNetworkReply *reply) {
        finishGeocodingRequest(reply, provider);
    }, attemptTimeoutMs(hasMoreAttempts));
}

void WeatherManager::startIpLocationRequest()
{
    m_location = {};
    m_location.source = WeatherLocationSource::IpFallback;
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;
    m_ipLocationProviderIndex = -1;
    startNextIpLocationRequest();
}

void WeatherManager::startNextIpLocationRequest()
{
    ++m_ipLocationProviderIndex;
    if (m_ipLocationProviderIndex >= IpLocationProviderCount)
    {
        m_stage = RequestStage::Idle;
        finishWithError(QStringLiteral("公网 IP 定位服务暂不可用，请在设置页填写默认城市后重试。"));
        return;
    }

    const WeatherIpLocationProvider provider = m_ipLocationProviderIndex == 0
        ? WeatherIpLocationProvider::IpApiCo
        : WeatherIpLocationProvider::IpWhoIs;
    const QUrl url(provider == WeatherIpLocationProvider::IpApiCo
        ? QStringLiteral("https://ipapi.co/json/")
        : QStringLiteral("https://ipwho.is/"));

    m_stage = RequestStage::IpLocation;
    m_activeProviderLabel = provider == WeatherIpLocationProvider::IpApiCo
        ? QStringLiteral("ipapi")
        : QStringLiteral("ipwhois");
    m_activeHost = url.host();
    startRequest(url, [this, provider](QNetworkReply *reply) {
        finishIpLocationRequest(reply, provider);
    }, qMin(m_config.timeoutMs, IpLocationAttemptTimeoutMs));
}

void WeatherManager::startForecastPipeline()
{
    const QVector<WeatherForecastProvider> baseOrder =
        WeatherProviderCodec::forecastFallbackOrder(m_config.provider, m_query);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    QVector<WeatherForecastProvider> demotedProviders;
    QStringList demotedNames;
    for (const WeatherForecastProvider provider : baseOrder)
    {
        const ForecastProviderHealth health = m_forecastProviderHealth.value(static_cast<int>(provider));
        if (health.demotedUntilUtc.isValid() && health.demotedUntilUtc > nowUtc)
        {
            demotedProviders.append(provider);
            demotedNames.append(WeatherProviderCodec::forecastProviderName(provider));
        }
    }

    m_forecastOrder = WeatherProviderCodec::applyForecastProviderDemotions(baseOrder, demotedProviders);
    if (!demotedNames.isEmpty() && !m_forecastOrder.isEmpty())
    {
        Logger::info(QStringLiteral("Weather forecast provider demoted temporarily: demoted=%1 first=%2")
            .arg(demotedNames.join(QLatin1Char(',')),
                 WeatherProviderCodec::forecastProviderName(m_forecastOrder.first())));
    }

    m_forecastAttemptIndex = -1;
    startNextForecastAttempt();
}

void WeatherManager::startNextForecastAttempt()
{
    ++m_forecastAttemptIndex;
    if (m_forecastAttemptIndex >= m_forecastOrder.size())
    {
        finishWithError(QStringLiteral("所有天气服务均超时或暂不可用，请检查网络或稍后再试。"));
        return;
    }

    const WeatherForecastProvider provider = m_forecastOrder.at(m_forecastAttemptIndex);
    const bool hasMoreAttempts = m_forecastAttemptIndex + 1 < m_forecastOrder.size();
    const QUrl url = WeatherProviderCodec::buildForecastUrl(provider, m_location);

    m_stage = RequestStage::Forecast;
    m_activeProviderLabel = WeatherProviderCodec::forecastProviderName(provider);
    m_activeHost = url.host();
    startRequest(url, [this, provider](QNetworkReply *reply) {
        finishForecastRequest(reply, provider);
    }, attemptTimeoutMs(hasMoreAttempts));
}

void WeatherManager::startDirectWttrRequest()
{
    const WeatherLocationSource preservedSource = m_location.source;
    m_location = {};
    m_location.source = preservedSource;
    m_location.cityName = m_locationSearch.originalName.trimmed().isEmpty()
        ? m_locationSearch.queryName.trimmed()
        : m_locationSearch.originalName.trimmed();
    m_locationCandidates.clear();
    m_hasLocationAmbiguity = false;

    const QUrl url = WeatherProviderCodec::buildDirectWttrUrl(m_locationSearch);
    m_stage = RequestStage::DirectWttr;
    m_activeProviderLabel = QStringLiteral("wttr.in");
    m_activeHost = url.host();
    startRequest(url, [this](QNetworkReply *reply) {
        finishDirectWttrRequest(reply);
    }, attemptTimeoutMs(m_directWttrTriedFirst));
}

void WeatherManager::finishGeocodingRequest(QNetworkReply *reply, WeatherGeocodingProvider provider)
{
    const qint64 elapsedMs = m_attemptTimer.elapsed();
    const QString providerName = WeatherProviderCodec::geocodingProviderName(provider);
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || httpStatus != 200)
    {
        Logger::warning(QStringLiteral("Weather geocoding attempt failed: provider=%1 elapsedMs=%2 networkError=%3 http=%4")
            .arg(providerName)
            .arg(elapsedMs)
            .arg(static_cast<int>(reply->error()))
            .arg(httpStatus));
        startNextGeocodingAttempt();
        return;
    }

    QVector<WeatherLocationCandidate> rawCandidates;
    if (!WeatherProviderCodec::parseGeocodingResponse(provider, body, &rawCandidates))
    {
        Logger::warning(QStringLiteral("Weather geocoding parse failed: provider=%1 elapsedMs=%2 bytes=%3")
            .arg(providerName)
            .arg(elapsedMs)
            .arg(body.size()));
        startNextGeocodingAttempt();
        return;
    }

    m_hadGeocodingHttpSuccess = true;
    const QVector<WeatherLocationCandidate> filteredCandidates =
        WeatherProviderCodec::filterRelevantGeocodingCandidates(provider, rawCandidates, m_locationSearch);
    m_locationCandidates = WeatherLocationResolver::selectRelevantCandidates(filteredCandidates, m_locationSearch);
    if (m_locationCandidates.isEmpty())
    {
        m_hadGeocodingNotFound = true;
        Logger::warning(QStringLiteral("Weather geocoding returned no matching candidates: provider=%1 elapsedMs=%2")
            .arg(providerName)
            .arg(elapsedMs));
        startNextGeocodingAttempt();
        return;
    }

    m_hasLocationAmbiguity = WeatherLocationResolver::hasAmbiguity(m_locationCandidates);
    m_location = locationFromCandidate(m_locationCandidates.first(), m_location.source);
    storeCachedGeocode(m_locationSearch);

    Logger::info(QStringLiteral("Weather geocoding succeeded: provider=%1 source=%2 elapsedMs=%3 http=%4 candidates=%5")
        .arg(providerName)
        .arg(static_cast<int>(m_location.source))
        .arg(elapsedMs)
        .arg(httpStatus)
        .arg(m_locationCandidates.size()));

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

    startForecastPipeline();
}

void WeatherManager::finishIpLocationRequest(QNetworkReply *reply, WeatherIpLocationProvider provider)
{
    const qint64 elapsedMs = m_attemptTimer.elapsed();
    const QString providerName = provider == WeatherIpLocationProvider::IpApiCo
        ? QStringLiteral("ipapi")
        : QStringLiteral("ipwhois");
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || httpStatus != 200)
    {
        Logger::warning(QStringLiteral("Weather IP location request failed: provider=%1 elapsedMs=%2 networkError=%3 http=%4")
            .arg(providerName)
            .arg(elapsedMs)
            .arg(static_cast<int>(reply->error()))
            .arg(httpStatus));
        startNextIpLocationRequest();
        return;
    }

    WeatherLocation location;
    if (!WeatherLocationResolver::parseIpLocationResponse(body, provider, &location))
    {
        Logger::warning(QStringLiteral("Weather IP location response was unusable: provider=%1 elapsedMs=%2")
            .arg(providerName)
            .arg(elapsedMs));
        startNextIpLocationRequest();
        return;
    }

    m_location = location;
    m_ipLocationProviderIndex = -1;
    Logger::info(QStringLiteral("Weather IP location succeeded: provider=%1 elapsedMs=%2")
        .arg(providerName)
        .arg(elapsedMs));
    startForecastPipeline();
}

void WeatherManager::finishForecastRequest(QNetworkReply *reply, WeatherForecastProvider provider)
{
    const qint64 elapsedMs = m_attemptTimer.elapsed();
    const QString providerName = WeatherProviderCodec::forecastProviderName(provider);
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || httpStatus != 200)
    {
        Logger::warning(QStringLiteral("Weather forecast attempt failed: provider=%1 elapsedMs=%2 networkError=%3 http=%4")
            .arg(providerName)
            .arg(elapsedMs)
            .arg(static_cast<int>(reply->error()))
            .arg(httpStatus));
        startNextForecastAttempt();
        return;
    }

    WeatherReport report;
    QString parseError;
    if (!WeatherProviderCodec::parseForecastResponse(provider, body, m_query, m_location, &report, &parseError))
    {
        Logger::warning(QStringLiteral("Weather forecast parse failed: provider=%1 elapsedMs=%2 reason=%3")
            .arg(providerName)
            .arg(elapsedMs)
            .arg(parseError));
        startNextForecastAttempt();
        return;
    }

    m_forecastProviderHealth.remove(static_cast<int>(provider));
    report.locationCandidates = m_locationCandidates;
    report.hasLocationAmbiguity = m_hasLocationAmbiguity;
    Logger::info(QStringLiteral("Weather forecast succeeded: provider=%1 elapsedMs=%2 http=%3")
        .arg(providerName)
        .arg(elapsedMs)
        .arg(httpStatus));
    finishWithReport(report);
}

void WeatherManager::finishDirectWttrRequest(QNetworkReply *reply)
{
    const qint64 elapsedMs = m_attemptTimer.elapsed();
    const QByteArray body = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || httpStatus != 200)
    {
        Logger::warning(QStringLiteral("Weather direct wttr.in attempt failed: elapsedMs=%1 networkError=%2 http=%3")
            .arg(elapsedMs)
            .arg(static_cast<int>(reply->error()))
            .arg(httpStatus));
        if (m_directWttrTriedFirst)
        {
            const WeatherLocationSource preservedSource = m_location.source;
            m_location = {};
            m_location.source = preservedSource;
            startGeocodingPipeline();
            return;
        }
        finishWithError(QStringLiteral("所有城市解析及备用天气服务均超时或不可用，请检查网络或稍后再试。"));
        return;
    }

    WeatherReport report;
    QString parseError;
    if (!WeatherProviderCodec::parseForecastResponse(
            WeatherForecastProvider::Wttr,
            body,
            m_query,
            m_location,
            &report,
            &parseError))
    {
        Logger::warning(QStringLiteral("Weather direct wttr.in parse failed: elapsedMs=%1 reason=%2")
            .arg(elapsedMs)
            .arg(parseError));
        if (m_directWttrTriedFirst)
        {
            const WeatherLocationSource preservedSource = m_location.source;
            m_location = {};
            m_location.source = preservedSource;
            startGeocodingPipeline();
            return;
        }
        finishWithError(QStringLiteral("所有城市解析及备用天气服务均超时或不可用，请检查网络或稍后再试。"));
        return;
    }

    report.locationCandidates = m_locationCandidates;
    report.hasLocationAmbiguity = m_hasLocationAmbiguity;
    Logger::info(QStringLiteral("Weather direct wttr.in succeeded: elapsedMs=%1 http=%2")
        .arg(elapsedMs)
        .arg(httpStatus));
    finishWithReport(report);
}

void WeatherManager::startRequest(
    const QUrl &url,
    std::function<void(QNetworkReply *)> finishedHandler,
    int timeoutMs)
{
    clearReply();

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "QtDesktopPet/0.2 (+https://github.com/Ysm-04/DesktopPet)");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_activeTimeoutMs = qBound(MinAttemptTimeoutMs, timeoutMs, 60000);
    m_attemptTimer.restart();
    Logger::info(QStringLiteral("Weather request started: stage=%1 provider=%2 host=%3 path=%4 timeoutMs=%5")
        .arg(stageName(static_cast<int>(m_stage)), m_activeProviderLabel, url.host(), url.path())
        .arg(m_activeTimeoutMs));

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

    m_timeoutTimer.start(m_activeTimeoutMs);
}

int WeatherManager::attemptTimeoutMs(bool hasMoreAttempts) const
{
    const int configuredTimeout = qBound(MinAttemptTimeoutMs, m_config.timeoutMs, MaxSingleAttemptTimeoutMs);
    if (hasMoreAttempts)
    {
        return qBound(MinAttemptTimeoutMs, qMin(configuredTimeout, FastFallbackAttemptCapMs), MaxSingleAttemptTimeoutMs);
    }
    return configuredTimeout;
}

bool WeatherManager::tryApplyCachedGeocode(const WeatherLocationSearch &search, WeatherLocationSource source)
{
    const QString key = geocodeCacheKey(search);
    const auto it = m_geocodeCache.constFind(key);
    if (it == m_geocodeCache.constEnd())
    {
        return false;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!it->cachedAtUtc.isValid() || it->cachedAtUtc.secsTo(nowUtc) > GeocodeCacheTtlSeconds)
    {
        m_geocodeCache.remove(key);
        return false;
    }

    m_location = it->location;
    m_location.source = source;
    m_locationCandidates = it->candidates;
    m_hasLocationAmbiguity = it->hasAmbiguity;
    return true;
}

void WeatherManager::storeCachedGeocode(const WeatherLocationSearch &search)
{
    const QString key = geocodeCacheKey(search);
    if (key.isEmpty() || m_location.cityName.trimmed().isEmpty())
    {
        return;
    }

    if (m_geocodeCache.size() >= MaxGeocodeCacheEntries && !m_geocodeCache.contains(key))
    {
        m_geocodeCache.clear();
    }

    CachedGeocodeEntry entry;
    entry.location = m_location;
    entry.candidates = m_locationCandidates;
    entry.hasAmbiguity = m_hasLocationAmbiguity;
    entry.cachedAtUtc = QDateTime::currentDateTimeUtc();
    m_geocodeCache.insert(key, entry);
}

void WeatherManager::handleAttemptTimeout()
{
    const RequestStage timedOutStage = m_stage;
    const qint64 elapsedMs = m_attemptTimer.isValid() ? m_attemptTimer.elapsed() : m_activeTimeoutMs;
    Logger::warning(QStringLiteral("Weather request timed out: stage=%1 provider=%2 host=%3 elapsedMs=%4 timeoutMs=%5")
        .arg(stageName(static_cast<int>(timedOutStage)), m_activeProviderLabel, m_activeHost)
        .arg(elapsedMs)
        .arg(m_activeTimeoutMs));

    clearReply();

    switch (timedOutStage)
    {
    case RequestStage::IpLocation:
        startNextIpLocationRequest();
        break;
    case RequestStage::Geocoding:
        startNextGeocodingAttempt();
        break;
    case RequestStage::Forecast:
        if (m_forecastAttemptIndex >= 0 && m_forecastAttemptIndex < m_forecastOrder.size())
        {
            const WeatherForecastProvider timedOutProvider = m_forecastOrder.at(m_forecastAttemptIndex);
            ForecastProviderHealth &health = m_forecastProviderHealth[static_cast<int>(timedOutProvider)];
            ++health.consecutiveTimeouts;
            if (health.consecutiveTimeouts >= ForecastDemoteTimeoutThreshold)
            {
                health.demotedUntilUtc = QDateTime::currentDateTimeUtc().addSecs(ForecastDemoteDurationSeconds);
                Logger::warning(QStringLiteral("Weather forecast provider demoted for 300s after consecutive timeouts: provider=%1 count=%2")
                    .arg(WeatherProviderCodec::forecastProviderName(timedOutProvider))
                    .arg(health.consecutiveTimeouts));
            }
        }
        startNextForecastAttempt();
        break;
    case RequestStage::DirectWttr:
        if (m_directWttrTriedFirst)
        {
            const WeatherLocationSource preservedSource = m_location.source;
            m_location = {};
            m_location.source = preservedSource;
            startGeocodingPipeline();
            break;
        }
        finishWithError(QStringLiteral("所有城市解析及备用天气服务均超时，请检查网络或稍后再试。"));
        break;
    case RequestStage::Idle:
        break;
    }
}

void WeatherManager::finishWithError(const QString &message)
{
    resetRequestState();
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
    resetRequestState();
    clearReply();
    LocationTestResult result;
    result.success = false;
    result.errorMessage = message.trimmed().isEmpty() ? QStringLiteral("城市测试失败。") : message.trimmed();
    invokeLocationTestCallback(result);
}

void WeatherManager::finishWithReport(const WeatherReport &report)
{
    resetRequestState();
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

void WeatherManager::resetRequestState()
{
    m_stage = RequestStage::Idle;
    m_locationCandidates.clear();
    m_geocodingOrder.clear();
    m_forecastOrder.clear();
    m_activeProviderLabel.clear();
    m_activeHost.clear();
    m_activeTimeoutMs = 0;
    m_geocodingAttemptIndex = -1;
    m_forecastAttemptIndex = -1;
    m_ipLocationProviderIndex = -1;
    m_hasLocationAmbiguity = false;
    m_allowDirectWttrFallback = true;
    m_directWttrTriedFirst = false;
    m_hadGeocodingNotFound = false;
    m_hadGeocodingHttpSuccess = false;
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
