#include "AgentStatusCodec.h"
#include "AgentStatusTypes.h"

#include <QByteArray>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdio>

namespace
{
constexpr int BridgeMaxStdinBytes = 32 * 1024;
constexpr int BridgeDefaultTimeoutMs = 250;
constexpr int BridgeMinTimeoutMs = 50;
constexpr int BridgeMaxTimeoutMs = 3000;

QByteArray readNonBlockingStdinIfPiped()
{
    QByteArray result;

#ifdef Q_OS_WIN
    const HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (stdinHandle == nullptr || stdinHandle == INVALID_HANDLE_VALUE)
    {
        return result;
    }

    const DWORD fileType = GetFileType(stdinHandle);
    if (fileType != FILE_TYPE_PIPE && fileType != FILE_TYPE_DISK)
    {
        return result;
    }

    QElapsedTimer deadline;
    deadline.start();
    char buffer[4096];
    while (result.size() < BridgeMaxStdinBytes)
    {
        if (fileType == FILE_TYPE_PIPE)
        {
            DWORD availableBytes = 0;
            if (!PeekNamedPipe(stdinHandle, nullptr, 0, nullptr, &availableBytes, nullptr))
            {
                break;
            }
            if (availableBytes == 0)
            {
                if (deadline.elapsed() >= 150) break;
                QThread::msleep(2);
                continue;
            }
        }

        const DWORD remaining = static_cast<DWORD>(BridgeMaxStdinBytes - result.size());
        const DWORD toRead = qMin<DWORD>(sizeof(buffer), remaining);
        DWORD bytesRead = 0;
        if (!ReadFile(stdinHandle, buffer, toRead, &bytesRead, nullptr) || bytesRead == 0)
        {
            break;
        }
        result.append(buffer, static_cast<int>(bytesRead));
        if (QJsonDocument::fromJson(result).isObject()) break;
    }
#else
    if (isatty(fileno(stdin)) != 0)
    {
        return result;
    }

    char buffer[4096];
    while (result.size() < BridgeMaxStdinBytes)
    {
        const size_t toRead = static_cast<size_t>(qMin<int>(sizeof(buffer), BridgeMaxStdinBytes - result.size()));
        const size_t bytesRead = std::fread(buffer, 1, toRead, stdin);
        if (bytesRead == 0)
        {
            break;
        }
        result.append(buffer, static_cast<int>(bytesRead));
    }
#endif

    return result;
}

void writeStdoutResponseIfRequested(const QString &stdoutJson)
{
    const QString trimmed = stdoutJson.trimmed();
    if (trimmed.isEmpty())
    {
        return;
    }

    const QByteArray output = trimmed.toUtf8() + '\n';
    std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
    std::fflush(stdout);
}

bool sendEventToDesktopPet(const AgentStatusEvent &event, const QString &pipeName, int timeoutMs)
{
    QByteArray payload = AgentStatusCodec::serializeEventJson(event);
    payload.append('\n');
    if (payload.size() > AgentStatusMaxPacketBytes)
    {
        return false;
    }

    QLocalSocket socket;
    socket.connectToServer(pipeName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(timeoutMs))
    {
        return false;
    }

    const qint64 written = socket.write(payload);
    if (written != payload.size())
    {
        socket.abort();
        return false;
    }

    socket.flush();
    while (socket.bytesToWrite() > 0)
    {
        if (!socket.waitForBytesWritten(timeoutMs))
        {
            break;
        }
    }
    const bool flushed = socket.bytesToWrite() == 0;
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
    {
        socket.waitForDisconnected(50);
    }
    return flushed;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("QtDesktopPetBridge"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PET_VERSION));
    // Avoid Qt's Windows message-box fallback when the launcher has no console.
    // Version probes must never show UI or contact the production Agent pipe.
    if (app.arguments().contains(QStringLiteral("--version")) || app.arguments().contains(QStringLiteral("-v")))
    {
        std::fputs("QtDesktopPetBridge " PET_VERSION "\n", stdout);
        std::fflush(stdout);
        return 0;
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lightweight status bridge for QtDesktopPet v1 agent IPC."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption sourceOption(
        QStringLiteral("source"),
        QStringLiteral("Agent source id (codex, antigravity, dsh, custom)."),
        QStringLiteral("source"),
        QStringLiteral("custom"));
    const QCommandLineOption stateOption(
        QStringLiteral("state"),
        QStringLiteral("Agent state (working, needs_input, done, error, idle)."),
        QStringLiteral("state"),
        QStringLiteral("working"));
    const QCommandLineOption sessionOption(
        QStringLiteral("session"),
        QStringLiteral("Optional session identifier."),
        QStringLiteral("sessionId"));
    const QCommandLineOption workspaceOption(
        QStringLiteral("workspace"),
        QStringLiteral("Optional workspace name or directory path."),
        QStringLiteral("workspace"));
    const QCommandLineOption summaryOption(
        QStringLiteral("summary"),
        QStringLiteral("Optional brief status summary."),
        QStringLiteral("summary"));
    const QCommandLineOption ttlOption(
        QStringLiteral("ttl"),
        QStringLiteral("Time-to-live in seconds (10..3600)."),
        QStringLiteral("seconds"),
        QString::number(AgentStatusDefaultTtlSec));
    const QCommandLineOption pipeOption(
        QStringLiteral("pipe"),
        QStringLiteral("Target local IPC pipe name."),
        QStringLiteral("pipeName"),
        defaultAgentStatusPipeName());
    const QCommandLineOption stdinSniffOption(
        QStringLiteral("stdin-sniff"),
        QStringLiteral("Attempt best-effort metadata extraction from piped JSON stdin."));
    const QCommandLineOption packetStdinOption(
        QStringLiteral("packet-stdin"),
        QStringLiteral("Read a complete normalized v1 status/activity packet from stdin."));
    const QCommandLineOption stdoutJsonOption(
        QStringLiteral("stdout-json"),
        QStringLiteral("Write JSON payload to stdout before exit (for hooks requiring JSON output)."),
        QStringLiteral("json"));
    const QCommandLineOption timeoutOption(
        QStringLiteral("timeout-ms"),
        QStringLiteral("IPC connect/write timeout in milliseconds (50..3000)."),
        QStringLiteral("ms"),
        QString::number(BridgeDefaultTimeoutMs));
    const QCommandLineOption strictOption(
        QStringLiteral("strict"),
        QStringLiteral("Return non-zero exit code if QtDesktopPet IPC server is unreachable."));

    parser.addOption(sourceOption);
    parser.addOption(stateOption);
    parser.addOption(sessionOption);
    parser.addOption(workspaceOption);
    parser.addOption(summaryOption);
    parser.addOption(ttlOption);
    parser.addOption(pipeOption);
    parser.addOption(stdinSniffOption);
    parser.addOption(packetStdinOption);
    parser.addOption(stdoutJsonOption);
    parser.addOption(timeoutOption);
    parser.addOption(strictOption);
    parser.process(app);

    const bool strictMode = parser.isSet(strictOption);
    const QString stdoutJson = parser.value(stdoutJsonOption);

    AgentStatusEvent event;
    event.protocol = AgentStatusProtocolVersion;
    event.source = AgentStatusCodec::normalizeSource(parser.value(sourceOption));
    event.sessionId = AgentStatusCodec::sanitizeSessionId(parser.value(sessionOption));
    event.workspace = AgentStatusCodec::extractWorkspaceDisplayName(parser.value(workspaceOption));
    event.summary = AgentStatusCodec::sanitizeSummary(parser.value(summaryOption));

    bool ttlOk = false;
    const int parsedTtl = parser.value(ttlOption).toInt(&ttlOk);
    event.ttlSec = ttlOk
        ? qBound(AgentStatusMinTtlSec, parsedTtl, AgentStatusMaxTtlSec)
        : AgentStatusDefaultTtlSec;

    const bool explicitStateProvided = parser.isSet(stateOption);
    AgentState parsedState = AgentState::Working;
    if (!AgentStatusCodec::normalizeState(parser.value(stateOption), &parsedState))
    {
        parsedState = AgentState::Working;
    }
    event.state = parsedState;

    if (parser.isSet(packetStdinOption))
    {
        if (!AgentStatusCodec::parsePacketJson(readNonBlockingStdinIfPiped(), &event))
        {
            writeStdoutResponseIfRequested(stdoutJson);
            return 2;
        }
    }
    else if (parser.isSet(stdinSniffOption))
    {
        const QByteArray stdinBytes = readNonBlockingStdinIfPiped();
        if (!stdinBytes.isEmpty())
        {
            AgentStatusCodec::enrichEventFromHookStdin(&event, stdinBytes, explicitStateProvided);
        }
    }

    bool timeoutOk = false;
    const int parsedTimeout = parser.value(timeoutOption).toInt(&timeoutOk);
    const int timeoutMs = timeoutOk
        ? qBound(BridgeMinTimeoutMs, parsedTimeout, BridgeMaxTimeoutMs)
        : BridgeDefaultTimeoutMs;

    const QString targetPipe = parser.value(pipeOption).trimmed().isEmpty()
        ? defaultAgentStatusPipeName()
        : parser.value(pipeOption).trimmed();

    const bool delivered = sendEventToDesktopPet(event, targetPipe, timeoutMs);
    writeStdoutResponseIfRequested(stdoutJson);

    if (strictMode && !delivered)
    {
        return 1;
    }

    return 0;
}
