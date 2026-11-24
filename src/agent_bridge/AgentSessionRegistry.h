#pragma once

#include "AgentStatusTypes.h"

#include <QMap>
#include <QString>
#include <QtGlobal>

struct AgentRegistryUpdateResult
{
    bool accepted = false;
    bool stateChanged = false;
    bool overallStateChanged = false;
    bool debounced = false;
    AgentSessionSnapshot session;
    AgentArbitrationResult arbitration;
};

class AgentSessionRegistry
{
public:
    static QString buildSessionKey(
        const QString &source,
        const QString &sessionId,
        const QString &workspace = {});

    AgentRegistryUpdateResult ingest(const AgentStatusEvent &event, qint64 nowMs);
    bool dismissSession(const QString &sessionKey, qint64 nowMs);
    bool pruneExpired(qint64 nowMs);
    void clear();
    void clearCompleted(qint64 nowMs);

    AgentArbitrationResult arbitrate(qint64 nowMs) const;
    int activeSessionCount(qint64 nowMs) const;

private:
    QMap<QString, AgentSessionSnapshot> m_sessions;
    quint64 m_activitySerial = 0;
    void limitRecentSessions(qint64 nowMs);
};
