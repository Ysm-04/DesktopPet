#include "WeatherStore.h"

#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtGlobal>

namespace
{
constexpr int MinimumWeatherTimeoutMs = 3000;
constexpr int MaximumWeatherTimeoutMs = 60000;
const QString WeatherConfigFileName = QStringLiteral("weather_config.json");

WeatherConfig normalizedConfig(WeatherConfig config)
{
    config.provider = config.provider.trimmed().toLower();
    if (config.provider.isEmpty())
    {
        config.provider = QStringLiteral("open-meteo");
    }

    config.defaultCityName = config.defaultCityName.trimmed();
    config.language = config.language.trimmed().toLower();
    if (config.language.isEmpty())
    {
        config.language = QStringLiteral("zh");
    }
    config.timeoutMs = qBound(MinimumWeatherTimeoutMs, config.timeoutMs, MaximumWeatherTimeoutMs);
    return config;
}

QJsonObject objectFromConfig(const WeatherConfig &config)
{
    const WeatherConfig normalized = normalizedConfig(config);
    QJsonObject root;
    root.insert(QStringLiteral("provider"), normalized.provider);
    root.insert(QStringLiteral("defaultCityName"), normalized.defaultCityName);
    root.insert(QStringLiteral("autoLocateWhenNoDefault"), normalized.autoLocateWhenNoDefault);
    root.insert(QStringLiteral("language"), normalized.language);
    root.insert(QStringLiteral("timeoutMs"), normalized.timeoutMs);
    return root;
}
}

WeatherConfig WeatherStore::load(QString *errorMessage) const
{
    WeatherConfig config;

    QFile file(storePath());
    if (!file.exists())
    {
        return config;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取天气配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to read weather config."));
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        backupBrokenConfig(storePath());
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("天气配置文件损坏，已备份并使用默认配置。");
        }
        Logger::warning(QStringLiteral("Weather config is broken; default config will be used."));
        return config;
    }

    const QJsonObject root = document.object();
    config.provider = root.value(QStringLiteral("provider")).toString(config.provider);
    config.defaultCityName = root.value(QStringLiteral("defaultCityName")).toString(config.defaultCityName);
    config.autoLocateWhenNoDefault = root.value(QStringLiteral("autoLocateWhenNoDefault")).toBool(config.autoLocateWhenNoDefault);
    config.language = root.value(QStringLiteral("language")).toString(config.language);
    config.timeoutMs = root.value(QStringLiteral("timeoutMs")).toInt(config.timeoutMs);
    return normalizedConfig(config);
}

bool WeatherStore::save(const WeatherConfig &config, QString *errorMessage) const
{
    QDir directory(configDirectoryPath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建天气配置目录。");
        }
        Logger::warning(QStringLiteral("Unable to create weather config directory."));
        return false;
    }

    const QByteArray payload = QJsonDocument(objectFromConfig(config)).toJson(QJsonDocument::Indented);
    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入天气配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to open weather config for writing."));
        return false;
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入天气配置文件不完整。");
        }
        Logger::warning(QStringLiteral("Weather config write was incomplete."));
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交天气配置文件失败。");
        }
        Logger::warning(QStringLiteral("Unable to commit weather config."));
        return false;
    }

    return true;
}

QString WeatherStore::storePath() const
{
    return QDir(configDirectoryPath()).filePath(WeatherConfigFileName);
}

QString WeatherStore::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty())
    {
        return path;
    }

    return QDir::currentPath();
}

void WeatherStore::backupBrokenConfig(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(QStringLiteral("weather_config.broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            QStringLiteral("weather_config.broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken weather config was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken weather config: %1").arg(filePath));
}
