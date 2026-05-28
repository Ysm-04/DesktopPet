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

class QMenu;
class QPixmap;
class PetView;

class PetWindow : public QWidget
{
public:
    explicit PetWindow(QWidget *parent = nullptr);

    void applyAppConfig(const AppConfig &config);
    AppConfig currentAppConfig() const;
    void pauseAnimation();
    void resumeAnimation();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void loadInitialImage();
    void buildAnimationClips();
    void addStateTestActions(QMenu *menu);
    void playState(const QString &stateName, bool centerWindow);
    void playResolvedState(const QString &stateName, bool centerWindow);
    void scheduleIdleBehavior();
    void playIdleBehavior();
    void returnToIdleFromBehavior();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setDisplayPixmap(const QPixmap &pixmap, bool centerWindow);
    bool isPointVisibleOnScreen(const QPoint &point) const;
    void setAlwaysOnTop(bool enabled);

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
