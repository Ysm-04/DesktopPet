#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QIcon>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QDir>
#include <QTextStream>
#include <QEventLoop>
#include <QTimer>
#include "src/character/WebpAnimationBackend.h"
#include "src/character/AlphaVideoBackend.h"

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
    AlphaVideoSpec::configureRuntime();
    QApplication app(argc, argv);
    app.setApplicationVersion(QStringLiteral(PET_VERSION));
    if (app.arguments().contains(QStringLiteral("--version")))
    {
        QTextStream(stdout) << "QtDesktopPet " << app.applicationVersion() << Qt::endl;
        return 0;
    }
    // Read-only release self-check, before single-instance activation or personal configuration.
    if (app.arguments().contains(QStringLiteral("--check-video-runtime")))
    {
        const QString path = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("examples/character_alpha_video/runtime-probe.mp4"));
        AlphaVideoBackend probe(path, QStringLiteral("left-right"), {});
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        bool finished = false, pixels = false;
        int frames = 0;
        QString error;
        IPetMediaBackend::PlaybackCallbacks callbacks;
        callbacks.frame = [&](const QPixmap &frame) {
            ++frames;
            const auto image = frame.toImage();
            pixels = image.size() == QSize(64, 64) && qAlpha(image.pixel(4, 32)) <= 5
                && qAbs(qAlpha(image.pixel(24, 32)) - 128) <= 8 && qAlpha(image.pixel(36, 32)) >= 247;
        };
        callbacks.finished = [&] { finished = true; loop.quit(); };
        callbacks.error = [&](const QString &message) { error = message; loop.quit(); };
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { error = QStringLiteral("视频自检超时。"); loop.quit(); });
        if (probe.startPlayback(false, std::move(callbacks)))
        {
            timeout.start(8000);
            loop.exec();
        }
        else error = probe.errorString();
        probe.stopPlayback();
        const bool valid = finished && pixels && frames >= 8 && error.isEmpty();
        QTextStream(stdout) << (valid ? QStringLiteral("VIDEO_RUNTIME_PASS") : error) << Qt::endl;
        return valid ? 0 : 2;
    }
    if (app.arguments().contains(QStringLiteral("--check-webp-runtime")))
    {
        const QString path = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("examples/character_webp/runtime-probe.webp"));
        QString error;
        WebpAnimationBackend probe(path, {});
        const bool valid = probe.ensureLoaded() && probe.frameCount() == 4
            && WebpAnimationBackend::validateFile(path, &error);
        if (!valid && error.isEmpty()) error = probe.errorString();
        QTextStream(stdout) << (valid ? QStringLiteral("WEBP_RUNTIME_PASS") : error) << Qt::endl;
        return valid ? 0 : 2;
    }
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
    window.setTrayNotificationCallback([&trayController](const QString &title, const QString &message) {
        return trayController.showNotification(title, message);
    });
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
