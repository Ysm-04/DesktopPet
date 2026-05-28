#include "ConfigManager.h"

#include "../util/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>

namespace
{
const QString AppConfigFileName = QStringLiteral("app_config.json");

QJsonObject windowObjectFromConfig(const AppConfig &config)
{
    QJsonObject window;
    window.insert(QStringLiteral("x"), config.windowPosition.x());
    window.insert(QStringLiteral("y"), config.windowPosition.y());
    window.insert(QStringLiteral("alwaysOnTop"), config.alwaysOnTop);
    return window;
}
}

ConfigManager::ConfigManager() = default;

AppConfig ConfigManager::loadAppConfig() const
{
    AppConfig config;

    QFile file(appConfigPath());
    if (!file.exists())
    {
        return config;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::warning(QStringLiteral("Unable to read app config."));
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenConfig(appConfigPath());
        Logger::warning(QStringLiteral("App config is broken; default config will be used."));
        return config;
    }

    const QJsonObject root = document.object();
    const QJsonObject window = root.value(QStringLiteral("window")).toObject();
    if (window.contains(QStringLiteral("x")) && window.contains(QStringLiteral("y")))
    {
        config.windowPosition = QPoint(
            window.value(QStringLiteral("x")).toInt(config.windowPosition.x()),
            window.value(QStringLiteral("y")).toInt(config.windowPosition.y()));
        config.hasWindowPosition = true;
    }

    if (window.contains(QStringLiteral("alwaysOnTop")))
    {
        config.alwaysOnTop = window.value(QStringLiteral("alwaysOnTop")).toBool(config.alwaysOnTop);
    }

    return config;
}

bool ConfigManager::saveAppConfig(const AppConfig &config) const
{
    const QString directoryPath = configDirectoryPath();
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        Logger::warning(QStringLiteral("Unable to create config directory."));
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("window"), windowObjectFromConfig(config));

    QFile file(appConfigPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        Logger::warning(QStringLiteral("Unable to open app config for writing."));
        return false;
    }

    const QJsonDocument document(root);
    return file.write(document.toJson(QJsonDocument::Indented)) >= 0;
}

QString ConfigManager::appConfigPath() const
{
    return QDir(configDirectoryPath()).filePath(AppConfigFileName);
}

QString ConfigManager::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty())
    {
        return path;
    }

    return QDir::currentPath();
}

void ConfigManager::backupBrokenConfig(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString backupPath = fileInfo.dir().filePath(fileInfo.completeBaseName() + QStringLiteral(".broken.json"));
    if (QFile::exists(backupPath))
    {
        QFile::remove(backupPath);
    }

    file.rename(backupPath);
    Logger::warning(QStringLiteral("Broken app config was backed up."));
}
