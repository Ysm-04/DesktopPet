#pragma once

#include "AppDiscovery.h"
#include "AppLaunchTypes.h"

#include <QString>

class AppLaunchManager
{
public:
    bool isLaunchIntentText(const QString &text) const;
    QString requestedAppNameFromText(const QString &text) const;

    AppLaunchPlan resolveLaunchPlan(const QString &text, const AppLaunchConfig &config) const;
    AppLaunchPlan manualSelectionPlan(const QString &text, const QString &executablePath) const;
    AppLaunchResult executeLaunchPlan(const AppLaunchPlan &plan) const;
    RegisteredApp registeredAppFromPlan(const AppLaunchPlan &plan, const QStringList &aliases) const;

private:
    bool isBlockedFileOperationText(const QString &text) const;
    bool validateLaunchPlan(const AppLaunchPlan &plan, QString *errorMessage) const;
    AppLaunchPlan planFromRegisteredApp(const QString &requestedName, const RegisteredApp &app, const QString &matchSource) const;

    AppDiscovery m_discovery;
};
