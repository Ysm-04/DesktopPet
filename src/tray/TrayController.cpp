#include "TrayController.h"

#include "../ui/PetWindow.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QPixmap>

namespace
{
QString trayIconPath()
{
    return QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/shiroko/preview.png");
}

QIcon loadTrayIcon()
{
    const QPixmap pixmap(trayIconPath());
    if (!pixmap.isNull())
    {
        return QIcon(pixmap);
    }

    return QIcon();
}
}

TrayController::TrayController(PetWindow *petWindow)
    : m_petWindow(petWindow)
{
    m_trayIcon.setIcon(loadTrayIcon());
    m_trayIcon.setToolTip(QStringLiteral("QtDesktopPet"));

    createMenu();
    m_trayIcon.setContextMenu(&m_menu);

    QObject::connect(&m_trayIcon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        {
            togglePetWindowVisibility();
        }
    });
}

bool TrayController::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::show()
{
    if (!isAvailable())
    {
        return;
    }

    m_trayIcon.show();
}

void TrayController::createMenu()
{
    QAction *showAction = m_menu.addAction(QStringLiteral("显示桌宠"));
    QObject::connect(showAction, &QAction::triggered, [this]() {
        showPetWindow();
    });

    QAction *hideAction = m_menu.addAction(QStringLiteral("隐藏桌宠"));
    QObject::connect(hideAction, &QAction::triggered, [this]() {
        hidePetWindow();
    });

    m_menu.addSeparator();

    QAction *quitAction = m_menu.addAction(QStringLiteral("退出"));
    QObject::connect(quitAction, &QAction::triggered, [this]() {
        quitApplication();
    });
}

void TrayController::showPetWindow()
{
    if (m_petWindow == nullptr)
    {
        return;
    }

    m_petWindow->show();
    m_petWindow->raise();
    m_petWindow->resumeAnimation();
}

void TrayController::hidePetWindow()
{
    if (m_petWindow == nullptr)
    {
        return;
    }

    m_petWindow->pauseAnimation();
    m_petWindow->hide();
}

void TrayController::togglePetWindowVisibility()
{
    if (m_petWindow == nullptr)
    {
        return;
    }

    if (m_petWindow->isVisible())
    {
        hidePetWindow();
        return;
    }

    showPetWindow();
}

void TrayController::quitApplication()
{
    if (m_petWindow != nullptr)
    {
        m_petWindow->pauseAnimation();
    }

    QApplication::quit();
}
