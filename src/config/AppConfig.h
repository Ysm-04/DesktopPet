#pragma once

#include <QPoint>
#include <QString>

struct AppConfig
{
    QPoint windowPosition = QPoint(100, 100);
    bool hasWindowPosition = false;
    bool alwaysOnTop = true;
    double scale = 1.0;
    QString performanceMode = QStringLiteral("standard");
    bool pauseWhenHidden = true;
    bool enableLazyLoad = true;
    int requestContextMessageLimit = 12;
    int memoryHistoryMessageLimit = 200;
    bool saveConversationHistory = false;
    int savedHistoryMessageLimit = 500;
};
