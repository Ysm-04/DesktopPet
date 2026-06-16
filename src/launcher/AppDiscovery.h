#pragma once

#include "AppLaunchTypes.h"

#include <QString>

class AppDiscovery
{
public:
    bool findRegisteredApp(const QString &requestedName, const AppLaunchConfig &config, RegisteredApp *app) const;
    bool findStartMenuShortcut(const QString &requestedName, RegisteredApp *app) const;
    bool findAppPathRegistryEntry(const QString &requestedName, RegisteredApp *app) const;

private:
    bool matchesName(const QString &requestedName, const RegisteredApp &app) const;
};
