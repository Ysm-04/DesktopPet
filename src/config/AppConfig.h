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
    bool requestContextUseMax = false;
    int requestContextMaxChars = 32000;
    int memoryHistoryMessageLimit = 200;
    bool saveConversationHistory = false;
    bool saveAllConversationHistory = false;
    int savedHistoryMessageLimit = 500;
    bool enableHistoryRecall = true;
    bool enableLongTermMemory = true;
    bool autoExtractLongTermMemory = false;
    bool enableTouchInteraction = true;
    bool enableCharacterEffects = true;
    bool enableStartupGreeting = true;
    bool enableProactiveCare = true;
    int sedentaryReminderMinutes = 60;
    bool enableAgentBridge = true;
    bool showAgentStatusBar = true;
    QString agentBubbleNotifyLevel = QStringLiteral("attention");
    bool agentAlertSoundEnabled = false;
};
