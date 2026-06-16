#include "AppDiscovery.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QtGlobal>

namespace
{
QString normalizedName(QString value)
{
    value = value.trimmed().toLower();
    value.remove(QChar::Space);
    value.remove(QStringLiteral(".exe"));
    value.remove(QStringLiteral(".lnk"));
    return value;
}

QStringList startMenuRoots()
{
    QStringList roots;
    const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString appData = environment.value(QStringLiteral("APPDATA"));
    const QString programData = environment.value(QStringLiteral("PROGRAMDATA"));
    if (!appData.isEmpty())
    {
        roots.append(QDir(appData).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs")));
    }
    if (!programData.isEmpty())
    {
        roots.append(QDir(programData).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs")));
    }
    return roots;
}

RegisteredApp appFromShortcut(const QString &shortcutPath)
{
    RegisteredApp app;
    const QFileInfo info(shortcutPath);
    app.id = info.completeBaseName().toLower();
    app.displayName = info.completeBaseName();
    app.aliases = {app.displayName};
    app.shortcutPath = info.absoluteFilePath();
    app.workingDirectory = info.absolutePath();
    app.userDefined = false;
    return app;
}

RegisteredApp appFromExecutable(const QString &executablePath)
{
    RegisteredApp app;
    const QFileInfo info(executablePath);
    app.id = info.completeBaseName().toLower();
    app.displayName = info.completeBaseName();
    app.aliases = {app.displayName, info.fileName()};
    app.executablePath = info.absoluteFilePath();
    app.workingDirectory = info.absolutePath();
    app.userDefined = false;
    return app;
}
}

bool AppDiscovery::findRegisteredApp(const QString &requestedName, const AppLaunchConfig &config, RegisteredApp *app) const
{
    for (const RegisteredApp &registeredApp : config.apps)
    {
        if (matchesName(requestedName, registeredApp))
        {
            if (app != nullptr)
            {
                *app = registeredApp;
            }
            return true;
        }
    }
    return false;
}

bool AppDiscovery::findStartMenuShortcut(const QString &requestedName, RegisteredApp *app) const
{
    const QString requested = normalizedName(requestedName);
    if (requested.isEmpty())
    {
        return false;
    }

    for (const QString &root : startMenuRoots())
    {
        if (!QDir(root).exists())
        {
            continue;
        }

        QDirIterator iterator(root, QStringList{QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString path = iterator.next();
            const QFileInfo info(path);
            if (normalizedName(info.completeBaseName()) == requested)
            {
                if (app != nullptr)
                {
                    *app = appFromShortcut(path);
                }
                return true;
            }
        }
    }
    return false;
}

bool AppDiscovery::findAppPathRegistryEntry(const QString &requestedName, RegisteredApp *app) const
{
#ifdef Q_OS_WIN
    const QString requested = normalizedName(requestedName);
    if (requested.isEmpty())
    {
        return false;
    }

    const QStringList roots = {
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
    };
    for (const QString &root : roots)
    {
        QSettings settings(root, QSettings::NativeFormat);
        for (const QString &group : settings.childGroups())
        {
            const QFileInfo groupInfo(group);
            if (normalizedName(groupInfo.completeBaseName()) != requested && normalizedName(group) != requested)
            {
                continue;
            }

            settings.beginGroup(group);
            QString executablePath = settings.value(QStringLiteral(".")).toString().trimmed();
            if (executablePath.isEmpty())
            {
                executablePath = settings.value(QStringLiteral("")).toString().trimmed();
            }
            settings.endGroup();
            if (executablePath.isEmpty())
            {
                continue;
            }

            if (app != nullptr)
            {
                *app = appFromExecutable(executablePath);
            }
            return true;
        }
    }
#else
    Q_UNUSED(requestedName);
    Q_UNUSED(app);
#endif
    return false;
}

bool AppDiscovery::matchesName(const QString &requestedName, const RegisteredApp &app) const
{
    const QString requested = normalizedName(requestedName);
    if (requested.isEmpty())
    {
        return false;
    }

    if (normalizedName(app.displayName) == requested)
    {
        return true;
    }

    for (const QString &alias : app.aliases)
    {
        if (normalizedName(alias) == requested)
        {
            return true;
        }
    }

    const QFileInfo executableInfo(app.executablePath);
    if (!app.executablePath.isEmpty() && normalizedName(executableInfo.completeBaseName()) == requested)
    {
        return true;
    }

    const QFileInfo shortcutInfo(app.shortcutPath);
    return !app.shortcutPath.isEmpty() && normalizedName(shortcutInfo.completeBaseName()) == requested;
}
