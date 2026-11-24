#pragma once

#include "../ai/LLMTypes.h"

#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QVector>

class QEvent;
class QTextBrowser;

class ChatHistoryPanel : public QDialog
{
public:
    explicit ChatHistoryPanel(QWidget *parent = nullptr);
    ~ChatHistoryPanel() override;

    void setMessages(const QVector<ChatMessage> &messages, int prunedMessageCount = 0);
    void showAt(const QPoint &anchorPosition);
    void showNear(const QRect &avoidRect);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool isOwnObject(QObject *object) const;
    QRect availableGeometryForPoint(const QPoint &point) const;
    QRect availableGeometryForRect(const QRect &rect) const;
    QPoint constrainedTopLeft(const QPoint &preferredTopLeft, const QRect &availableGeometry) const;
    QPoint smartTopLeftNear(const QRect &avoidRect) const;
    void updateContent();

    QTextBrowser *m_textBrowser;
    QVector<ChatMessage> m_messages;
    int m_prunedMessageCount = 0;
};
