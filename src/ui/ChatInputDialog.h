#pragma once

#include <QDialog>
#include <QPoint>
#include <QString>

#include <functional>

class QEvent;
class QCheckBox;
class QLabel;
class QMouseEvent;
class QPushButton;
class QTextEdit;

class ChatInputDialog : public QDialog
{
public:
    using SubmitCallback = std::function<bool(const QString &, bool)>;

    explicit ChatInputDialog(int maxLength, QWidget *parent = nullptr);

    QString message() const;
    bool webEnabled() const;
    void setWebEnabled(bool enabled);
    void setWebToggleAvailable(bool available, const QString &toolTip = QString());
    void setSubmitCallback(SubmitCallback callback);
    void showAt(const QPoint &anchorPosition);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool canSend() const;
    bool submitIfValid();
    void clearMessage();
    void updateCounter();
    void beginDrag(const QPoint &globalPosition);
    bool updateDrag(const QPoint &globalPosition);
    void endDrag();

    QTextEdit *m_textEdit = nullptr;
    QLabel *m_counterLabel = nullptr;
    QCheckBox *m_webToggleCheckBox = nullptr;
    QPushButton *m_sendButton = nullptr;
    SubmitCallback m_submitCallback;
    QPoint m_dragStartPosition;
    QPoint m_dragWindowPosition;
    int m_maxLength = 0;
    bool m_dragging = false;
    bool m_dragCandidate = false;
};
