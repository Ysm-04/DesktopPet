#pragma once

#include "WeatherTypes.h"

#include <QString>

class WeatherParser
{
public:
    WeatherQuery parse(const QString &text) const;
};
