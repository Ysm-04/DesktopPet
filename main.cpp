#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QObject>

#include "src/config/ConfigManager.h"
#include "src/tray/TrayController.h"
#include "src/ui/PetWindow.h"
#include "src/util/Logger.h"
#include "src/util/ResourcePaths.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("QtDesktopPet");
    QApplication::setOrganizationName("QtDesktopPet");

    const QIcon appIcon(ResourcePaths::appIconPath());
    if (!appIcon.isNull())
    {
        QApplication::setWindowIcon(appIcon);
    }

    Logger::info(QStringLiteral("Application started."));

    ConfigManager configManager;
    PetWindow window;
    window.applyAppConfig(configManager.loadAppConfig());

    TrayController trayController(&window);
    window.setSettingsFallbackInContextMenuEnabled(!trayController.isAvailable());
    trayController.show();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&configManager, &window]() {
        if (!configManager.saveAppConfig(window.currentAppConfig()))
        {
            Logger::warning(QStringLiteral("Failed to save app config."));
        }

        Logger::info(QStringLiteral("Application is exiting."));
    });

    window.show();

    return app.exec();
}
