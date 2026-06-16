#pragma once

#include "WeatherTypes.h"

#include <QString>

class WeatherSummaryFormatter
{
public:
    QString format(const WeatherReport &report) const;

    static QString weatherCodeText(int code);
    static QString windDirectionText(double degree);
};
