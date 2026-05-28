#pragma once

#include <QString>

class Logger
{
public:
    static void info(const QString &message);
    static void warning(const QString &message);
    static void error(const QString &message);

private:
    static void write(const QString &level, const QString &message);
    static QString logDirectoryPath();
    static QString logFilePath();
    static void rotateIfNeeded();
};
