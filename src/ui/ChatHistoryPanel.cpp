#include "ChatHistoryPanel.h"

#include "MarkdownViewHelper.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QtGlobal>

#include <limits>

namespace
{
constexpr int PanelWidth = 460;
constexpr int PanelHeight = 360;
constexpr int PanelGap = 16;
constexpr int ScreenMargin = 8;

int boundedCoordinate(int value, int minimum, int maximum)
{
    if (maximum < minimum)
    {
        return minimum;
    }

    return qBound(minimum, value, maximum);
}

qint64 overlapArea(const QRect &first, const QRect &second)
{
    const QRect intersection = first.intersected(second);
    if (intersection.isEmpty())
    {
        return 0;
    }

    return static_cast<qint64>(intersection.width()) * static_cast<qint64>(intersection.height());
}

qint64 squaredDistance(const QPoint &first, const QPoint &second)
{
    const qint64 dx = first.x() - second.x();
    const qint64 dy = first.y() - second.y();
    return dx * dx + dy * dy;
}

QString roleLabel(const QString &role)
{
    if (role == QStringLiteral("user"))
    {
        return QStringLiteral("你");
    }

    if (role == QStringLiteral("assistant"))
    {
        return QStringLiteral("桌宠");
    }

    return role;
}

QString roleColor(const QString &role)
{
    if (role == QStringLiteral("user"))
    {
        return QStringLiteral("#1769aa");
    }

    if (role == QStringLiteral("assistant"))
    {
        return QStringLiteral("#3f6b34");
    }

    return QStringLiteral("#5f6368");
}

QString htmlEscapedContent(QString content)
{
    content = content.trimmed().toHtmlEscaped();
    content.replace(QStringLiteral("\n"), QStringLiteral("<br>"));
    return content;
}

bool isDescendantOrSelf(const QObject *object, const QObject *ancestor)
{
    while (object != nullptr)
    {
        if (object == ancestor)
        {
            return true;
        }
        object = object->parent();
    }

    return false;
}
}

ChatHistoryPanel::ChatHistoryPanel(QWidget *parent)
    : QDialog(parent)
    , m_textBrowser(new QTextBrowser(this))
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(false);
    resize(PanelWidth, PanelHeight);

    m_textBrowser->setReadOnly(true);
    m_textBrowser->setOpenExternalLinks(false);
    m_textBrowser->setOpenLinks(false);
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    m_textBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_textBrowser->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textBrowser->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_textBrowser->document()->setDefaultStyleSheet(
        QStringLiteral("a { color: #175cd3; text-decoration: none; }"));
    m_textBrowser->installEventFilter(this);
    m_textBrowser->viewport()->installEventFilter(this);
    m_textBrowser->verticalScrollBar()->installEventFilter(this);
    if (qApp != nullptr)
    {
        qApp->installEventFilter(this);
    }

    setStyleSheet(QStringLiteral(
        "QDialog {"
        "background: transparent;"
        "}"
        "QTextBrowser {"
        "background: rgba(246, 249, 253, 232);"
        "color: #202124;"
        "border: 1px solid rgba(32, 33, 36, 46);"
        "border-radius: 12px;"
        "padding: 14px 16px;"
        "font-size: 14px;"
        "selection-background-color: rgba(66, 133, 244, 90);"
        "}"
        "QScrollBar:vertical {"
        "background: transparent;"
        "width: 9px;"
        "margin: 12px 5px 12px 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "background: rgba(32, 33, 36, 88);"
        "border-radius: 4px;"
        "min-height: 28px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "height: 0;"
        "}"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_textBrowser);

    connect(m_textBrowser, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        MarkdownViewHelper::openExternalUrlIfSafe(url);
    });
}

ChatHistoryPanel::~ChatHistoryPanel()
{
    if (qApp != nullptr)
    {
        qApp->removeEventFilter(this);
    }
}

void ChatHistoryPanel::setMessages(const QVector<ChatMessage> &messages, int prunedMessageCount)
{
    m_messages = messages;
    m_prunedMessageCount = qMax(0, prunedMessageCount);
    updateContent();
}

void ChatHistoryPanel::showAt(const QPoint &anchorPosition)
{
    move(constrainedTopLeft(anchorPosition, availableGeometryForPoint(anchorPosition)));
    show();
    raise();
    activateWindow();
    m_textBrowser->setFocus();
    m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
}

void ChatHistoryPanel::showNear(const QRect &avoidRect)
{
    move(smartTopLeftNear(avoidRect));
    show();
    raise();
    activateWindow();
    m_textBrowser->setFocus();
    m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
}

bool ChatHistoryPanel::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
    {
        const QWidget *activePopup = QApplication::activePopupWidget();
        if (activePopup != nullptr && isDescendantOrSelf(activePopup, this))
        {
            return QDialog::event(event);
        }

        hide();
        return true;
    }

    return QDialog::event(event);
}

bool ChatHistoryPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && isVisible() && event->type() == QEvent::ApplicationDeactivate)
    {
        hide();
        return false;
    }

    if (isVisible() && event->type() == QEvent::MouseButtonPress && !isOwnObject(watched))
    {
        hide();
        return false;
    }

    return QDialog::eventFilter(watched, event);
}

bool ChatHistoryPanel::isOwnObject(QObject *object) const
{
    if (object == nullptr)
    {
        return false;
    }

    if (windowHandle() != nullptr && object == windowHandle())
    {
        return true;
    }

    if (QWidget *activePopup = QApplication::activePopupWidget())
    {
        if (isDescendantOrSelf(activePopup, this))
        {
            if (activePopup->windowHandle() != nullptr && object == activePopup->windowHandle())
            {
                return true;
            }
            if (isDescendantOrSelf(object, activePopup))
            {
                return true;
            }
        }
    }

    if (isDescendantOrSelf(object, this))
    {
        return true;
    }

    if (m_textBrowser == nullptr)
    {
        return false;
    }

    return isDescendantOrSelf(object, m_textBrowser)
        || isDescendantOrSelf(object, m_textBrowser->viewport())
        || isDescendantOrSelf(object, m_textBrowser->verticalScrollBar());
}

QRect ChatHistoryPanel::availableGeometryForPoint(const QPoint &point) const
{
    const QScreen *screen = QGuiApplication::screenAt(point);
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen == nullptr)
    {
        return {};
    }

    return screen->availableGeometry();
}

QRect ChatHistoryPanel::availableGeometryForRect(const QRect &rect) const
{
    const QScreen *screen = QGuiApplication::screenAt(rect.center());
    if (screen == nullptr)
    {
        screen = QGuiApplication::screenAt(rect.topLeft());
    }
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen == nullptr)
    {
        return {};
    }

    return screen->availableGeometry();
}

QPoint ChatHistoryPanel::constrainedTopLeft(const QPoint &preferredTopLeft, const QRect &availableGeometry) const
{
    if (!availableGeometry.isValid())
    {
        return preferredTopLeft;
    }

    const int x = boundedCoordinate(
        preferredTopLeft.x(),
        availableGeometry.left() + ScreenMargin,
        availableGeometry.right() - width() - ScreenMargin);
    const int y = boundedCoordinate(
        preferredTopLeft.y(),
        availableGeometry.top() + ScreenMargin,
        availableGeometry.bottom() - height() - ScreenMargin);

    return QPoint(x, y);
}

QPoint ChatHistoryPanel::smartTopLeftNear(const QRect &avoidRect) const
{
    if (!avoidRect.isValid())
    {
        return constrainedTopLeft(pos(), availableGeometryForPoint(pos()));
    }

    const QRect availableGeometry = availableGeometryForRect(avoidRect);
    const QVector<QPoint> preferredTopLefts = {
        QPoint(avoidRect.right() + 1 + PanelGap, avoidRect.top()),
        QPoint(avoidRect.left() - PanelGap - width(), avoidRect.top()),
        QPoint(avoidRect.center().x() - width() / 2, avoidRect.top() - PanelGap - height()),
        QPoint(avoidRect.center().x() - width() / 2, avoidRect.bottom() + 1 + PanelGap),
    };

    QPoint bestTopLeft = constrainedTopLeft(preferredTopLefts.first(), availableGeometry);
    qint64 bestOverlap = std::numeric_limits<qint64>::max();
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    int bestIndex = preferredTopLefts.size();

    for (int index = 0; index < preferredTopLefts.size(); ++index)
    {
        const QPoint candidateTopLeft = constrainedTopLeft(preferredTopLefts.at(index), availableGeometry);
        const QRect candidateRect(candidateTopLeft, size());
        const qint64 candidateOverlap = overlapArea(candidateRect, avoidRect);
        const qint64 candidateDistance = squaredDistance(candidateTopLeft, preferredTopLefts.at(index));

        if (candidateOverlap < bestOverlap
            || (candidateOverlap == bestOverlap && candidateDistance < bestDistance)
            || (candidateOverlap == bestOverlap && candidateDistance == bestDistance && index < bestIndex))
        {
            bestTopLeft = candidateTopLeft;
            bestOverlap = candidateOverlap;
            bestDistance = candidateDistance;
            bestIndex = index;
        }
    }

    return bestTopLeft;
}

void ChatHistoryPanel::updateContent()
{
    QString html = QStringLiteral(
        "<html><body style=\"font-family:'Microsoft YaHei','Segoe UI',sans-serif; margin:0;\">");

    bool hasContent = false;
    if (m_prunedMessageCount > 0)
    {
        hasContent = true;
        html += QStringLiteral(
            "<div style=\"margin:0 0 14px 0; padding:8px 10px; border-radius:8px; background:#eef4ff; color:#475467; font-size:13px;\">"
            "较早 %1 条消息已按历史上限自动清理。"
            "</div>")
            .arg(m_prunedMessageCount);
    }

    for (const ChatMessage &message : m_messages)
    {
        const QString content = message.role == QStringLiteral("assistant")
            ? MarkdownViewHelper::markdownToHtmlFragment(message.content, m_textBrowser->font())
            : htmlEscapedContent(message.content);
        if (content.isEmpty())
        {
            continue;
        }

        hasContent = true;
        html += QStringLiteral(
            "<div style=\"margin:0 0 14px 0;\">"
            "<div style=\"font-size:12px; font-weight:600; color:%1; margin-bottom:5px;\">%2</div>"
            "<div style=\"font-size:14px; line-height:1.45; color:#202124;\">%3</div>"
            "</div>")
            .arg(roleColor(message.role), roleLabel(message.role).toHtmlEscaped(), content);
    }

    if (!hasContent)
    {
        html += QStringLiteral(
            "<div style=\"font-size:14px; line-height:1.45; color:#5f6368;\">暂无对话记录。</div>");
    }

    html += QStringLiteral("</body></html>");
    m_textBrowser->setCurrentCharFormat(QTextCharFormat());
    m_textBrowser->setHtml(html);
    m_textBrowser->verticalScrollBar()->setValue(m_textBrowser->verticalScrollBar()->maximum());
}
