#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QIcon>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>

#include "src/config/ConfigManager.h"
#include "src/tray/TrayController.h"
#include "src/ui/PetWindow.h"
#include "src/util/Logger.h"
#include "src/util/ResourcePaths.h"

namespace
{
const QString SingleInstanceServerName = QStringLiteral("Make.QtDesktopPet.SingleInstance");
const QByteArray ActivateMessage("activate\n");
constexpr int SingleInstanceConnectTimeoutMs = 500;
constexpr int SingleInstanceWriteTimeoutMs = 500;

bool notifyRunningInstance()
{
    QLocalSocket socket;
    socket.connectToServer(SingleInstanceServerName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(SingleInstanceConnectTimeoutMs))
    {
        return false;
    }

    socket.write(ActivateMessage);
    socket.flush();
    socket.waitForBytesWritten(SingleInstanceWriteTimeoutMs);
    socket.disconnectFromServer();
    return true;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("QtDesktopPet");
    QApplication::setOrganizationName("QtDesktopPet");

    if (notifyRunningInstance())
    {
        return 0;
    }

    const QIcon appIcon(ResourcePaths::appIconPath());
    if (!appIcon.isNull())
    {
        QApplication::setWindowIcon(appIcon);
    }

    Logger::info(QStringLiteral("Application started."));

    QLocalServer singleInstanceServer;
    QLocalServer::removeServer(SingleInstanceServerName);
    if (!singleInstanceServer.listen(SingleInstanceServerName))
    {
        if (notifyRunningInstance())
        {
            return 0;
        }

        Logger::warning(QStringLiteral("Unable to start single instance server: ")
            + singleInstanceServer.errorString());
    }

    ConfigManager configManager;
    PetWindow window;
    window.applyAppConfig(configManager.loadAppConfig());

    TrayController trayController(&window);
    window.setSettingsFallbackInContextMenuEnabled(!trayController.isAvailable());
    trayController.show();

    QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, [&singleInstanceServer, &window]() {
        while (singleInstanceServer.hasPendingConnections())
        {
            QLocalSocket *socket = singleInstanceServer.nextPendingConnection();
            if (socket == nullptr)
            {
                continue;
            }

            const auto handleActivationMessage = [socket, &window]() {
                const QByteArray message = socket->readAll().trimmed();
                if (message == ActivateMessage.trimmed())
                {
                    window.activateFromExternalInstance();
                }
            };
            QObject::connect(socket, &QLocalSocket::readyRead, handleActivationMessage);
            QObject::connect(socket, &QLocalSocket::disconnected, [socket, handleActivationMessage]() {
                if (socket->bytesAvailable() > 0)
                {
                    handleActivationMessage();
                }
                socket->deleteLater();
            });
            if (socket->bytesAvailable() > 0)
            {
                handleActivationMessage();
            }
        }
    });

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
