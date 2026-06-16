#pragma once

#include <QMenu>
#include <QSystemTrayIcon>

class PetWindow;

class TrayController
{
public:
    explicit TrayController(PetWindow *petWindow);

    bool isAvailable() const;
    void show();
    bool showNotification(const QString &title, const QString &message);

private:
    void createMenu();
    void showPetWindow();
    void hidePetWindow();
    void togglePetWindowVisibility();
    void openSettings();
    void quitApplication();

    PetWindow *m_petWindow = nullptr;
    QSystemTrayIcon m_trayIcon;
    QMenu m_menu;
};
