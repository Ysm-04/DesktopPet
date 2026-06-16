#pragma once

#include <QString>

namespace StartupManager
{
QString startupEntryName();
QString startupCommandForCurrentExecutable();
bool isLaunchAtStartupEnabled();
bool setLaunchAtStartupEnabled(bool enabled, QString *errorMessage = nullptr);
}
