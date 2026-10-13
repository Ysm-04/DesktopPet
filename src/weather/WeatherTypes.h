#pragma once

#include "WeatherConfig.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>
#include <QtGlobal>

enum class WeatherLocationSource
{
    ExplicitCity,
    SettingsDefault,
    IpFallback,
};

enum class WeatherQueryKind
{
    Current,
    Daily,
    Range,
};

enum class WeatherIpLocationProvider
{
    IpApiCo,
    IpWhoIs,
};

struct WeatherQuery
{
    QString originalText;
    QString cityName;
    WeatherLocationSource locationSource = WeatherLocationSource::ExplicitCity;
    WeatherQueryKind kind = WeatherQueryKind::Current;
    int dateOffset = 0;
    int forecastDays = 1;
    QString unsupportedReason;
};

struct WeatherLocationSearch
{
    QString originalName;
    QString queryName;
    QString countryCode;
};

struct WeatherLocation
{
    QString cityName;
    QString adminName;
    QString countryName;
    QString timezone;
    double latitude = 0.0;
    double longitude = 0.0;
    WeatherLocationSource source = WeatherLocationSource::ExplicitCity;
};

struct WeatherLocationCandidate
{
    QString cityName;
    QString adminName;
    QString countryName;
    QString timezone;
    double latitude = 0.0;
    double longitude = 0.0;
    QString countryCode;
    QString featureCode;
    qint64 population = -1;
};

struct WeatherCurrent
{
    bool valid = false;
    QDateTime time;
    int weatherCode = -1;
    double temperatureC = 0.0;
    bool hasTemperature = false;
    double apparentTemperatureC = 0.0;
    bool hasApparentTemperature = false;
    double humidityPercent = 0.0;
    bool hasHumidity = false;
    double windSpeedKmh = 0.0;
    bool hasWindSpeed = false;
    double windDirectionDegree = 0.0;
    bool hasWindDirection = false;
    double precipitationMm = 0.0;
    bool hasPrecipitation = false;
};

struct WeatherDailyForecast
{
    QDate date;
    int weatherCode = -1;
    double temperatureMaxC = 0.0;
    bool hasTemperatureMax = false;
    double temperatureMinC = 0.0;
    bool hasTemperatureMin = false;
    double precipitationProbabilityPercent = 0.0;
    bool hasPrecipitationProbability = false;
};

struct WeatherReport
{
    WeatherQuery query;
    WeatherLocation location;
    WeatherCurrent current;
    QVector<WeatherDailyForecast> dailyForecasts;
    QVector<WeatherLocationCandidate> locationCandidates;
    QString dataSourceName = QStringLiteral("Open-Meteo");
    bool hasLocationAmbiguity = false;
};

struct WeatherQueryResult
{
    bool success = false;
    QString message;
    QString errorMessage;
    WeatherReport report;
};

struct LocationTestResult
{
    bool success = false;
    QString errorMessage;
    WeatherLocation selectedLocation;
    QVector<WeatherLocationCandidate> candidates;
    bool hasAmbiguity = false;
};
