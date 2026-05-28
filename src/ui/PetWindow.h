#pragma once

#include "../character/CharacterPackage.h"

#include <QLabel>
#include <QPoint>
#include <QStringList>
#include <QTimer>
#include <QWidget>

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
    void playState(const QString &stateName, bool centerWindow);
    void advanceStateFrame();
    void setDisplayImage(const QString &imagePath, bool centerWindow);
    void setAlwaysOnTop(bool enabled);

    QLabel *m_imageLabel;
    QTimer m_animationTimer;
    CharacterPackage m_characterPackage;
    QString m_currentStateName;
    QStringList m_currentFrames;
    QPoint m_dragOffset;
    int m_currentFrameIndex;
    bool m_dragging;
    bool m_alwaysOnTop;
};
