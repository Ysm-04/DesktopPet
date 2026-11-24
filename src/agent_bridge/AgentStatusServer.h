#pragma once

#include "AgentStatusTypes.h"

#include <QString>

#include <functional>
#include <memory>

class QLocalServer;

class AgentStatusServer
{
public:
    using EventCallback = std::function<void(const AgentStatusEvent &)>;

    explicit AgentStatusServer(QString pipeName = defaultAgentStatusPipeName());
    ~AgentStatusServer();

    void setEventCallback(EventCallback callback);
    bool start(QString *errorMessage = nullptr);
    void stop();
    bool isListening() const;
    QString pipeName() const;

private:
    void handleNewConnections();

    QString m_pipeName;
    std::unique_ptr<QLocalServer> m_server;
    EventCallback m_eventCallback;
};
