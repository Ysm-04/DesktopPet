#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct RegisteredApp
{
    QString id;
    QString displayName;
    QStringList aliases;
    QString executablePath;
    QString shortcutPath;
    QString workingDirectory;
    bool userDefined = true;
};

struct AppLaunchConfig
{
    bool enabled = true;
    bool allowManualExeSelection = true;
    bool alwaysConfirmBeforeLaunch = true;
    QVector<RegisteredApp> apps;
};

struct AppLaunchPlan
{
    bool success = false;
    bool needsManualSelection = false;
    QString requestedName;
    QString displayName;
    QString executablePath;
    QString shortcutPath;
    QString workingDirectory;
    QString matchSource;
    QString errorMessage;
    bool canRemember = false;
};

struct AppLaunchResult
{
    bool success = false;
    QString message;
    QString errorMessage;
};
