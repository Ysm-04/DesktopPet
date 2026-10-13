#include "ChatBubble.h"

#include <QEvent>
#include <QFrame>
#include <QRect>
#include <QScrollBar>
#include <QSize>
#include <QTextEdit>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtGlobal>

namespace
{
constexpr int MinBubbleWidth = 120;
constexpr int MaxBubbleWidth = 420;
constexpr int MaxBubbleHeight = 220;
constexpr int BubblePaddingWidth = 28;
constexpr int BubblePaddingHeight = 24;
constexpr int BubbleWidthSafetyMargin = 12;

QSizeF documentSizeForWidth(const QTextEdit *textEdit, const QString &message, int width)
{
    QTextDocument document;
    document.setDefaultFont(textEdit->font());
    document.setDefaultTextOption(textEdit->document()->defaultTextOption());
    document.setPlainText(message);
    document.setDocumentMargin(textEdit->document()->documentMargin());
    document.setTextWidth(width);
    return document.size();
}
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

void ChatBubble::showMessage(const QString &message, const QPoint &anchorPosition, int durationMs, bool scrollToBottom)
{
    m_anchorPosition = anchorPosition;
    m_autoHideDurationMs = durationMs;
    const QString trimmed = message.trimmed();
    m_textEdit->setPlainText(trimmed);

    const QSize bubbleSize = preferredBubbleSize(trimmed);
    const int documentWidth = bubbleSize.width() - BubblePaddingWidth;
    const QSizeF documentSize = documentSizeForWidth(m_textEdit, trimmed, documentWidth);
    const bool needsVerticalScroll = documentSize.height() + BubblePaddingHeight > MaxBubbleHeight;
    m_textEdit->setVerticalScrollBarPolicy(needsVerticalScroll ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    m_textEdit->setFixedSize(bubbleSize);
    setFixedSize(bubbleSize);
    updatePosition();
    show();
    raise();

    m_textEdit->verticalScrollBar()->setValue(
        scrollToBottom ? m_textEdit->verticalScrollBar()->maximum() : 0);

    if (durationMs > 0)
    {
        m_hideTimer.start(durationMs);
    }
    else
    {
        m_hideTimer.stop();
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
    if ((watched == m_textEdit || watched == m_textEdit->viewport() || watched == m_textEdit->verticalScrollBar())
        && isUserInteractionEvent(event))
    {
        resetAutoHideTimer();
    }

    return QWidget::eventFilter(watched, event);
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
    QTextDocument singleLineDocument;
    singleLineDocument.setDefaultFont(m_textEdit->font());
    singleLineDocument.setDefaultTextOption(m_textEdit->document()->defaultTextOption());
    singleLineDocument.setPlainText(message);
    singleLineDocument.setDocumentMargin(m_textEdit->document()->documentMargin());
    singleLineDocument.setTextWidth(-1);

    const int singleLineWidth = qCeil(singleLineDocument.idealWidth())
        + BubblePaddingWidth
        + BubbleWidthSafetyMargin;
    int preferredWidth = qBound(MinBubbleWidth, singleLineWidth, MaxBubbleWidth);

    QSizeF documentSize = documentSizeForWidth(m_textEdit, message, preferredWidth - BubblePaddingWidth);
    int preferredHeight = qCeil(documentSize.height()) + BubblePaddingHeight;
    if (preferredHeight > MaxBubbleHeight)
    {
        const int scrollbarWidth = m_textEdit->verticalScrollBar()->sizeHint().width();
        preferredWidth = qMin(MaxBubbleWidth, preferredWidth + scrollbarWidth);
        documentSize = documentSizeForWidth(m_textEdit, message, preferredWidth - BubblePaddingWidth - scrollbarWidth);
        preferredHeight = qCeil(documentSize.height()) + BubblePaddingHeight;
    }

    return QSize(preferredWidth, qMin(preferredHeight, MaxBubbleHeight));
}

void ChatBubble::updatePosition()
{
    move(m_anchorPosition.x() - width() / 2, m_anchorPosition.y() - height());
}
