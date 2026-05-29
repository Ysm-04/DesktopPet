#include "ChatBubble.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QRect>
#include <QScrollBar>
#include <QSize>
#include <QVBoxLayout>
#include <QtGlobal>

namespace
{
constexpr int MinBubbleWidth = 120;
constexpr int MaxBubbleWidth = 420;
constexpr int MaxBubbleHeight = 220;
constexpr int BubbleOffsetY = 8;
constexpr int BubblePaddingWidth = 28;
constexpr int BubblePaddingHeight = 24;
}

ChatBubble::ChatBubble(QWidget *parent)
    : QWidget(parent)
    , m_textEdit(new QTextEdit(this))
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_textEdit->setReadOnly(true);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textEdit->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_textEdit->installEventFilter(this);
    m_textEdit->viewport()->installEventFilter(this);
    m_textEdit->verticalScrollBar()->installEventFilter(this);
    qApp->installEventFilter(this);
    m_textEdit->setStyleSheet(QStringLiteral(
        "QTextEdit {"
        "background: rgba(255, 255, 255, 232);"
        "color: #202124;"
        "border: 1px solid rgba(32, 33, 36, 48);"
        "border-radius: 8px;"
        "padding: 10px 12px;"
        "}"
        "QScrollBar:vertical {"
        "background: transparent;"
        "width: 8px;"
        "margin: 8px 4px 8px 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "background: rgba(32, 33, 36, 80);"
        "border-radius: 4px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "height: 0;"
        "}"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_textEdit);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        hide();
    });
}

void ChatBubble::showMessage(const QString &message, const QPoint &anchorPosition, int durationMs)
{
    m_dismissOnExternalInteraction = false;
    m_anchorPosition = anchorPosition;
    m_autoHideDurationMs = durationMs;
    const QString trimmed = message.trimmed();
    m_textEdit->setPlainText(trimmed);
    m_textEdit->verticalScrollBar()->setValue(0);

    const QSize bubbleSize = preferredBubbleSize(trimmed);
    m_textEdit->setFixedSize(bubbleSize);
    setFixedSize(bubbleSize);
    updatePosition();
    show();

    if (durationMs > 0)
    {
        m_hideTimer.start(durationMs);
    }
}

void ChatBubble::updateAnchorPosition(const QPoint &anchorPosition)
{
    m_anchorPosition = anchorPosition;
    if (isVisible())
    {
        updatePosition();
    }
}

void ChatBubble::setDismissOnExternalInteraction(bool enabled)
{
    m_dismissOnExternalInteraction = enabled;
}

void ChatBubble::resetAutoHideTimer()
{
    if (isVisible() && m_autoHideDurationMs > 0)
    {
        m_hideTimer.start(m_autoHideDurationMs);
    }
}

void ChatBubble::hideBubble()
{
    m_hideTimer.stop();
    hide();
}

bool ChatBubble::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && m_dismissOnExternalInteraction && event->type() == QEvent::ApplicationDeactivate)
    {
        hideBubble();
        return false;
    }

    if (m_dismissOnExternalInteraction && event->type() == QEvent::MouseButtonPress && !isOwnObject(watched))
    {
        hideBubble();
        return false;
    }

    if ((watched == m_textEdit || watched == m_textEdit->viewport() || watched == m_textEdit->verticalScrollBar())
        && isUserInteractionEvent(event))
    {
        resetAutoHideTimer();
    }

    return QWidget::eventFilter(watched, event);
}

bool ChatBubble::isOwnObject(QObject *object) const
{
    while (object != nullptr)
    {
        if (object == this || object == m_textEdit || object == m_textEdit->viewport() || object == m_textEdit->verticalScrollBar())
        {
            return true;
        }

        object = object->parent();
    }

    return false;
}

bool ChatBubble::isUserInteractionEvent(QEvent *event) const
{
    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyPress:
        return true;
    default:
        return false;
    }
}

QSize ChatBubble::preferredBubbleSize(const QString &message) const
{
    const QFontMetrics metrics(m_textEdit->font());
    const int textWidth = metrics.horizontalAdvance(message);
    const int preferredWidth = qBound(MinBubbleWidth, textWidth + BubblePaddingWidth, MaxBubbleWidth);

    const QRect wrappedRect = metrics.boundingRect(
        QRect(0, 0, preferredWidth - BubblePaddingWidth, 10000),
        Qt::TextWordWrap,
        message);

    const int preferredHeight = qMin(wrappedRect.height() + BubblePaddingHeight, MaxBubbleHeight);
    return QSize(preferredWidth, preferredHeight);
}

void ChatBubble::updatePosition()
{
    move(m_anchorPosition.x() - width() / 2, m_anchorPosition.y() - height() - BubbleOffsetY);
}
