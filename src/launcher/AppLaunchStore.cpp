#include "AppLaunchStore.h"

#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace
{
const QString LauncherConfigFileName = QStringLiteral("launcher_config.json");

QString sanitizedId(const QString &value)
{
    QString id = value.trimmed().toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("_"));
    id = id.trimmed();
    if (id.isEmpty())
    {
        id = QStringLiteral("app");
    }
    return id.left(80);
}

QStringList aliasesFromJson(const QJsonArray &array)
{
    QStringList aliases;
    for (const QJsonValue &value : array)
    {
        const QString alias = value.toString().trimmed();
        if (!alias.isEmpty() && !aliases.contains(alias, Qt::CaseInsensitive))
        {
            aliases.append(alias);
        }
    }
    return aliases;
}

QJsonArray aliasesToJson(const QStringList &aliases)
{
    QJsonArray array;
    for (const QString &alias : aliases)
    {
        const QString normalized = alias.trimmed();
        if (!normalized.isEmpty())
        {
            array.append(normalized);
        }
    }
    return array;
}

RegisteredApp appFromJson(const QJsonObject &object)
{
    RegisteredApp app;
    app.id = sanitizedId(object.value(QStringLiteral("id")).toString());
    app.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
    app.aliases = aliasesFromJson(object.value(QStringLiteral("aliases")).toArray());
    app.executablePath = object.value(QStringLiteral("executablePath")).toString().trimmed();
    app.shortcutPath = object.value(QStringLiteral("shortcutPath")).toString().trimmed();
    app.workingDirectory = object.value(QStringLiteral("workingDirectory")).toString().trimmed();
    app.userDefined = object.value(QStringLiteral("userDefined")).toBool(true);
    if (app.displayName.isEmpty())
    {
        app.displayName = QFileInfo(app.executablePath.isEmpty() ? app.shortcutPath : app.executablePath).completeBaseName();
    }
    if (app.aliases.isEmpty() && !app.displayName.isEmpty())
    {
        app.aliases.append(app.displayName);
    }
    return app;
}

QJsonObject jsonFromApp(const RegisteredApp &app)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), sanitizedId(app.id.isEmpty() ? app.displayName : app.id));
    object.insert(QStringLiteral("displayName"), app.displayName.trimmed());
    object.insert(QStringLiteral("aliases"), aliasesToJson(app.aliases));
    object.insert(QStringLiteral("executablePath"), app.executablePath.trimmed());
    object.insert(QStringLiteral("shortcutPath"), app.shortcutPath.trimmed());
    object.insert(QStringLiteral("workingDirectory"), app.workingDirectory.trimmed());
    object.insert(QStringLiteral("userDefined"), app.userDefined);
    return object;
}

AppLaunchConfig normalizedConfig(AppLaunchConfig config)
{
    QVector<RegisteredApp> apps;
    QSet<QString> ids;
    for (RegisteredApp app : config.apps)
    {
        app.id = sanitizedId(app.id.isEmpty() ? app.displayName : app.id);
        if (ids.contains(app.id))
        {
            int suffix = 2;
            QString candidate;
            do
            {
                candidate = app.id + QStringLiteral("_") + QString::number(suffix++);
            } while (ids.contains(candidate));
            app.id = candidate;
        }

        app.displayName = app.displayName.trimmed();
        app.executablePath = app.executablePath.trimmed();
        app.shortcutPath = app.shortcutPath.trimmed();
        app.workingDirectory = app.workingDirectory.trimmed();
        QStringList aliases;
        for (const QString &alias : app.aliases)
        {
            const QString normalized = alias.trimmed();
            if (!normalized.isEmpty() && !aliases.contains(normalized, Qt::CaseInsensitive))
            {
                aliases.append(normalized);
            }
        }
        if (!app.displayName.isEmpty() && !aliases.contains(app.displayName, Qt::CaseInsensitive))
        {
            aliases.prepend(app.displayName);
        }
        app.aliases = aliases;
        if (app.displayName.isEmpty() || (app.executablePath.isEmpty() && app.shortcutPath.isEmpty()))
        {
            continue;
        }

        ids.insert(app.id);
        apps.append(app);
    }
    config.apps = apps;
    return config;
}
}

AppLaunchConfig AppLaunchStore::load(QString *errorMessage) const
{
    AppLaunchConfig config;

    QFile file(storePath());
    if (!file.exists())
    {
        return config;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取应用启动配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to read launcher config."));
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
            *errorMessage = QStringLiteral("应用启动配置文件损坏，已备份并使用默认配置。");
        }
        Logger::warning(QStringLiteral("Launcher config is broken; default config will be used."));
        return config;
    }

    const QJsonObject root = document.object();
    config.enabled = root.value(QStringLiteral("enabled")).toBool(config.enabled);
    config.allowManualExeSelection = root.value(QStringLiteral("allowManualExeSelection")).toBool(config.allowManualExeSelection);
    config.alwaysConfirmBeforeLaunch = root.value(QStringLiteral("alwaysConfirmBeforeLaunch")).toBool(config.alwaysConfirmBeforeLaunch);

    const QJsonArray appArray = root.value(QStringLiteral("apps")).toArray();
    for (const QJsonValue &value : appArray)
    {
        if (value.isObject())
        {
            config.apps.append(appFromJson(value.toObject()));
        }
    }
    return normalizedConfig(config);
}

bool AppLaunchStore::save(const AppLaunchConfig &config, QString *errorMessage) const
{
    QDir directory(configDirectoryPath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建应用启动配置目录。");
        }
        Logger::warning(QStringLiteral("Unable to create launcher config directory."));
        return false;
    }

    const AppLaunchConfig normalized = normalizedConfig(config);
    QJsonObject root;
    root.insert(QStringLiteral("enabled"), normalized.enabled);
    root.insert(QStringLiteral("allowManualExeSelection"), normalized.allowManualExeSelection);
    root.insert(QStringLiteral("alwaysConfirmBeforeLaunch"), normalized.alwaysConfirmBeforeLaunch);
    QJsonArray appArray;
    for (const RegisteredApp &app : normalized.apps)
    {
        appArray.append(jsonFromApp(app));
    }
    root.insert(QStringLiteral("apps"), appArray);

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(storePath());
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入应用启动配置文件。");
        }
        Logger::warning(QStringLiteral("Unable to open launcher config for writing."));
        return false;
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入应用启动配置文件不完整。");
        }
        Logger::warning(QStringLiteral("Launcher config write was incomplete."));
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交应用启动配置文件失败。");
        }
        Logger::warning(QStringLiteral("Unable to commit launcher config."));
        return false;
    }

    return true;
}

QString AppLaunchStore::storePath() const
{
    return QDir(configDirectoryPath()).filePath(LauncherConfigFileName);
}

QString AppLaunchStore::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty())
    {
        return path;
    }
    return QDir::currentPath();
}

void AppLaunchStore::backupBrokenConfig(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(QStringLiteral("launcher_config.broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            QStringLiteral("launcher_config.broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken launcher config was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken launcher config: %1").arg(filePath));
}
