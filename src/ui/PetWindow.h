#pragma once

#include "../character/AnimationClip.h"
#include "../character/CharacterPackage.h"
#include "../character/FrameAnimator.h"
#include "../config/AppConfig.h"
#include "../state/PetStateMachine.h"

#include <QMap>
#include <QPoint>
#include <QTimer>
#include <QWidget>

#include <memory>

class QMenu;
class QMoveEvent;
class QPixmap;
class ChatBubble;
class PetView;

class PetWindow : public QWidget
{
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow();

    void applyAppConfig(const AppConfig &config);
    AppConfig currentAppConfig() const;
    void pauseAnimation();
    void resumeAnimation();
    void showBubbleMessage(const QString &message);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void loadInitialImage();
    void buildAnimationClips();
    void addStateTestActions(QMenu *menu);
    void updateBubblePosition();
    QPoint bubbleAnchorPosition() const;
    void playState(const QString &stateName, bool centerWindow);
    void playResolvedState(const QString &stateName, bool centerWindow);
    void scheduleIdleBehavior();
    void playIdleBehavior();
    void returnToIdleFromBehavior();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setDisplayPixmap(const QPixmap &pixmap, bool centerWindow);
    bool isPointVisibleOnScreen(const QPoint &point) const;
    void setAlwaysOnTop(bool enabled);

    std::unique_ptr<ChatBubble> m_chatBubble;
    PetView *m_petView;
    QTimer m_idleBehaviorTimer;
    QTimer m_behaviorReturnTimer;
    CharacterPackage m_characterPackage;
    QMap<QString, AnimationClip> m_clips;
    FrameAnimator m_frameAnimator;
    PetStateMachine m_stateMachine;
    QPoint m_dragOffset;
    bool m_dragging;
    bool m_alwaysOnTop;
    bool m_centerNextFrame;
    bool m_returnToIdleAfterResume = false;
};
