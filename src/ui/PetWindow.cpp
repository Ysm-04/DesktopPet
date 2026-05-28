#include "PetWindow.h"

#include "../character/CharacterPackageLoader.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QScreen>
#include <QVBoxLayout>

namespace
{
QString characterPackagePath()
{
    return QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/shiroko");
}

QString previewImagePath()
{
    return QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/shiroko/preview.png");
}
}

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
    , m_imageLabel(new QLabel(this))
    , m_dragging(false)
    , m_alwaysOnTop(true)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);

    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setAttribute(Qt::WA_TranslucentBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_imageLabel);

    loadInitialImage();
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *topAction = menu.addAction(QStringLiteral("取消置顶"));
    topAction->setCheckable(true);
    topAction->setChecked(m_alwaysOnTop);

    menu.addSeparator();
    QAction *exitAction = menu.addAction(QStringLiteral("退出"));

    QAction *selectedAction = menu.exec(event->globalPos());
    if (selectedAction == topAction)
    {
        setAlwaysOnTop(!m_alwaysOnTop);
    }
    else if (selectedAction == exitAction)
    {
        close();
    }
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PetWindow::loadInitialImage()
{
    QString loadError;
    const CharacterPackage package = CharacterPackageLoader::load(characterPackagePath(), &loadError);
    const CharacterState *idleState = package.state(QStringLiteral("idle"));
    if (idleState != nullptr && !idleState->framePaths.isEmpty())
    {
        setDisplayImage(idleState->framePaths.first());
        return;
    }

    setDisplayImage(previewImagePath());
}

void PetWindow::setDisplayImage(const QString &imagePath)
{
    QPixmap pixmap(imagePath);
    if (pixmap.isNull())
    {
        m_imageLabel->setText(QStringLiteral("QtDesktopPet"));
        resize(240, 160);
        return;
    }

    const QSize targetSize(320, 320);
    const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
    resize(scaled.size());

    if (const QScreen *screen = QGuiApplication::primaryScreen())
    {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }
}

void PetWindow::setAlwaysOnTop(bool enabled)
{
    m_alwaysOnTop = enabled;

    Qt::WindowFlags flags = windowFlags();
    if (enabled)
    {
        flags |= Qt::WindowStaysOnTopHint;
    }
    else
    {
        flags &= ~Qt::WindowStaysOnTopHint;
    }

    setWindowFlags(flags);
    show();
}
