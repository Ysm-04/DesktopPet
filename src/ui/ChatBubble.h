#pragma once

#include <QTimer>
#include <QWidget>

class QEvent;
class QTextBrowser;

class ChatBubble : public QWidget
{
public:
    explicit ChatBubble(QWidget *parent = nullptr);

    void showMessage(
        const QString &message,
        const QPoint &anchorPosition,
        int durationMs = 10000,
        bool scrollToBottom = false,
        bool renderMarkdown = false);
    void updateAnchorPosition(const QPoint &anchorPosition);
    void resetAutoHideTimer();
    void hideBubble();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool isUserInteractionEvent(QEvent *event) const;
    QSize preferredBubbleSize(const QString &message, bool renderMarkdown) const;
    void updatePosition();

    QTextBrowser *m_textBrowser = nullptr;
    QTimer m_hideTimer;
    QPoint m_anchorPosition;
    int m_autoHideDurationMs = 0;
};
