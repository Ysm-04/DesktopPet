#pragma once

#include "WeatherLocationResolver.h"
#include "WeatherParser.h"
#include "WeatherProviderCodec.h"
#include "WeatherSummaryFormatter.h"
#include "WeatherTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include <functional>

class QNetworkReply;
class QUrl;

class WeatherManager
{
public:
    using QueryCallback = std::function<void(const WeatherQueryResult &)>;
    using LocationTestCallback = std::function<void(const LocationTestResult &)>;

    WeatherManager();
    ~WeatherManager();

    bool isBusy() const;
    void queryWeather(const QString &text, const WeatherConfig &config, QueryCallback callback);
    void resolveLocationForTest(const QString &cityName, const WeatherConfig &config, LocationTestCallback callback);
    void cancel();

private:
    enum class RequestStage
    {
        Idle,
        IpLocation,
        Geocoding,
        Forecast,
        DirectWttr,
    };

    struct CachedGeocodeEntry
    {
        WeatherLocation location;
        QVector<WeatherLocationCandidate> candidates;
        bool hasAmbiguity = false;
        QDateTime cachedAtUtc;
    };

    struct ForecastProviderHealth
    {
        int consecutiveTimeouts = 0;
        QDateTime demotedUntilUtc;
    };

    void startGeocodingPipeline();
    void startNextGeocodingAttempt();
    void startIpLocationRequest();
    void startNextIpLocationRequest();
    void startForecastPipeline();
    void startNextForecastAttempt();
    void startDirectWttrRequest();

    void finishGeocodingRequest(QNetworkReply *reply, WeatherGeocodingProvider provider);
    void finishIpLocationRequest(QNetworkReply *reply, WeatherIpLocationProvider provider);
    void finishForecastRequest(QNetworkReply *reply, WeatherForecastProvider provider);
    void finishDirectWttrRequest(QNetworkReply *reply);

    void startRequest(
        const QUrl &url,
        std::function<void(QNetworkReply *)> finishedHandler,
        int timeoutMs);
    int attemptTimeoutMs(bool hasMoreAttempts) const;
    bool tryApplyCachedGeocode(const WeatherLocationSearch &search, WeatherLocationSource source);
    void storeCachedGeocode(const WeatherLocationSearch &search);
    void handleAttemptTimeout();

    void finishGeocodingWithError(const QString &message);
    void finishWithError(const QString &message);
    void finishLocationTestWithError(const QString &message);
    void finishWithReport(const WeatherReport &report);
    void clearReply();
    void resetRequestState();
    void invokeCallback(const WeatherQueryResult &result);
    void invokeLocationTestCallback(const LocationTestResult &result);

    WeatherParser m_parser;
    WeatherSummaryFormatter m_formatter;
    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QMetaObject::Connection m_replyFinishedConnection;
    QTimer m_timeoutTimer;
    QElapsedTimer m_attemptTimer;
    QueryCallback m_callback;
    LocationTestCallback m_locationTestCallback;
    WeatherConfig m_config;
    WeatherQuery m_query;
    WeatherLocationSearch m_locationSearch;
    WeatherLocation m_location;
    QVector<WeatherLocationCandidate> m_locationCandidates;
    QVector<WeatherGeocodingProvider> m_geocodingOrder;
    QVector<WeatherForecastProvider> m_forecastOrder;
    QHash<QString, CachedGeocodeEntry> m_geocodeCache;
    QHash<int, ForecastProviderHealth> m_forecastProviderHealth;
    RequestStage m_stage = RequestStage::Idle;
    QString m_activeProviderLabel;
    QString m_activeHost;
    int m_activeTimeoutMs = 0;
    int m_geocodingAttemptIndex = -1;
    int m_forecastAttemptIndex = -1;
    int m_ipLocationProviderIndex = -1;
    bool m_locationTestOnly = false;
    bool m_hasLocationAmbiguity = false;
    bool m_allowDirectWttrFallback = true;
    bool m_directWttrTriedFirst = false;
    bool m_hadGeocodingNotFound = false;
    bool m_hadGeocodingHttpSuccess = false;
    bool m_busy = false;
};
