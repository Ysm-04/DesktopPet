#pragma once

#include "WeatherParser.h"
#include "WeatherSummaryFormatter.h"
#include "WeatherTypes.h"

#include <QByteArray>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>

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
    void startGeocodingRequest(const QString &cityName, WeatherLocationSource source, bool locationTestOnly);
    void startIpLocationRequest();
    void startForecastRequest();
    void finishGeocodingRequest(QNetworkReply *reply);
    void finishIpLocationRequest(QNetworkReply *reply);
    void finishForecastRequest(QNetworkReply *reply);
    void startRequest(const QUrl &url, std::function<void(QNetworkReply *)> finishedHandler);
    void finishGeocodingWithError(const QString &message);
    void finishWithError(const QString &message);
    void finishLocationTestWithError(const QString &message);
    void finishWithReport(const WeatherReport &report);
    void clearReply();
    void invokeCallback(const WeatherQueryResult &result);
    void invokeLocationTestCallback(const LocationTestResult &result);

    WeatherParser m_parser;
    WeatherSummaryFormatter m_formatter;
    QNetworkAccessManager m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QMetaObject::Connection m_replyFinishedConnection;
    QTimer m_timeoutTimer;
    QueryCallback m_callback;
    LocationTestCallback m_locationTestCallback;
    WeatherConfig m_config;
    WeatherQuery m_query;
    WeatherLocation m_location;
    QVector<WeatherLocationCandidate> m_locationCandidates;
    bool m_locationTestOnly = false;
    bool m_hasLocationAmbiguity = false;
    bool m_busy = false;
};
