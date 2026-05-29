#pragma once

#include <QTextEdit>
#include <QTimer>
#include <QWidget>

class ChatBubble : public QWidget
{
public:
    explicit ChatBubble(QWidget *parent = nullptr);

    void showMessage(const QString &message, const QPoint &anchorPosition, int durationMs = 10000);
    void updateAnchorPosition(const QPoint &anchorPosition);

private:
    QSize preferredBubbleSize(const QString &message) const;
    void updatePosition();

    QTextEdit *m_textEdit = nullptr;
    QTimer m_hideTimer;
    QPoint m_anchorPosition;
};
