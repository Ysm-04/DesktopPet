#pragma once

#include <QString>

struct WebConfig
{
    bool enabled = true;
    bool rememberLastToggle = true;
    bool defaultToggleOn = false;
    bool lastToggleOn = false;
    QString providerMode = QStringLiteral("auto");
    int timeoutMs = 60000;
    bool showCitations = true;
};
