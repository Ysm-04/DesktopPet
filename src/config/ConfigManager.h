#pragma once

#include "AIConfig.h"
#include "AppConfig.h"

#include <QString>

class ConfigManager
{
public:
    ConfigManager();

    AppConfig loadAppConfig() const;
    AIConfigStore loadAIConfigStore() const;
    AIConfig loadAIConfig() const;
    bool saveAppConfig(const AppConfig &config) const;
    bool saveAIConfigStore(const AIConfigStore &store) const;
    bool saveAIConfig(const AIConfig &config) const;
    QString appConfigPath() const;
    QString aiConfigPath() const;
    QString conversationHistoryPath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath, const QString &configName) const;
};
