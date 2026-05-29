#include "ChatInputDialog.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextEdit>

#include <utility>

ChatInputDialog::ChatInputDialog(int maxLength, QWidget *parent)
    : QDialog(parent)
    , m_textEdit(new QTextEdit(this))
    , m_counterLabel(new QLabel(this))
    , m_sendButton(new QPushButton(QStringLiteral("↗"), this))
    , m_maxLength(maxLength)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(false);
    resize(560, 76);
    setStyleSheet(QStringLiteral(
        "QDialog {"
        "background: transparent;"
        "}"
        "QFrame#InputPanel {"
        "background: rgba(246, 249, 253, 218);"
        "border: 1px solid rgba(32, 33, 36, 38);"
        "border-radius: 12px;"
        "}"
        "QTextEdit {"
        "background: transparent;"
        "color: #202124;"
        "border: none;"
        "padding: 8px 10px;"
        "selection-background-color: rgba(66, 133, 244, 90);"
        "font-size: 20px;"
        "}"
        "QPushButton {"
        "min-width: 34px;"
        "max-width: 34px;"
        "min-height: 34px;"
        "max-height: 34px;"
        "border: 1px solid rgba(32, 33, 36, 45);"
        "border-radius: 17px;"
        "background: rgba(255, 255, 255, 236);"
        "color: rgba(32, 33, 36, 190);"
        "font-size: 18px;"
        "font-weight: 600;"
        "padding: 0;"
        "}"
        "QPushButton:hover {"
        "background: rgba(255, 255, 255, 255);"
        "}"
        "QPushButton:disabled {"
        "color: rgba(32, 33, 36, 80);"
        "background: rgba(255, 255, 255, 170);"
        "}"));

    m_textEdit->setPlaceholderText(QStringLiteral("输入消息，Enter 发送，Shift+Enter 换行"));
    m_textEdit->setLineWrapMode(QTextEdit::WidgetWidth);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setFixedHeight(56);
    m_textEdit->installEventFilter(this);
    m_textEdit->viewport()->installEventFilter(this);

    m_counterLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_counterLabel->setStyleSheet(QStringLiteral("color: #b3261e; font-size: 12px;"));
    m_counterLabel->setVisible(false);

    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        submitIfValid();
    });
    connect(m_textEdit, &QTextEdit::textChanged, this, [this]() {
        updateCounter();
    });

    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("InputPanel"));
    panel->installEventFilter(this);

    auto *panelLayout = new QHBoxLayout(panel);
    panelLayout->setContentsMargins(16, 6, 8, 6);
    panelLayout->setSpacing(8);
    panelLayout->addWidget(m_textEdit, 1);
    panelLayout->addWidget(m_counterLabel);
    panelLayout->addWidget(m_sendButton);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(panel);

    updateCounter();
    m_textEdit->setFocus();
}

QString ChatInputDialog::message() const
{
    return m_textEdit->toPlainText().trimmed();
}

void ChatInputDialog::setSubmitCallback(SubmitCallback callback)
{
    m_submitCallback = std::move(callback);
}

void ChatInputDialog::showAt(const QPoint &anchorPosition)
{
    move(anchorPosition.x() - width() / 2, anchorPosition.y() - height() / 2);
    show();
    raise();
    activateWindow();
    m_textEdit->setFocus();
}

bool ChatInputDialog::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate)
    {
        hide();
        return true;
    }

    return QDialog::event(event);
}

bool ChatInputDialog::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_textEdit || watched == m_textEdit->viewport()) && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            if (keyEvent->modifiers() & Qt::ShiftModifier)
            {
                return false;
            }

            submitIfValid();
            return true;
        }
    }

    if ((watched == m_textEdit->viewport() || watched == m_textEdit->parentWidget())
        && event->type() == QEvent::MouseButtonPress)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton)
        {
            beginDrag(mouseEvent->globalPosition().toPoint());
            return false;
        }
    }

    if ((watched == m_textEdit->viewport() || watched == m_textEdit->parentWidget())
        && event->type() == QEvent::MouseMove)
    {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->buttons() & Qt::LeftButton)
        {
            return updateDrag(mouseEvent->globalPosition().toPoint());
        }
    }

    if ((watched == m_textEdit->viewport() || watched == m_textEdit->parentWidget())
        && event->type() == QEvent::MouseButtonRelease)
    {
        endDrag();
    }

    return QDialog::eventFilter(watched, event);
}

void ChatInputDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        beginDrag(event->globalPosition().toPoint());
    }

    QDialog::mousePressEvent(event);
}

void ChatInputDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && updateDrag(event->globalPosition().toPoint()))
    {
        event->accept();
        return;
    }

    QDialog::mouseMoveEvent(event);
}

void ChatInputDialog::mouseReleaseEvent(QMouseEvent *event)
{
    endDrag();
    QDialog::mouseReleaseEvent(event);
}

bool ChatInputDialog::canSend() const
{
    const int length = message().size();
    return length > 0 && (m_maxLength <= 0 || length <= m_maxLength);
}

bool ChatInputDialog::submitIfValid()
{
    if (!canSend())
    {
        return false;
    }

    const QString submittedMessage = message();
    const bool accepted = m_submitCallback ? m_submitCallback(submittedMessage) : true;
    if (accepted)
    {
        clearMessage();
        show();
        raise();
        activateWindow();
        m_textEdit->setFocus();
    }

    return accepted;
}

void ChatInputDialog::clearMessage()
{
    m_textEdit->clear();
}

void ChatInputDialog::updateCounter()
{
    const int length = message().size();
    m_sendButton->setEnabled(canSend());

    if (m_maxLength > 0)
    {
        m_counterLabel->setVisible(length > m_maxLength);
        if (length > m_maxLength)
        {
            m_counterLabel->setText(QString::number(length) + QStringLiteral("/") + QString::number(m_maxLength));
        }
        return;
    }

    m_counterLabel->setVisible(false);
}

void ChatInputDialog::beginDrag(const QPoint &globalPosition)
{
    m_dragCandidate = true;
    m_dragging = false;
    m_dragStartPosition = globalPosition;
    m_dragWindowPosition = pos();
}

bool ChatInputDialog::updateDrag(const QPoint &globalPosition)
{
    if (!m_dragCandidate)
    {
        return false;
    }

    const QPoint delta = globalPosition - m_dragStartPosition;
    if (!m_dragging && delta.manhattanLength() < QApplication::startDragDistance())
    {
        return false;
    }

    m_dragging = true;
    move(m_dragWindowPosition + delta);
    return true;
}

void ChatInputDialog::endDrag()
{
    m_dragCandidate = false;
    m_dragging = false;
}
