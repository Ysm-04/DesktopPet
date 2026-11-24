#pragma once

#include "WeatherTypes.h"

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

enum class WeatherGeocodingProvider
{
    OpenMeteo,
    Nominatim,
    Photon,
};

enum class WeatherForecastProvider
{
    OpenMeteo,
    Wttr,
    MetNorway,
};

namespace WeatherProviderCodec
{
QString normalizedProviderSetting(const QString &provider);
QString geocodingProviderName(WeatherGeocodingProvider provider);
QString forecastProviderName(WeatherForecastProvider provider);
QVector<WeatherGeocodingProvider> geocodingFallbackOrder();
QVector<WeatherForecastProvider> forecastFallbackOrder(
    const QString &preferredProvider,
    const WeatherQuery &query);
QVector<WeatherForecastProvider> applyForecastProviderDemotions(
    const QVector<WeatherForecastProvider> &order,
    const QVector<WeatherForecastProvider> &demotedProviders);
bool canUseDirectWttrQuery(const WeatherQuery &query);

QUrl buildGeocodingUrl(
    WeatherGeocodingProvider provider,
    const WeatherLocationSearch &search,
    const QString &language);
QUrl buildForecastUrl(
    WeatherForecastProvider provider,
    const WeatherLocation &location);
QUrl buildDirectWttrUrl(const WeatherLocationSearch &search);

bool parseGeocodingResponse(
    WeatherGeocodingProvider provider,
    const QByteArray &payload,
    QVector<WeatherLocationCandidate> *candidates);
QVector<WeatherLocationCandidate> filterRelevantGeocodingCandidates(
    WeatherGeocodingProvider provider,
    const QVector<WeatherLocationCandidate> &candidates,
    const WeatherLocationSearch &search);
bool parseForecastResponse(
    WeatherForecastProvider provider,
    const QByteArray &payload,
    const WeatherQuery &query,
    const WeatherLocation &location,
    WeatherReport *report,
    QString *errorMessage = nullptr);
}
