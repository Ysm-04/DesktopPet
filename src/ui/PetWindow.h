#pragma once

#include <QLabel>
#include <QPoint>
#include <QStringList>
#include <QTimer>
#include <QWidget>

struct CharacterState;

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
    void startIdleAnimation(const CharacterState &idleState);
    void advanceIdleFrame();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setAlwaysOnTop(bool enabled);

    QLabel *m_imageLabel;
    QTimer m_animationTimer;
    QStringList m_idleFrames;
    QPoint m_dragOffset;
    int m_currentFrameIndex;
    bool m_dragging;
    bool m_alwaysOnTop;
};
