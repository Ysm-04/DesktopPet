#pragma once

#include <QPoint>

struct AppConfig
{
    QPoint windowPosition = QPoint(100, 100);
    bool hasWindowPosition = false;
    bool alwaysOnTop = true;
};
