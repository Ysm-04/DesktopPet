#pragma once

#include "AgentStatusTypes.h"

#include <QByteArray>
#include <QString>

class AgentStatusCodec
{
public:
    static QString normalizeSource(const QString &rawSource);
    static QString sourceDisplayLabel(const QString &source);
    static bool normalizeState(const QString &rawState, AgentState *parsedState);
    static QString stateToWireString(AgentState state);
    static QString stateDisplayText(AgentState state);
    static int statePriority(AgentState state);

    static QString sanitizeSessionId(const QString &rawSessionId);
    static QString extractWorkspaceDisplayName(const QString &rawWorkspaceOrPath);
    static QString sanitizeSummary(const QString &rawSummary);
    static bool containsSensitiveSecret(const QString &text);
    static QString sanitizeActivityText(const QString &text);
    static QString activityKind(const QString &tool);

    static bool parsePacketJson(
        const QByteArray &payload,
        AgentStatusEvent *event,
        QString *errorMessage = nullptr);
    static QByteArray serializeEventJson(const AgentStatusEvent &event);

    static void enrichEventFromHookStdin(
        AgentStatusEvent *event,
        const QByteArray &stdinBytes,
        bool explicitStateProvided);
};
