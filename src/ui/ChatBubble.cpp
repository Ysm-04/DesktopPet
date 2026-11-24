#include "ChatBubble.h"

#include "MarkdownViewHelper.h"

#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QRect>
#include <QScrollBar>
#include <QSize>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QUrl>
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

void configureDocument(
    QTextDocument *document,
    const QTextBrowser *browser,
    const QString &message,
    bool renderMarkdown)
{
    if (document == nullptr || browser == nullptr)
    {
        return;
    }

    document->setDefaultTextOption(browser->document()->defaultTextOption());
    document->setDocumentMargin(browser->document()->documentMargin());
    if (renderMarkdown)
    {
        MarkdownViewHelper::applyMarkdownToDocument(document, message, browser->font(), true);
        return;
    }

    document->setDefaultStyleSheet(QString());
    document->setDefaultFont(browser->font());
    document->setPlainText(message);
}

QSizeF documentSizeForWidth(const QTextBrowser *browser, const QString &message, int width, bool renderMarkdown)
{
    QTextDocument document;
    configureDocument(&document, browser, message, renderMarkdown);
    document.setTextWidth(width);
    return document.size();
}
}

ChatBubble::ChatBubble(QWidget *parent)
    : QWidget(parent)
    , m_textBrowser(new QTextBrowser(this))
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_textBrowser->setReadOnly(true);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_textBrowser->document()->setDocumentMargin(2.0);
    m_textBrowser->installEventFilter(this);
    m_textBrowser->viewport()->installEventFilter(this);
    m_textBrowser->verticalScrollBar()->installEventFilter(this);
    m_textBrowser->setStyleSheet(QStringLiteral(
        "QTextBrowser {"
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
    layout->addWidget(m_textBrowser);

    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        resetAutoHideTimer();
        MarkdownViewHelper::openExternalUrlIfSafe(url);
    });

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, [this]() {
        hide();
    });
}

void ChatBubble::showMessage(
    const QString &message,
    const QPoint &anchorPosition,
    int durationMs,
    bool scrollToBottom,
    bool renderMarkdown)
{
    m_anchorPosition = anchorPosition;
    m_autoHideDurationMs = durationMs;
    const QString trimmed = message.trimmed();
    m_textBrowser->clear();
    m_textBrowser->setCurrentCharFormat(QTextCharFormat());
    m_textBrowser->document()->setDefaultStyleSheet(QString());
    m_textBrowser->document()->setDefaultFont(m_textBrowser->font());
    m_textBrowser->viewport()->setCursor(Qt::IBeamCursor);
    if (renderMarkdown)
    {
        MarkdownViewHelper::applyMarkdownToDocument(m_textBrowser->document(), trimmed, m_textBrowser->font(), true);
    }
    else
    {
        m_textBrowser->document()->setPlainText(trimmed);
    }

    const QSize bubbleSize = preferredBubbleSize(trimmed, renderMarkdown);
    const int documentWidth = bubbleSize.width() - BubblePaddingWidth;
    const QSizeF documentSize = documentSizeForWidth(m_textBrowser, trimmed, documentWidth, renderMarkdown);
    const bool needsVerticalScroll = documentSize.height() + BubblePaddingHeight > MaxBubbleHeight;
    m_textBrowser->setVerticalScrollBarPolicy(needsVerticalScroll ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    m_textBrowser->setFixedSize(bubbleSize);
    setFixedSize(bubbleSize);
    updatePosition();
    show();
    raise();

    m_textBrowser->verticalScrollBar()->setValue(
        scrollToBottom ? m_textBrowser->verticalScrollBar()->maximum() : 0);

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
    if ((watched == m_textBrowser
            || watched == m_textBrowser->viewport()
            || watched == m_textBrowser->verticalScrollBar())
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

QSize ChatBubble::preferredBubbleSize(const QString &message, bool renderMarkdown) const
{
    QTextDocument singleLineDocument;
    configureDocument(&singleLineDocument, m_textBrowser, message, renderMarkdown);
    singleLineDocument.setTextWidth(-1);

    const int singleLineWidth = qCeil(singleLineDocument.idealWidth())
        + BubblePaddingWidth
        + BubbleWidthSafetyMargin;
    int preferredWidth = qBound(MinBubbleWidth, singleLineWidth, MaxBubbleWidth);

    QSizeF documentSize = documentSizeForWidth(m_textBrowser, message, preferredWidth - BubblePaddingWidth, renderMarkdown);
    int preferredHeight = qCeil(documentSize.height()) + BubblePaddingHeight;
    if (preferredHeight > MaxBubbleHeight)
    {
        const int scrollbarWidth = m_textBrowser->verticalScrollBar()->sizeHint().width();
        preferredWidth = qMin(MaxBubbleWidth, preferredWidth + scrollbarWidth);
        documentSize = documentSizeForWidth(
            m_textBrowser,
            message,
            preferredWidth - BubblePaddingWidth - scrollbarWidth,
            renderMarkdown);
        preferredHeight = qCeil(documentSize.height()) + BubblePaddingHeight;
    }

    return QSize(preferredWidth, qMin(preferredHeight, MaxBubbleHeight));
}

void ChatBubble::updatePosition()
{
    move(m_anchorPosition.x() - width() / 2, m_anchorPosition.y() - height());
}
