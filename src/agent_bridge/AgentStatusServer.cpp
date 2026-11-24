#include "AgentStatusServer.h"

#include "AgentStatusCodec.h"
#include "../util/Logger.h"

#include <QByteArray>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>

#include <memory>
#include <utility>

namespace
{
void processClientPayload(
    const QByteArray &payload,
    const AgentStatusServer::EventCallback &callback)
{
    const QByteArray trimmed = payload.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    AgentStatusEvent event;
    QString parseError;
    if (!AgentStatusCodec::parsePacketJson(trimmed, &event, &parseError))
    {
        Logger::warning(QStringLiteral("Agent status packet ignored: ") + parseError);
        return;
    }

    if (callback)
    {
        callback(event);
    }
}
}

AgentStatusServer::AgentStatusServer(QString pipeName)
    : m_pipeName(pipeName.trimmed().isEmpty() ? defaultAgentStatusPipeName() : std::move(pipeName))
    , m_server(std::make_unique<QLocalServer>())
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    QObject::connect(m_server.get(), &QLocalServer::newConnection, m_server.get(), [this]() {
        handleNewConnections();
    });
}

AgentStatusServer::~AgentStatusServer()
{
    stop();
}

void AgentStatusServer::setEventCallback(EventCallback callback)
{
    m_eventCallback = std::move(callback);
}

bool AgentStatusServer::start(QString *errorMessage)
{
    if (!m_server)
    {
        return false;
    }

    if (m_server->isListening())
    {
        return true;
    }

    QLocalServer::removeServer(m_pipeName);
    if (!m_server->listen(m_pipeName))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = m_server->errorString();
        }
        Logger::warning(QStringLiteral("Failed to listen on agent status pipe %1: %2")
            .arg(m_pipeName, m_server->errorString()));
        return false;
    }

    return true;
}

void AgentStatusServer::stop()
{
    if (m_server && m_server->isListening())
    {
        m_server->close();
    }
}

bool AgentStatusServer::isListening() const
{
    return m_server && m_server->isListening();
}

QString AgentStatusServer::pipeName() const
{
    return m_pipeName;
}

void AgentStatusServer::handleNewConnections()
{
    if (!m_server)
    {
        return;
    }

    while (m_server->hasPendingConnections())
    {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (socket == nullptr)
        {
            continue;
        }

        auto buffer = std::make_shared<QByteArray>();
        auto completed = std::make_shared<bool>(false);
        QPointer<QLocalServer> serverGuard(m_server.get());

        auto finalizeSocket = [this, socket, buffer, completed, serverGuard]() {
            if (*completed)
            {
                return;
            }
            *completed = true;

            if (!serverGuard.isNull())
            {
                processClientPayload(*buffer, m_eventCallback);
            }
            socket->disconnectFromServer();
            socket->deleteLater();
        };

        QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer, completed, finalizeSocket]() {
            if (*completed)
            {
                return;
            }

            const QByteArray chunk = socket->read(AgentStatusMaxPacketBytes - buffer->size() + 1);
            buffer->append(chunk);
            if (buffer->size() > AgentStatusMaxPacketBytes)
            {
                *completed = true;
                Logger::warning(QStringLiteral("Agent status packet exceeded 16 KB limit and was dropped."));
                socket->abort();
                socket->deleteLater();
                return;
            }

            const int newlineIndex = buffer->indexOf('\n');
            if (newlineIndex >= 0)
            {
                *buffer = buffer->left(newlineIndex);
                finalizeSocket();
            }
        });

        QObject::connect(socket, &QLocalSocket::disconnected, socket, [socket, buffer, completed, finalizeSocket]() {
            if (!*completed)
            {
                if (socket->bytesAvailable() > 0)
                {
                    const qint64 remainingAllowance = AgentStatusMaxPacketBytes - buffer->size() + 1;
                    if (remainingAllowance > 0)
                    {
                        buffer->append(socket->read(remainingAllowance));
                    }
                }
                if (buffer->size() > AgentStatusMaxPacketBytes)
                {
                    *completed = true;
                    Logger::warning(QStringLiteral("Agent status packet exceeded 16 KB limit and was dropped."));
                    socket->deleteLater();
                    return;
                }
                finalizeSocket();
                return;
            }
            socket->deleteLater();
        });
    }
}
