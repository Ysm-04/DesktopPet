#pragma once

#include "../ai/LLMTypes.h"

#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QVector>

class QEvent;
class QTextEdit;

class ChatHistoryPanel : public QDialog
{
public:
    explicit ChatHistoryPanel(QWidget *parent = nullptr);
    ~ChatHistoryPanel() override;

    void setMessages(const QVector<ChatMessage> &messages);
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

    QTextEdit *m_textEdit;
    QVector<ChatMessage> m_messages;
};
