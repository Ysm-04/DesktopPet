#include "AgentStatusCodec.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QtGlobal>

namespace
{
bool setCodecError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}

QString firstNonEmptyString(const QJsonObject &object, const std::initializer_list<const char *> &keys)
{
    for (const char *key : keys)
    {
        const QJsonValue value = object.value(QLatin1String(key));
        if (value.isString())
        {
            const QString trimmed = value.toString().trimmed();
            if (!trimmed.isEmpty())
            {
                return trimmed;
            }
        }
    }
    return {};
}

QString firstWorkspacePathFromJson(const QJsonObject &object)
{
    const QString direct = firstNonEmptyString(object, {"workspace", "cwd", "project", "root", "workingDirectory"});
    if (!direct.isEmpty())
    {
        return direct;
    }

    const QJsonValue workspacePaths = object.value(QStringLiteral("workspacePaths"));
    if (workspacePaths.isArray())
    {
        for (const QJsonValue &entry : workspacePaths.toArray())
        {
            if (entry.isString())
            {
                const QString path = entry.toString().trimmed();
                if (!path.isEmpty())
                {
                    return path;
                }
            }
        }
    }

    return {};
}

QString extractHookSummary(const QJsonObject &root)
{
    const QJsonValue toolCallValue = root.value(QStringLiteral("toolCall"));
    if (toolCallValue.isObject())
    {
        const QString toolName = firstNonEmptyString(toolCallValue.toObject(), {"name", "toolName"});
        if (!toolName.isEmpty())
        {
            return toolName;
        }
    }

    const QString directTool = firstNonEmptyString(root, {"summary", "message", "tool_name", "toolName"});
    if (!directTool.isEmpty())
    {
        return directTool;
    }

    const QString hookEvent = firstNonEmptyString(root, {"hook_event_name", "hookEventName", "event"});
    return hookEvent;
}

QString idValue(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    return value.isDouble() ? QString::number(value.toInteger()) : value.toString().trimmed();
}

QJsonObject inputObject(const QJsonValue &value)
{
    if (value.isObject())
    {
        return value.toObject();
    }
    if (value.isString())
    {
        return QJsonDocument::fromJson(value.toString().toUtf8()).object();
    }
    return {};
}

QString normalizedPhase(const QString &phase)
{
    static const QStringList allowed = {
        QStringLiteral("started"), QStringLiteral("finished"), QStringLiteral("succeeded"),
        QStringLiteral("failed"), QStringLiteral("cancelled")
    };
    return allowed.contains(phase) ? phase : QStringLiteral("finished");
}

QString normalizedEventKind(const QString &kind)
{
    static const QStringList allowed = {
        QStringLiteral("session_start"), QStringLiteral("turn_start"), QStringLiteral("tool"),
        QStringLiteral("turn_end"), QStringLiteral("turn_pause"), QStringLiteral("session_end"), QStringLiteral("attention")
    };
    return allowed.contains(kind) ? kind : QString();
}
}

QString AgentStatusCodec::normalizeSource(const QString &rawSource)
{
    const QString lowered = rawSource.trimmed().toLower();
    if (lowered == QStringLiteral("agy") || lowered == QStringLiteral("antigravity-ide") || lowered == QStringLiteral("antigravity-cli"))
    {
        return QStringLiteral("antigravity");
    }

    if (lowered == QStringLiteral("deepseek-harness")
        || lowered == QStringLiteral("deepseek_harness")
        || lowered == QStringLiteral("cordis-dsh"))
    {
        return QStringLiteral("dsh");
    }

    QString cleaned;
    cleaned.reserve(lowered.size());
    for (const QChar ch : lowered)
    {
        const bool allowed = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('_');
        if (allowed)
        {
            cleaned.append(ch);
            if (cleaned.size() >= AgentStatusSourceMaxChars)
            {
                break;
            }
        }
    }

    return cleaned.isEmpty() ? QStringLiteral("custom") : cleaned;
}

QString AgentStatusCodec::sourceDisplayLabel(const QString &source)
{
    const QString normalized = normalizeSource(source);
    if (normalized == QStringLiteral("codex"))
    {
        return QStringLiteral("Codex");
    }
    if (normalized == QStringLiteral("antigravity"))
    {
        return QStringLiteral("Antigravity");
    }
    if (normalized == QStringLiteral("dsh"))
    {
        return QStringLiteral("DSH");
    }
    if (normalized == QStringLiteral("claude"))
    {
        return QStringLiteral("Claude");
    }

    QString label = normalized;
    label[0] = label.at(0).toUpper();
    return label;
}

bool AgentStatusCodec::normalizeState(const QString &rawState, AgentState *parsedState)
{
    if (parsedState == nullptr)
    {
        return false;
    }

    const QString lowered = rawState.trimmed().toLower();
    if (lowered == QStringLiteral("working")
        || lowered == QStringLiteral("running")
        || lowered == QStringLiteral("thinking")
        || lowered == QStringLiteral("busy")
        || lowered == QStringLiteral("in_progress")
        || lowered == QStringLiteral("in-progress")
        || lowered == QStringLiteral("executing")
        || lowered == QStringLiteral("started")
        || lowered == QStringLiteral("active"))
    {
        *parsedState = AgentState::Working;
        return true;
    }

    if (lowered == QStringLiteral("needs_input")
        || lowered == QStringLiteral("needs-input")
        || lowered == QStringLiteral("needsinput")
        || lowered == QStringLiteral("waiting")
        || lowered == QStringLiteral("waiting_input")
        || lowered == QStringLiteral("waiting-input")
        || lowered == QStringLiteral("approval")
        || lowered == QStringLiteral("needs_approval")
        || lowered == QStringLiteral("blocked")
        || lowered == QStringLiteral("paused")
        || lowered == QStringLiteral("confirm")
        || lowered == QStringLiteral("question"))
    {
        *parsedState = AgentState::NeedsInput;
        return true;
    }

    if (lowered == QStringLiteral("done")
        || lowered == QStringLiteral("completed")
        || lowered == QStringLiteral("complete")
        || lowered == QStringLiteral("success")
        || lowered == QStringLiteral("succeeded")
        || lowered == QStringLiteral("finished"))
    {
        *parsedState = AgentState::Done;
        return true;
    }

    if (lowered == QStringLiteral("error")
        || lowered == QStringLiteral("failed")
        || lowered == QStringLiteral("fail")
        || lowered == QStringLiteral("crashed")
        || lowered == QStringLiteral("exception"))
    {
        *parsedState = AgentState::Error;
        return true;
    }

    if (lowered == QStringLiteral("idle")
        || lowered == QStringLiteral("stopped")
        || lowered == QStringLiteral("canceled")
        || lowered == QStringLiteral("cancelled")
        || lowered == QStringLiteral("clear"))
    {
        *parsedState = AgentState::Idle;
        return true;
    }

    return false;
}

QString AgentStatusCodec::stateToWireString(AgentState state)
{
    switch (state)
    {
    case AgentState::Idle:
        return QStringLiteral("idle");
    case AgentState::Done:
        return QStringLiteral("done");
    case AgentState::Working:
        return QStringLiteral("working");
    case AgentState::Error:
        return QStringLiteral("error");
    case AgentState::NeedsInput:
        return QStringLiteral("needs_input");
    }

    return QStringLiteral("idle");
}

QString AgentStatusCodec::stateDisplayText(AgentState state)
{
    switch (state)
    {
    case AgentState::Idle:
        return QStringLiteral("空闲");
    case AgentState::Done:
        return QStringLiteral("已完成");
    case AgentState::Working:
        return QStringLiteral("运行中");
    case AgentState::Error:
        return QStringLiteral("异常");
    case AgentState::NeedsInput:
        return QStringLiteral("待确认");
    }

    return QStringLiteral("空闲");
}

int AgentStatusCodec::statePriority(AgentState state)
{
    return static_cast<int>(state);
}

QString AgentStatusCodec::sanitizeSessionId(const QString &rawSessionId)
{
    QString cleaned;
    cleaned.reserve(qMin(rawSessionId.size(), AgentStatusSessionIdMaxChars));
    for (const QChar ch : rawSessionId.trimmed())
    {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7F || ch.isSpace())
        {
            continue;
        }
        cleaned.append(ch);
        if (cleaned.size() >= AgentStatusSessionIdMaxChars)
        {
            break;
        }
    }
    return cleaned;
}

QString AgentStatusCodec::extractWorkspaceDisplayName(const QString &rawWorkspaceOrPath)
{
    QString normalized = rawWorkspaceOrPath.trimmed();
    if (normalized.isEmpty())
    {
        return {};
    }

    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (normalized.endsWith(QLatin1Char('/')) && normalized.size() > 1)
    {
        normalized.chop(1);
    }

    QString display = normalized;
    if (normalized.contains(QLatin1Char('/')))
    {
        const QString baseName = QFileInfo(normalized).fileName().trimmed();
        if (!baseName.isEmpty())
        {
            display = baseName;
        }
    }

    display = display.simplified();
    if (display.size() > AgentStatusWorkspaceMaxChars)
    {
        display = display.left(AgentStatusWorkspaceMaxChars).trimmed();
    }
    return display;
}

bool AgentStatusCodec::containsSensitiveSecret(const QString &text)
{
    static const QRegularExpression TokenPattern(QStringLiteral(
        "(?:\\bsk-[A-Za-z0-9_-]{12,}|\\bAIza[0-9A-Za-z_-]{20,}|\\bghp_[A-Za-z0-9]{20,}"
        "|\\bgithub_pat_[A-Za-z0-9_]{20,}|\\bAKIA[0-9A-Z]{16}|Bearer\\s+[A-Za-z0-9._-]{12,})"));
    static const QRegularExpression AssignmentPattern(QStringLiteral(
        "(?:api[_\\s-]?key|access[_\\s-]?token|secret[_\\s-]?key|authorization|password|passwd|密码|口令|密钥|秘钥)\\s*[:：=]\\s*\\S{4,}"),
        QRegularExpression::CaseInsensitiveOption);

    return TokenPattern.match(text).hasMatch() || AssignmentPattern.match(text).hasMatch();
}

QString AgentStatusCodec::sanitizeSummary(const QString &rawSummary)
{
    QString cleaned = rawSummary.simplified();
    if (cleaned.isEmpty())
    {
        return {};
    }

    if (containsSensitiveSecret(cleaned))
    {
        return {};
    }

    if (cleaned.size() > AgentStatusSummaryMaxChars)
    {
        cleaned = cleaned.left(AgentStatusSummaryMaxChars - 3).trimmed() + QStringLiteral("...");
    }
    return cleaned;
}

QString AgentStatusCodec::sanitizeActivityText(const QString &text)
{
    if (containsSensitiveSecret(text))
    {
        return QStringLiteral("已隐藏敏感信息");
    }
    return sanitizeSummary(text);
}

QString AgentStatusCodec::activityKind(const QString &tool)
{
    const QString name = tool.toLower();
    if (name.contains(QStringLiteral("command")) || name.contains(QStringLiteral("bash"))
        || name.contains(QStringLiteral("shell")) || name == QStringLiteral("exec"))
        return QStringLiteral("command");
    if (name.contains(QStringLiteral("search")) || name.contains(QStringLiteral("grep"))
        || name.contains(QStringLiteral("find")) || name.contains(QStringLiteral("glob")))
        return QStringLiteral("search");
    if (name.contains(QStringLiteral("edit")) || name.contains(QStringLiteral("write"))
        || name.contains(QStringLiteral("patch")) || name.contains(QStringLiteral("replace")))
        return QStringLiteral("edit");
    if (name.contains(QStringLiteral("read")) || name.contains(QStringLiteral("view"))
        || name.contains(QStringLiteral("open")))
        return QStringLiteral("read");
    return QStringLiteral("tool");
}

bool AgentStatusCodec::parsePacketJson(
    const QByteArray &payload,
    AgentStatusEvent *event,
    QString *errorMessage)
{
    if (event == nullptr)
    {
        return false;
    }

    if (payload.isEmpty())
    {
        return setCodecError(errorMessage, QStringLiteral("Status packet is empty."));
    }

    if (payload.size() > AgentStatusMaxPacketBytes)
    {
        return setCodecError(errorMessage, QStringLiteral("Status packet exceeds 16 KB limit."));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return setCodecError(errorMessage, QStringLiteral("Status packet is not valid JSON object."));
    }

    const QJsonObject root = document.object();
    const QJsonValue protocolValue = root.value(QStringLiteral("protocol"));
    int protocol = AgentStatusProtocolVersion;
    if (!protocolValue.isUndefined() && !protocolValue.isNull())
    {
        if (!protocolValue.isDouble())
        {
            return setCodecError(errorMessage, QStringLiteral("Protocol version must be an integer."));
        }
        protocol = protocolValue.toInt(0);
    }

    if (protocol != AgentStatusProtocolVersion)
    {
        return setCodecError(errorMessage, QStringLiteral("Unsupported protocol version."));
    }

    const QString rawState = root.value(QStringLiteral("state")).toString().trimmed();
    AgentState parsedState = AgentState::Idle;
    if (!normalizeState(rawState, &parsedState))
    {
        return setCodecError(errorMessage, QStringLiteral("Unrecognized agent state."));
    }

    AgentStatusEvent parsed;
    parsed.protocol = protocol;
    parsed.source = normalizeSource(root.value(QStringLiteral("source")).toString());
    parsed.sessionId = sanitizeSessionId(root.value(QStringLiteral("sessionId")).toString());
    if (parsed.sessionId.isEmpty())
    {
        parsed.sessionId = sanitizeSessionId(root.value(QStringLiteral("session_id")).toString());
    }
    parsed.workspace = extractWorkspaceDisplayName(root.value(QStringLiteral("workspace")).toString());
    parsed.state = parsedState;
    parsed.summary = sanitizeSummary(root.value(QStringLiteral("summary")).toString());
    parsed.title = sanitizeActivityText(root.value(QStringLiteral("title")).toString());
    parsed.turnId = sanitizeSessionId(root.value(QStringLiteral("turnId")).toString());
    parsed.eventKind = normalizedEventKind(root.value(QStringLiteral("eventKind")).toString());
    parsed.eventId = sanitizeSessionId(root.value(QStringLiteral("eventId")).toString());
    const QJsonObject activity = root.value(QStringLiteral("activity")).toObject();
    if (!activity.isEmpty())
    {
        parsed.eventKind = QStringLiteral("tool");
        parsed.activity.id = sanitizeSessionId(activity.value(QStringLiteral("id")).toString());
        parsed.activity.tool = sanitizeActivityText(activity.value(QStringLiteral("tool")).toString());
        parsed.activity.kind = activityKind(parsed.activity.tool);
        parsed.activity.summary = sanitizeActivityText(activity.value(QStringLiteral("summary")).toString());
        parsed.activity.phase = normalizedPhase(activity.value(QStringLiteral("phase")).toString());
    }

    const int rawTtl = root.value(QStringLiteral("ttlSec")).toInt(AgentStatusDefaultTtlSec);
    parsed.ttlSec = qBound(AgentStatusMinTtlSec, rawTtl, AgentStatusMaxTtlSec);

    *event = parsed;
    return true;
}

QByteArray AgentStatusCodec::serializeEventJson(const AgentStatusEvent &event)
{
    QJsonObject root;
    root.insert(QStringLiteral("protocol"), AgentStatusProtocolVersion);
    root.insert(QStringLiteral("source"), normalizeSource(event.source));
    if (!event.sessionId.trimmed().isEmpty())
    {
        root.insert(QStringLiteral("sessionId"), sanitizeSessionId(event.sessionId));
    }
    if (!event.workspace.trimmed().isEmpty())
    {
        root.insert(QStringLiteral("workspace"), extractWorkspaceDisplayName(event.workspace));
    }
    root.insert(QStringLiteral("state"), stateToWireString(event.state));
    const QString cleanedSummary = sanitizeSummary(event.summary);
    if (!cleanedSummary.isEmpty())
    {
        root.insert(QStringLiteral("summary"), cleanedSummary);
    }
    root.insert(QStringLiteral("ttlSec"), qBound(AgentStatusMinTtlSec, event.ttlSec, AgentStatusMaxTtlSec));
    root.insert(QStringLiteral("title"), sanitizeActivityText(event.title));
    root.insert(QStringLiteral("turnId"), sanitizeSessionId(event.turnId));
    root.insert(QStringLiteral("eventKind"), normalizedEventKind(event.eventKind));
    root.insert(QStringLiteral("eventId"), sanitizeSessionId(event.eventId));
    if (!event.activity.id.isEmpty() || !event.activity.tool.isEmpty() || !event.activity.summary.isEmpty())
    {
        QJsonObject activity;
        activity.insert(QStringLiteral("id"), sanitizeSessionId(event.activity.id));
        activity.insert(QStringLiteral("tool"), sanitizeActivityText(event.activity.tool));
        activity.insert(QStringLiteral("summary"), sanitizeActivityText(event.activity.summary));
        activity.insert(QStringLiteral("phase"), normalizedPhase(event.activity.phase));
        root.insert(QStringLiteral("activity"), activity);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void AgentStatusCodec::enrichEventFromHookStdin(
    AgentStatusEvent *event,
    const QByteArray &stdinBytes,
    bool explicitStateProvided)
{
    if (event == nullptr || stdinBytes.trimmed().isEmpty() || stdinBytes.size() > 32 * 1024)
    {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stdinBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return;
    }

    const QJsonObject root = document.object();
    QString hook = firstNonEmptyString(root, {"hook_event_name", "hookEventName", "event"});
    // Antigravity payloads carry stage-specific fields rather than an event name.
    if (hook.isEmpty() && root.contains(QStringLiteral("fullyIdle"))) hook = QStringLiteral("Stop");
    if (hook.isEmpty() && root.contains(QStringLiteral("invocationNum"))) hook = QStringLiteral("PreInvocation");
    event->turnId = sanitizeSessionId(firstNonEmptyString(root, {"turn_id", "turnId"}));
    event->title = sanitizeActivityText(firstNonEmptyString(root, {"conversationTitle", "session_title", "title"}));
    if (hook == QStringLiteral("SessionStart"))
        event->eventKind = QStringLiteral("session_start");
    else if (hook == QStringLiteral("UserPromptSubmit") || hook == QStringLiteral("PreInvocation"))
        event->eventKind = QStringLiteral("turn_start");
    else if (hook == QStringLiteral("Stop"))
        event->eventKind = QStringLiteral("turn_end");
    else if (hook == QStringLiteral("SessionEnd"))
        event->eventKind = QStringLiteral("session_end");
    else if (hook.contains(QStringLiteral("Permission")) || hook == QStringLiteral("Notification"))
        event->eventKind = QStringLiteral("attention");
    if (event->sessionId.trimmed().isEmpty())
    {
        event->sessionId = sanitizeSessionId(
            firstNonEmptyString(root, {"sessionId", "session_id", "conversationId", "conversation_id"}));
    }

    if (event->workspace.trimmed().isEmpty())
    {
        event->workspace = extractWorkspaceDisplayName(firstWorkspacePathFromJson(root));
    }

    if (event->summary.trimmed().isEmpty())
    {
        event->summary = sanitizeSummary(extractHookSummary(root));
    }

    const QString hookError = firstNonEmptyString(root, {"error", "errorMessage"});
    const QString terminationReason = firstNonEmptyString(root, {"terminationReason", "termination_reason"});
    const QString normalizedReason = terminationReason.toUpper();
    const bool cancelled = normalizedReason == QStringLiteral("USER_CANCELED")
        || normalizedReason == QStringLiteral("USER_CANCELLED")
        || normalizedReason == QStringLiteral("CANCELED")
        || normalizedReason == QStringLiteral("CANCELLED")
        || normalizedReason == QStringLiteral("INTERRUPTED");
    if (hook == QStringLiteral("Interrupt")
        || (event->source == QStringLiteral("codex") && hook == QStringLiteral("Stop") && cancelled))
    {
        event->eventKind = QStringLiteral("turn_pause");
        event->state = AgentState::NeedsInput;
        event->summary = QStringLiteral("已暂停，等待继续");
        return;
    }
    const QJsonObject toolCall = root.value(QStringLiteral("toolCall")).toObject();
    const QString toolName = firstNonEmptyString(root, {"tool_name", "toolName"});
    if (hook == QStringLiteral("PostToolUse") || hook == QStringLiteral("PreToolUse")
        || !toolCall.isEmpty())
    {
        event->eventKind = QStringLiteral("tool");
        event->activity.tool = sanitizeActivityText(toolName.isEmpty()
            ? firstNonEmptyString(toolCall, {"name", "toolName"}) : toolName);
        event->activity.kind = activityKind(event->activity.tool);
        event->activity.id = firstNonEmptyString(root, {"tool_use_id", "toolCallId", "callId"});
        if (event->activity.id.isEmpty())
            event->activity.id = firstNonEmptyString(toolCall, {"id", "callId"});
        if (event->activity.id.isEmpty() && root.contains(QStringLiteral("stepIdx")))
            event->activity.id = QStringLiteral("step:") + idValue(root, "stepIdx");
        event->activity.id = sanitizeSessionId(event->activity.id);
        QJsonObject input = inputObject(root.value(QStringLiteral("tool_input")));
        if (input.isEmpty()) input = inputObject(toolCall.value(QStringLiteral("args")));
        if (input.isEmpty()) input = inputObject(toolCall.value(QStringLiteral("arguments")));
        if (input.isEmpty()) input = inputObject(toolCall.value(QStringLiteral("input")));
        QString summary = firstNonEmptyString(input, {"command", "cmd", "CommandLine", "query", "SearchQuery", "pattern"});
        if (summary.isEmpty())
        {
            summary = extractWorkspaceDisplayName(firstNonEmptyString(input,
                {"file_path", "path", "TargetFile", "AbsolutePath", "SearchPath"}));
        }
        event->activity.summary = sanitizeActivityText(summary.isEmpty() ? event->activity.tool : summary);
        event->activity.phase = hook == QStringLiteral("PreToolUse")
            ? QStringLiteral("started") : QStringLiteral("finished");
        const QJsonObject response = inputObject(root.value(QStringLiteral("tool_response")));
        const bool failed = !hookError.isEmpty() || response.value(QStringLiteral("isError")).toBool()
            || response.value(QStringLiteral("is_error")).toBool()
            || (response.value(QStringLiteral("exit_code")).isDouble()
                && response.value(QStringLiteral("exit_code")).toInt() != 0);
        if (failed)
            event->activity.phase = QStringLiteral("failed");
        else if ((event->source == QStringLiteral("antigravity") && root.contains(QStringLiteral("error")))
                 || response.value(QStringLiteral("exit_code")).isDouble()
                 || response.value(QStringLiteral("success")).toBool())
            event->activity.phase = QStringLiteral("succeeded");
        // A failed tool is an activity failure, not a failed conversation.
        return;
    }

    if (cancelled && hookError.isEmpty())
    {
        event->eventKind = QStringLiteral("turn_end");
        event->state = AgentState::Idle;
        return;
    }
    if (hook == QStringLiteral("Stop") && root.contains(QStringLiteral("fullyIdle"))
        && !root.value(QStringLiteral("fullyIdle")).toBool(true) && hookError.isEmpty())
    {
        event->state = AgentState::Working;
        event->eventKind = QStringLiteral("attention");
        return;
    }
    if (!hookError.isEmpty() || terminationReason.compare(QStringLiteral("ERROR"), Qt::CaseInsensitive) == 0)
    {
        if (event->state == AgentState::Working || event->state == AgentState::Done || !explicitStateProvided)
        {
            event->state = AgentState::Error;
        }
        if (event->summary.trimmed().isEmpty() && !hookError.isEmpty())
        {
            event->summary = sanitizeSummary(hookError);
        }
        return;
    }

}
