#include "AgentSessionRegistry.h"

#include "AgentStatusCodec.h"

#include <QtGlobal>

#include <algorithm>

namespace
{
constexpr qint64 DebounceWindowMs = 200;

qint64 retentionMsForEvent(const AgentStatusEvent &event)
{
    const int boundedTtl = qBound(AgentStatusMinTtlSec, event.ttlSec, AgentStatusMaxTtlSec);
    const int effectiveSec = event.state == AgentState::Done
        ? qMin(boundedTtl, AgentStatusDoneRetentionSec)
        : boundedTtl;
    return static_cast<qint64>(effectiveSec) * 1000;
}
}

QString AgentSessionRegistry::buildSessionKey(
    const QString &source,
    const QString &sessionId,
    const QString &workspace)
{
    const QString normalizedSource = AgentStatusCodec::normalizeSource(source);
    const QString cleanedSession = AgentStatusCodec::sanitizeSessionId(sessionId);
    if (!cleanedSession.isEmpty())
    {
        return normalizedSource + QLatin1Char(':') + cleanedSession;
    }

    const QString workspaceName = AgentStatusCodec::extractWorkspaceDisplayName(workspace);
    if (!workspaceName.isEmpty())
    {
        return normalizedSource + QStringLiteral(":ws:") + workspaceName;
    }

    return normalizedSource + QStringLiteral(":default");
}

AgentRegistryUpdateResult AgentSessionRegistry::ingest(const AgentStatusEvent &event, qint64 nowMs)
{
    const AgentState previousOverall = arbitrate(nowMs).overallState;
    pruneExpired(nowMs);
    AgentRegistryUpdateResult result;
    result.accepted = true;
    const QString source = AgentStatusCodec::normalizeSource(event.source);
    const QString id = AgentStatusCodec::sanitizeSessionId(event.sessionId);
    const QString workspace = AgentStatusCodec::extractWorkspaceDisplayName(event.workspace);
    QString key = buildSessionKey(source, id, workspace);

    // Never guess which identified conversation an anonymous event belongs to.
    // Promote only an exact provisional workspace record when its stable ID arrives.
    if (!id.isEmpty() && !workspace.isEmpty() && !m_sessions.contains(key))
    {
        const QString provisionalKey = buildSessionKey(source, {}, workspace);
        auto provisional = m_sessions.find(provisionalKey);
        if (provisional != m_sessions.end() && provisional->sessionId.isEmpty())
        {
            AgentSessionSnapshot promoted = provisional.value();
            m_sessions.erase(provisional);
            promoted.sessionKey = key;
            promoted.sessionId = id;
            m_sessions.insert(key, promoted);
        }
    }
    auto it = m_sessions.find(key);
    const bool existed = it != m_sessions.end();
    const AgentState oldState = existed ? it->state : AgentState::Idle;
    if (event.state == AgentState::Idle && event.eventKind.isEmpty())
    {
        m_sessions.remove(key); // Legacy idle packets retain their removal semantics.
        result.stateChanged = existed;
        result.session.sessionKey = key;
        result.session.source = source;
        result.session.state = AgentState::Idle;
    }
    else
    {
        if (!existed)
        {
            AgentSessionSnapshot created;
            created.sessionKey = key;
            created.source = source;
            created.sessionId = id;
            it = m_sessions.insert(key, created);
        }
        AgentSessionSnapshot &snapshot = it.value();
        if (!event.eventId.isEmpty() && snapshot.recentEventIds.contains(event.eventId))
        {
            result.debounced = true;
            result.session = snapshot;
            result.arbitration = arbitrate(nowMs);
            return result;
        }
        if (!event.eventId.isEmpty())
        {
            snapshot.recentEventIds.append(event.eventId);
            while (snapshot.recentEventIds.size() > 128) snapshot.recentEventIds.removeFirst();
        }
        if (!workspace.isEmpty()) snapshot.workspace = workspace;
        if (!event.title.isEmpty()) snapshot.title = AgentStatusCodec::sanitizeActivityText(event.title);
        const bool toolEvent = event.eventKind == QStringLiteral("tool");
        const bool scopedEvent = toolEvent || event.eventKind == QStringLiteral("turn_end")
            || event.eventKind == QStringLiteral("turn_pause") || event.eventKind == QStringLiteral("attention");
        const bool oldTurn = scopedEvent && !event.turnId.isEmpty() && !snapshot.turnId.isEmpty()
            && event.turnId != snapshot.turnId;
        const bool afterEnd = toolEvent && existed && (oldState == AgentState::Done || oldState == AgentState::Idle);
        const bool endedAfterInterrupt = snapshot.interrupted && event.eventKind == QStringLiteral("turn_end")
            && (event.turnId.isEmpty() || snapshot.turnId.isEmpty() || event.turnId == snapshot.turnId);
        const bool stateEligible = !oldTurn && !afterEnd && !endedAfterInterrupt;
        const QString summary = AgentStatusCodec::sanitizeSummary(event.summary);
        result.debounced = existed && oldState == event.state && snapshot.summary == summary
            && nowMs >= snapshot.updatedAtMs && nowMs - snapshot.updatedAtMs < DebounceWindowMs;

        if (toolEvent && (!event.activity.id.isEmpty() || !event.activity.tool.isEmpty() || !event.activity.summary.isEmpty()))
        {
            AgentActivity activity = event.activity;
            activity.id = AgentStatusCodec::sanitizeSessionId(activity.id);
            // IDs are scoped to a turn when supplied by the adapter.
            if (!activity.id.isEmpty() && !event.turnId.isEmpty())
                activity.id = event.turnId + QLatin1Char(':') + activity.id;
            if (activity.id.isEmpty()) activity.id = QStringLiteral("local:%1").arg(++m_activitySerial);
            activity.tool = AgentStatusCodec::sanitizeActivityText(activity.tool);
            activity.summary = AgentStatusCodec::sanitizeActivityText(activity.summary);
            activity.kind = AgentStatusCodec::activityKind(activity.tool);
            activity.startedAtMs = nowMs;
            activity.updatedAtMs = nowMs;
            auto existing = std::find_if(snapshot.activities.begin(), snapshot.activities.end(),
                [&activity](const AgentActivity &entry) { return entry.id == activity.id; });
            if (existing == snapshot.activities.end()) snapshot.activities.append(activity);
            else
            {
                // A delayed start cannot downgrade a terminal result or resurrect its conversation.
                if (existing->phase == QStringLiteral("started") || activity.phase != QStringLiteral("started"))
                {
                    activity.startedAtMs = existing->startedAtMs;
                    if (activity.summary.isEmpty() || activity.summary == activity.tool) activity.summary = existing->summary;
                    if (activity.tool.isEmpty()) activity.tool = existing->tool;
                    activity.kind = AgentStatusCodec::activityKind(activity.tool);
                    *existing = activity;
                }
            }
            while (snapshot.activities.size() > AgentActivityLimit) snapshot.activities.removeFirst();
        }
        if (stateEligible)
        {
            if (event.eventKind == QStringLiteral("turn_start") || event.eventKind == QStringLiteral("session_start"))
                snapshot.interrupted = false;
            else if (event.eventKind == QStringLiteral("turn_pause"))
            {
                snapshot.interrupted = true;
                for (AgentActivity &activity : snapshot.activities)
                    if (activity.phase == QStringLiteral("started"))
                    {
                        activity.phase = QStringLiteral("cancelled");
                        activity.updatedAtMs = nowMs;
                    }
            }
            // Tool progress does not clear an outstanding conversation-level request/error.
            if (!toolEvent || (oldState != AgentState::NeedsInput && oldState != AgentState::Error))
                snapshot.state = event.eventKind == QStringLiteral("session_end") ? AgentState::Idle : event.state;
            if (!event.turnId.isEmpty()) snapshot.turnId = event.turnId;
            snapshot.summary = summary;
            snapshot.updatedAtMs = nowMs;
            snapshot.expiresAtMs = event.eventKind == QStringLiteral("session_end")
                ? nowMs : nowMs + retentionMsForEvent(event);
            snapshot.retainUntilMs = snapshot.expiresAtMs + AgentRecentRetentionMs;
            snapshot.stale = false;
            if (snapshot.state != oldState) snapshot.dismissed = false;
        }
        result.stateChanged = stateEligible && snapshot.state != oldState;
        result.session = snapshot;
        limitRecentSessions(nowMs);
    }
    result.arbitration = arbitrate(nowMs);
    result.overallStateChanged = result.arbitration.overallState != previousOverall;
    return result;
}

bool AgentSessionRegistry::dismissSession(const QString &sessionKey, qint64 nowMs)
{
    pruneExpired(nowMs);
    auto iterator = m_sessions.find(sessionKey);
    if (iterator == m_sessions.end())
    {
        return false;
    }

    iterator.value().dismissed = true;
    return true;
}

bool AgentSessionRegistry::pruneExpired(qint64 nowMs)
{
    bool changed = false;
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        if (it->retainUntilMs <= nowMs)
        {
            it = m_sessions.erase(it);
            changed = true;
        }
        else
        {
            if (it->expiresAtMs <= nowMs && !it->stale)
            {
                it->stale = true;
                changed = true;
            }
            ++it;
        }
    }
    limitRecentSessions(nowMs);
    return changed;
}

void AgentSessionRegistry::limitRecentSessions(qint64 nowMs)
{
    QMap<QString, QVector<AgentSessionSnapshot>> recent;
    for (const AgentSessionSnapshot &session : m_sessions)
        if (session.state == AgentState::Done || session.state == AgentState::Idle || session.expiresAtMs <= nowMs)
            recent[session.source].append(session);
    for (auto &sessions : recent)
    {
        std::sort(sessions.begin(), sessions.end(), [](const auto &a, const auto &b) { return a.updatedAtMs > b.updatedAtMs; });
        for (int i = AgentRecentSessionLimit; i < sessions.size(); ++i) m_sessions.remove(sessions[i].sessionKey);
    }
}

void AgentSessionRegistry::clearCompleted(qint64 nowMs)
{
    for (auto it = m_sessions.begin(); it != m_sessions.end();)
    {
        if (it->state == AgentState::Done || it->state == AgentState::Idle || it->expiresAtMs <= nowMs)
            it = m_sessions.erase(it);
        else ++it;
    }
}

void AgentSessionRegistry::clear()
{
    m_sessions.clear();
}

AgentArbitrationResult AgentSessionRegistry::arbitrate(qint64 nowMs) const
{
    AgentArbitrationResult result;
    for (auto iterator = m_sessions.constBegin(); iterator != m_sessions.constEnd(); ++iterator)
    {
        const AgentSessionSnapshot &snapshot = iterator.value();
        if (snapshot.retainUntilMs <= nowMs || snapshot.dismissed) continue;
        AgentSessionSnapshot visible = snapshot;
        visible.stale = snapshot.expiresAtMs <= nowMs && snapshot.state != AgentState::Done && snapshot.state != AgentState::Idle;
        result.visibleSessions.append(visible);
        if (snapshot.expiresAtMs > nowMs && snapshot.state != AgentState::Idle)
            result.activeSessions.append(snapshot);
    }

    std::sort(result.activeSessions.begin(), result.activeSessions.end(), [](const AgentSessionSnapshot &left, const AgentSessionSnapshot &right) {
        const int leftPriority = AgentStatusCodec::statePriority(left.state);
        const int rightPriority = AgentStatusCodec::statePriority(right.state);
        if (leftPriority != rightPriority)
        {
            return leftPriority > rightPriority;
        }
        if (left.updatedAtMs != right.updatedAtMs)
        {
            return left.updatedAtMs > right.updatedAtMs;
        }
        return left.sessionKey < right.sessionKey;
    });

    if (!result.activeSessions.isEmpty())
    {
        result.hasPrimary = true;
        result.primarySession = result.activeSessions.first();
        result.overallState = result.primarySession.state;
    }

    return result;
}

int AgentSessionRegistry::activeSessionCount(qint64 nowMs) const
{
    return arbitrate(nowMs).activeSessions.size();
}
