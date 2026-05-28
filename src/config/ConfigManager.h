#pragma once

#include "AppConfig.h"

#include <QString>

class ConfigManager
{
public:
    ConfigManager();

    AppConfig loadAppConfig() const;
    bool saveAppConfig(const AppConfig &config) const;
    QString appConfigPath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath) const;
};
