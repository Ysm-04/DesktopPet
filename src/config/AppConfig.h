#pragma once

#include <QPoint>
#include <QString>

struct AppConfig
{
    QPoint windowPosition = QPoint(100, 100);
    bool hasWindowPosition = false;
    bool alwaysOnTop = true;
    bool launchAtStartup = false;
    double scale = 1.0;
    QString performanceMode = QStringLiteral("standard");
    bool pauseWhenHidden = true;
    bool enableLazyLoad = true;
    bool enableAnimationPrewarm = true;
    int animationCacheLimitMb = 180;
    bool unloadAnimationsWhenHidden = true;
    QString characterId = QStringLiteral("shiroko");
    QString reminderSoundId = QStringLiteral("reminder_default");
    bool reminderSoundEnabled = true;
    double reminderSoundVolume = 0.8;
    int requestContextMessageLimit = 12;
    int memoryHistoryMessageLimit = 200;
    bool saveConversationHistory = false;
    int savedHistoryMessageLimit = 500;
};
