#pragma once

#include <QLabel>
#include <QPoint>
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
    void loadPreviewImage();
    void setAlwaysOnTop(bool enabled);

    QLabel *m_imageLabel;
    QPoint m_dragOffset;
    bool m_dragging;
    bool m_alwaysOnTop;
};
