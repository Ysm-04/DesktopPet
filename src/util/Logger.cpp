#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace
{
const QString LogFileName = QStringLiteral("QtDesktopPet.log");
constexpr qint64 MaxLogFileSize = 2 * 1024 * 1024;
constexpr int MaxRotatedLogFiles = 3;
}

void Logger::info(const QString &message)
{
    write(QStringLiteral("INFO"), message);
}

void Logger::warning(const QString &message)
{
    write(QStringLiteral("WARN"), message);
}

void Logger::error(const QString &message)
{
    write(QStringLiteral("ERROR"), message);
}

void Logger::write(const QString &level, const QString &message)
{
    QDir directory(logDirectoryPath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        return;
    }

    rotateIfNeeded();

    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODate)
           << " [" << level << "] "
           << message
           << '\n';
}

QString Logger::logDirectoryPath()
{
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configPath.isEmpty())
    {
        return QDir(configPath).filePath(QStringLiteral("logs"));
    }

    return QDir(QDir::currentPath()).filePath(QStringLiteral("logs"));
}

QString Logger::logFilePath()
{
    return QDir(logDirectoryPath()).filePath(LogFileName);
}

void Logger::rotateIfNeeded()
{
    const QString currentPath = logFilePath();
    QFile currentFile(currentPath);
    if (!currentFile.exists() || currentFile.size() < MaxLogFileSize)
    {
        return;
    }

    for (int index = MaxRotatedLogFiles; index >= 1; --index)
    {
        const QString oldPath = currentPath + QStringLiteral(".") + QString::number(index);
        const QString nextPath = currentPath + QStringLiteral(".") + QString::number(index + 1);

        if (!QFile::exists(oldPath))
        {
            continue;
        }

        if (index == MaxRotatedLogFiles)
        {
            QFile::remove(oldPath);
        }
        else
        {
            QFile::remove(nextPath);
            QFile::rename(oldPath, nextPath);
        }
    }

    QFile::remove(currentPath + QStringLiteral(".1"));
    QFile::rename(currentPath, currentPath + QStringLiteral(".1"));
}
