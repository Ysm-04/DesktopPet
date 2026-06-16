#pragma once

#include "../character/AnimationClip.h"
#include "../character/CharacterPackage.h"
#include "../character/FrameAnimator.h"
#include "../config/AppConfig.h"
#include "../reminder/ReminderTypes.h"
#include "../web/WebChatTypes.h"
#include "../state/PetStateMachine.h"

#include <QMap>
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include <functional>
#include <memory>

class QMenu;
class QHideEvent;
class QMoveEvent;
class QPixmap;
class QShowEvent;
class ChatBubble;
class ChatHistoryPanel;
class ChatInputDialog;
class ConversationManager;
class ConversationStore;
class FileOperationManager;
class AppLaunchManager;
class NotificationDispatcher;
class PetView;
class ReminderManager;
class ReminderSoundPlayer;
class WebChatManager;
class WeatherManager;

class PetWindow : public QWidget
{
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow();

    void applyAppConfig(const AppConfig &config);
    AppConfig currentAppConfig() const;
    void openSettingsDialog();
    void activateFromExternalInstance();
    void setSettingsFallbackInContextMenuEnabled(bool enabled);
    void setTrayNotificationCallback(std::function<bool(const QString &, const QString &)> callback);
    void pauseAnimation();
    void resumeAnimation();
    void showBubbleMessage(const QString &message);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void loadInitialImage();
    bool loadCharacterPackage(const QString &characterId, bool centerWindow);
    void buildAnimationClips();
    void addStateTestActions(QMenu *menu);
    void startChat();
    bool submitChatMessage(const QString &message, bool webEnabled);
    bool submitAiChatMessage(const QString &message);
    bool submitWebChatMessage(const QString &message);
    bool handleReminderChatMessage(const QString &message);
    bool handleWeatherChatMessage(const QString &message);
    bool handleFileOperationChatMessage(const QString &message);
    bool handleLaunchAppChatMessage(const QString &message);
    bool hasActiveWebRequest() const;
    QString formatWebChatResponseForDisplay(const WebChatResponse &response) const;
    void refreshChatInputWebToggle();
    void saveWebTogglePreference(bool webEnabled);
    void handleTriggeredReminder(const ReminderItem &item);
    void playReminderSound();
    void showReminderNotification(const ReminderItem &item);
    bool shouldNotifyOnlyForReminder() const;
    void enqueueVisibleTriggeredReminder(const ReminderItem &item);
    void showNextTriggeredReminder();
    void finishActiveTriggeredReminder(bool hideBubble);
    void showTriggeredReminder(const ReminderItem &item);
    void ensureReminderActionPanel();
    void showReminderActions(const ReminderItem &item);
    void hideReminderActions();
    void updateReminderActionPosition();
    void snoozeTriggeredReminder(const ReminderItem &item);
    void clearConversation();
    void cancelActiveAIRequest();
    void showConversationHistory();
    void refreshChatHistoryPanel();
    void configureConversation(bool loadPersistedHistory);
    void loadConversationHistoryIfNeeded();
    void saveConversationHistoryIfNeeded();
    void handleChatStreamDelta(const QString &delta);
    void flushStreamingBubble(bool finalUpdate);
    void finishStreamingChat();
    void cancelStreamingChat();
    bool hasActiveAIRequest() const;
    bool isManualStateSwitchLocked() const;
    void resetBubbleAutoHideTimer();
    QPoint chatInputAnchorPosition() const;
    void updateBubblePosition();
    QPoint bubbleAnchorPosition() const;
    void playState(const QString &stateName, bool centerWindow);
    void playResolvedState(const QString &stateName, bool centerWindow);
    QSize animationTargetSize() const;
    int effectiveAnimationFps(int fps) const;
    bool isLowPowerMode() const;
    bool isAnimationCacheManagementEnabled() const;
    void rebuildAnimationPrewarmQueue();
    void scheduleAnimationPrewarm();
    void stopAnimationPrewarm();
    void processAnimationPrewarm();
    void noteAnimationClipAccess(const QString &stateName);
    qint64 animationCacheLimitBytes() const;
    qint64 loadedAnimationCacheBytes() const;
    int loadedAnimationClipCount() const;
    QSet<QString> protectedAnimationStates() const;
    void trimAnimationCache(const QString &reason);
    void unloadNonProtectedAnimationCache(const QString &reason);
    void scheduleIdleBehavior();
    void playIdleBehavior();
    void returnToIdleFromBehavior();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setDisplayPixmap(const QPixmap &pixmap, bool centerWindow);
    bool isPointVisibleOnScreen(const QPoint &point) const;
    void setAlwaysOnTop(bool enabled);

    std::unique_ptr<ChatBubble> m_chatBubble;
    std::unique_ptr<ChatHistoryPanel> m_chatHistoryPanel;
    std::unique_ptr<ChatInputDialog> m_chatInputDialog;
    std::unique_ptr<ConversationManager> m_conversationManager;
    std::unique_ptr<ConversationStore> m_conversationStore;
    std::unique_ptr<FileOperationManager> m_fileOperationManager;
    std::unique_ptr<AppLaunchManager> m_appLaunchManager;
    std::unique_ptr<NotificationDispatcher> m_notificationDispatcher;
    std::unique_ptr<ReminderManager> m_reminderManager;
    std::unique_ptr<ReminderSoundPlayer> m_reminderSoundPlayer;
    std::unique_ptr<WebChatManager> m_webChatManager;
    std::unique_ptr<WeatherManager> m_weatherManager;
    std::unique_ptr<QWidget> m_reminderActionPanel;
    PetView *m_petView;
    QTimer m_idleBehaviorTimer;
    QTimer m_behaviorReturnTimer;
    QTimer m_streamBubbleUpdateTimer;
    QTimer m_animationPrewarmTimer;
    AppConfig m_appConfig;
    CharacterPackage m_characterPackage;
    QMap<QString, AnimationClip> m_clips;
    QMap<QString, qint64> m_clipLastAccessSerial;
    QSet<QString> m_animationPrewarmAttemptedStates;
    FrameAnimator m_frameAnimator;
    PetStateMachine m_stateMachine;
    QPoint m_dragOffset;
    QString m_streamingAssistantText;
    QStringList m_animationPrewarmQueue;
    QVector<ReminderItem> m_pendingVisibleTriggeredReminders;
    ReminderItem m_activeTriggeredReminder;
    qint64 m_clipAccessSerial = 0;
    bool m_dragging;
    bool m_alwaysOnTop;
    bool m_centerNextFrame;
    bool m_returnToIdleAfterResume = false;
    bool m_streamingChatActive = false;
    bool m_streamingTalkStarted = false;
    bool m_settingsFallbackInContextMenuEnabled = true;
    bool m_hasActiveTriggeredReminder = false;
};
