#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

inline constexpr int AgentStatusProtocolVersion = 1;
inline constexpr int AgentStatusMaxPacketBytes = 16 * 1024;
inline constexpr int AgentStatusDefaultTtlSec = 180;
inline constexpr int AgentStatusMinTtlSec = 10;
inline constexpr int AgentStatusMaxTtlSec = 3600;
inline constexpr int AgentStatusDoneRetentionSec = 25;
inline constexpr int AgentStatusSummaryMaxChars = 120;
inline constexpr int AgentStatusWorkspaceMaxChars = 64;
inline constexpr int AgentStatusSessionIdMaxChars = 96;
inline constexpr int AgentStatusSourceMaxChars = 32;
inline constexpr int AgentActivityLimit = 50;
inline constexpr int AgentRecentSessionLimit = 20;
inline constexpr qint64 AgentRecentRetentionMs = 10 * 60 * 1000;

inline QString defaultAgentStatusPipeName()
{
    return QStringLiteral("QtDesktopPet.AgentStatus.v1");
}

enum class AgentState
{
    Idle = 0,
    Done = 1,
    Working = 2,
    Error = 3,
    NeedsInput = 4,
};

struct AgentActivity
{
    QString id;
    QString kind;
    QString tool;
    QString summary;
    // started, finished (outcome unknown), succeeded, failed, cancelled
    QString phase;
    qint64 startedAtMs = 0;
    qint64 updatedAtMs = 0;
};

struct AgentStatusEvent
{
    int protocol = AgentStatusProtocolVersion;
    QString source;
    QString sessionId;
    QString workspace;
    AgentState state = AgentState::Idle;
    QString summary;
    int ttlSec = AgentStatusDefaultTtlSec;
    QString title;
    QString turnId;
    QString eventKind; // session_start, turn_start, tool, turn_end, turn_pause, session_end, attention
    QString eventId;
    AgentActivity activity;
};

struct AgentSessionSnapshot
{
    QString sessionKey;
    QString source;
    QString sessionId;
    QString workspace;
    AgentState state = AgentState::Idle;
    QString summary;
    qint64 updatedAtMs = 0;
    qint64 expiresAtMs = 0;
    bool dismissed = false;
    QString title;
    QString turnId;
    qint64 retainUntilMs = 0;
    bool stale = false;
    bool interrupted = false;
    QVector<AgentActivity> activities;
    QStringList recentEventIds;
};

struct AgentArbitrationResult
{
    AgentState overallState = AgentState::Idle;
    bool hasPrimary = false;
    AgentSessionSnapshot primarySession;
    QVector<AgentSessionSnapshot> activeSessions;
    QVector<AgentSessionSnapshot> visibleSessions;
};
