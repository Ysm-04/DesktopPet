#pragma once

#include "WeatherConfig.h"

#include <QString>

class WeatherStore
{
public:
    WeatherConfig load(QString *errorMessage = nullptr) const;
    bool save(const WeatherConfig &config, QString *errorMessage = nullptr) const;

    QString storePath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath) const;
};
