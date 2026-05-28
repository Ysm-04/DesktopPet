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
    , m_currentFrameIndex(0)
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

    connect(&m_animationTimer, &QTimer::timeout, this, [this]() {
        advanceStateFrame();
    });

    loadInitialImage();
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction *topAction = menu.addAction(QStringLiteral("取消置顶"));
    topAction->setCheckable(true);
    topAction->setChecked(m_alwaysOnTop);

    addStateTestActions(&menu);

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
    else if (selectedAction != nullptr && selectedAction->data().isValid())
    {
        playState(selectedAction->data().toString(), false);
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
        playState(QStringLiteral("drag"), false);
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
        playState(QStringLiteral("idle"), false);
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PetWindow::loadInitialImage()
{
    QString loadError;
    m_characterPackage = CharacterPackageLoader::load(characterPackagePath(), &loadError);
    if (m_characterPackage.hasState(QStringLiteral("idle")))
    {
        playState(QStringLiteral("idle"), true);
        return;
    }

    setDisplayImage(previewImagePath(), true);
}

void PetWindow::addStateTestActions(QMenu *menu)
{
    QMenu *stateMenu = menu->addMenu(QStringLiteral("State Test"));
    const QStringList stateNames = {
        QStringLiteral("idle"),
        QStringLiteral("talk"),
        QStringLiteral("think"),
        QStringLiteral("sleep"),
        QStringLiteral("happy"),
        QStringLiteral("error"),
        QStringLiteral("drag"),
    };

    for (const QString &stateName : stateNames)
    {
        if (!m_characterPackage.hasState(stateName))
        {
            continue;
        }

        QAction *stateAction = stateMenu->addAction(stateName);
        stateAction->setData(stateName);
    }

    stateMenu->setEnabled(!stateMenu->actions().isEmpty());
}

void PetWindow::playState(const QString &stateName, bool centerWindow)
{
    if (m_currentStateName == stateName && !m_currentFrames.isEmpty())
    {
        return;
    }

    const CharacterState *state = m_characterPackage.state(stateName);
    if (state == nullptr || state->framePaths.isEmpty() || state->fps <= 0)
    {
        return;
    }

    m_animationTimer.stop();
    m_currentStateName = stateName;
    m_currentFrames = state->framePaths;
    m_currentFrameIndex = 0;

    setDisplayImage(m_currentFrames.at(m_currentFrameIndex), centerWindow);

    const int intervalMs = qMax(1, 1000 / state->fps);
    m_animationTimer.start(intervalMs);
}

void PetWindow::advanceStateFrame()
{
    if (m_currentFrames.isEmpty())
    {
        m_animationTimer.stop();
        return;
    }

    const CharacterState *state = m_characterPackage.state(m_currentStateName);
    if (state == nullptr)
    {
        m_animationTimer.stop();
        return;
    }

    if (m_currentFrameIndex + 1 >= m_currentFrames.size())
    {
        if (state->loop)
        {
            m_currentFrameIndex = 0;
        }
        else if (!state->nextState.isEmpty())
        {
            playState(state->nextState, false);
            return;
        }
        else
        {
            m_animationTimer.stop();
            return;
        }
    }
    else
    {
        ++m_currentFrameIndex;
    }

    setDisplayImage(m_currentFrames.at(m_currentFrameIndex), false);
}

void PetWindow::setDisplayImage(const QString &imagePath, bool centerWindow)
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

    if (centerWindow)
    {
        if (const QScreen *screen = QGuiApplication::primaryScreen())
        {
            const QRect available = screen->availableGeometry();
            move(available.center() - rect().center());
        }
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
