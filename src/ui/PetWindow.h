#pragma once

#include "../character/CharacterPackage.h"

#include <QLabel>
#include <QPoint>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QMenu;

class PetWindow : public QWidget
{
public:
    explicit PetWindow(QWidget *parent = nullptr);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void loadInitialImage();
    void addStateTestActions(QMenu *menu);
    void playState(const QString &stateName, bool centerWindow, bool autoReturn = false);
    void advanceStateFrame();
    void scheduleIdleBehavior();
    void playIdleBehavior();
    void returnToIdleFromBehavior();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setAlwaysOnTop(bool enabled);

    QLabel *m_imageLabel;
    QTimer m_animationTimer;
    QTimer m_idleBehaviorTimer;
    QTimer m_behaviorReturnTimer;
    CharacterPackage m_characterPackage;
    QString m_currentStateName;
    QStringList m_currentFrames;
    QPoint m_dragOffset;
    int m_currentFrameIndex;
    bool m_dragging;
    bool m_alwaysOnTop;
};
