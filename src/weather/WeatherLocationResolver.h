#pragma once

#include "WeatherTypes.h"

#include <QByteArray>

class WeatherLocationResolver
{
public:
    static WeatherLocationSearch resolveSearch(const QString &cityText);
    static bool parseIpLocationResponse(
        const QByteArray &payload,
        WeatherIpLocationProvider provider,
        WeatherLocation *location);
    static QVector<WeatherLocationCandidate> selectRelevantCandidates(
        const QVector<WeatherLocationCandidate> &candidates,
        const WeatherLocationSearch &search);
    static bool hasAmbiguity(const QVector<WeatherLocationCandidate> &candidates);
};
