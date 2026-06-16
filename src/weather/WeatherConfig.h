#pragma once

#include <QString>

struct WeatherConfig
{
    QString provider = QStringLiteral("open-meteo");
    QString defaultCityName;
    bool autoLocateWhenNoDefault = true;
    QString language = QStringLiteral("zh");
    int timeoutMs = 10000;
};
