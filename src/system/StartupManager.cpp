#include "StartupManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace
{
const QString RunRegistryPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString StartupEntryName = QStringLiteral("QtDesktopPet");

QString quotedCommand(const QString &filePath)
{
    QString nativePath = QDir::toNativeSeparators(filePath.trimmed());
    nativePath.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"") + nativePath + QStringLiteral("\"");
}
}

namespace StartupManager
{
QString startupEntryName()
{
    return StartupEntryName;
}

QString startupCommandForCurrentExecutable()
{
    const QString executablePath = QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
    return executablePath.trimmed().isEmpty() ? QString() : quotedCommand(executablePath);
}

bool isLaunchAtStartupEnabled()
{
    QSettings settings(RunRegistryPath, QSettings::NativeFormat);
    return !settings.value(StartupEntryName).toString().trimmed().isEmpty();
}

bool setLaunchAtStartupEnabled(bool enabled, QString *errorMessage)
{
    QSettings settings(RunRegistryPath, QSettings::NativeFormat);
    if (enabled)
    {
        const QString command = startupCommandForCurrentExecutable();
        if (command.isEmpty())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("无法获取当前程序路径，开机自启动设置未保存。");
            }
            return false;
        }

        settings.setValue(StartupEntryName, command);
    }
    else
    {
        settings.remove(StartupEntryName);
    }

    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = enabled
                ? QStringLiteral("写入开机自启动注册表失败。")
                : QStringLiteral("移除开机自启动注册表失败。");
        }
        return false;
    }

    return true;
}
}
