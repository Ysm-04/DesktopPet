#pragma once

#include <QTextEdit>
#include <QTimer>
#include <QWidget>

class QEvent;

class ChatBubble : public QWidget
{
public:
    explicit ChatBubble(QWidget *parent = nullptr);

    void showMessage(
        const QString &message,
        const QPoint &anchorPosition,
        int durationMs = 10000,
        bool scrollToBottom = false);
    void updateAnchorPosition(const QPoint &anchorPosition);
    void setDismissOnExternalInteraction(bool enabled);
    void resetAutoHideTimer();
    void hideBubble();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool isOwnObject(QObject *object) const;
    bool isUserInteractionEvent(QEvent *event) const;
    QSize preferredBubbleSize(const QString &message) const;
    void updatePosition();

    QTextEdit *m_textEdit = nullptr;
    QTimer m_hideTimer;
    QPoint m_anchorPosition;
    int m_autoHideDurationMs = 0;
    bool m_dismissOnExternalInteraction = false;
};
