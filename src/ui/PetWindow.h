#pragma once

#include "../character/AnimationClip.h"
#include "../character/CharacterPackage.h"
#include "../character/FrameAnimator.h"
#include "../config/AppConfig.h"
#include "../state/PetStateMachine.h"

#include <QMap>
#include <QPoint>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

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
class PetView;

class PetWindow : public QWidget
{
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow();

    void applyAppConfig(const AppConfig &config);
    AppConfig currentAppConfig() const;
    void openSettingsDialog();
    void setSettingsFallbackInContextMenuEnabled(bool enabled);
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
    bool submitChatMessage(const QString &message);
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
    qint64 m_clipAccessSerial = 0;
    bool m_dragging;
    bool m_alwaysOnTop;
    bool m_centerNextFrame;
    bool m_returnToIdleAfterResume = false;
    bool m_streamingChatActive = false;
    bool m_streamingTalkStarted = false;
    bool m_settingsFallbackInContextMenuEnabled = true;
};
