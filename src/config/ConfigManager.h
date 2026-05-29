#pragma once

#include "AIConfig.h"
#include "AppConfig.h"

#include <QString>

class ConfigManager
{
public:
    ConfigManager();

    AppConfig loadAppConfig() const;
    AIConfig loadAIConfig() const;
    bool saveAppConfig(const AppConfig &config) const;
    bool saveAIConfig(const AIConfig &config) const;
    QString appConfigPath() const;
    QString aiConfigPath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath) const;
};
