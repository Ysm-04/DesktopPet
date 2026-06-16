#include "WebStore.h"

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
constexpr int MinimumWebTimeoutMs = 3000;
constexpr int MaximumWebTimeoutMs = 120000;
const QString WebConfigFileName = QStringLiteral("web_config.json");

QString normalizedProviderMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("openai") || normalized == QStringLiteral("gemini"))
    {
        return normalized;
    }
    return QStringLiteral("auto");
}

WebConfig normalizedConfig(WebConfig config)
{
    config.providerMode = normalizedProviderMode(config.providerMode);
    config.timeoutMs = qBound(MinimumWebTimeoutMs, config.timeoutMs, MaximumWebTimeoutMs);
    return config;
}

QJsonObject objectFromConfig(const WebConfig &config)
{
    const WebConfig normalized = normalizedConfig(config);
    QJsonObject root;
    root.insert(QStringLiteral("enabled"), normalized.enabled);
    root.insert(QStringLiteral("rememberLastToggle"), normalized.rememberLastToggle);
    root.insert(QStringLiteral("defaultToggleOn"), normalized.defaultToggleOn);
    root.insert(QStringLiteral("lastToggleOn"), normalized.lastToggleOn);
    root.insert(QStringLiteral("providerMode"), normalized.providerMode);
    root.insert(QStringLiteral("timeoutMs"), normalized.timeoutMs);
    root.insert(QStringLiteral("showCitations"), normalized.showCitations);
    return root;
}
}

WebConfig WebStore::load(QString *errorMessage) const
{
    WebConfig config;

    QFile file(storePath());
    if (!file.exists())
    {
        return config;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取联网模式配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to read web config."));
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
            *errorMessage = QStringLiteral("联网模式配置文件损坏，已备份并使用默认配置。");
        }
        Logger::warning(QStringLiteral("Web config is broken; default config will be used."));
        return config;
    }

    const QJsonObject root = document.object();
    config.enabled = root.value(QStringLiteral("enabled")).toBool(config.enabled);
    config.rememberLastToggle = root.value(QStringLiteral("rememberLastToggle")).toBool(config.rememberLastToggle);
    config.defaultToggleOn = root.value(QStringLiteral("defaultToggleOn")).toBool(config.defaultToggleOn);
    config.lastToggleOn = root.value(QStringLiteral("lastToggleOn")).toBool(config.lastToggleOn);
    config.providerMode = root.value(QStringLiteral("providerMode")).toString(config.providerMode);
    config.timeoutMs = root.value(QStringLiteral("timeoutMs")).toInt(config.timeoutMs);
    config.showCitations = root.value(QStringLiteral("showCitations")).toBool(config.showCitations);
    return normalizedConfig(config);
}

bool WebStore::save(const WebConfig &config, QString *errorMessage) const
{
    QDir directory(configDirectoryPath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建联网模式配置目录。");
        }
        Logger::warning(QStringLiteral("Unable to create web config directory."));
        return false;
    }

    const QByteArray payload = QJsonDocument(objectFromConfig(config)).toJson(QJsonDocument::Indented);
    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入联网模式配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to open web config for writing."));
        return false;
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入联网模式配置文件不完整。");
        }
        Logger::warning(QStringLiteral("Web config write was incomplete."));
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交联网模式配置文件失败。");
        }
        Logger::warning(QStringLiteral("Unable to commit web config."));
        return false;
    }

    return true;
}

QString WebStore::storePath() const
{
    return QDir(configDirectoryPath()).filePath(WebConfigFileName);
}

QString WebStore::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty())
    {
        return path;
    }

    return QDir::currentPath();
}

void WebStore::backupBrokenConfig(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(QStringLiteral("web_config.broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            QStringLiteral("web_config.broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken web config was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken web config: %1").arg(filePath));
}
