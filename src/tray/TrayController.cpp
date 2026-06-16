#include "TrayController.h"

#include "../character/CharacterPackageRepository.h"
#include "../ui/PetWindow.h"
#include "../util/ResourcePaths.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace
{
QIcon loadTrayIcon()
{
    const QIcon appIcon(ResourcePaths::appIconPath());
    if (!appIcon.isNull())
    {
        return appIcon;
    }

    const QPixmap pixmap(CharacterPackageRepository::defaultPreviewPath());
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

bool TrayController::showNotification(const QString &title, const QString &message)
{
    if (!isAvailable() || !m_trayIcon.isVisible() || !QSystemTrayIcon::supportsMessages())
    {
        return false;
    }

    m_trayIcon.showMessage(title, message, QSystemTrayIcon::Information, 10000);
    return true;
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

    QAction *settingsAction = m_menu.addAction(QStringLiteral("设置..."));
    QObject::connect(settingsAction, &QAction::triggered, [this]() {
        openSettings();
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

void TrayController::openSettings()
{
    if (m_petWindow == nullptr)
    {
        return;
    }

    m_petWindow->openSettingsDialog();
}

void TrayController::quitApplication()
{
    if (m_petWindow != nullptr)
    {
        m_petWindow->pauseAnimation();
    }

    QApplication::quit();
}
