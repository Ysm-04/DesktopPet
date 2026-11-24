#pragma once

#include <QString>
#include <QStringList>

enum class AgentHookTarget
{
    Codex = 0,
    Antigravity = 1,
};

enum class AgentHookInstallState
{
    NotDetected = 0,
    NotInstalled = 1,
    Installed = 2,
    PathMismatch = 3,
    ConfigInvalid = 4,
};

struct AgentHookStatus
{
    AgentHookTarget target = AgentHookTarget::Codex;
    AgentHookInstallState state = AgentHookInstallState::NotDetected;
    QString targetDir;
    QString configPath;
    QString configuredBridgePath;
    QString detail;
};

struct AgentHookOperationResult
{
    bool success = false;
    QString configPath;
    QString backupPath;
    QString message;
};

class AgentHookManager
{
public:
    static QString targetDisplayName(AgentHookTarget target);
    static QString defaultBridgeExecutablePath(const QString &appDirOverride = QString());
    static QString resolveCodexDirectory(
        const QString &codexHomeEnvValue,
        const QString &homeDirOverride = QString());
    static QString resolveTargetDirectory(AgentHookTarget target, const QString &homeDirOverride = QString());
    static QString resolveConfigPath(AgentHookTarget target, const QString &homeDirOverride = QString());

    static QString buildSafeCommand(const QString &bridgeExePath, const QStringList &arguments);
    static QString extractBridgeExecutablePath(const QString &command);

    static AgentHookStatus inspect(
        AgentHookTarget target,
        const QString &expectedBridgeExePath,
        const QString &homeDirOverride = QString());

    static AgentHookOperationResult installOrUpdate(
        AgentHookTarget target,
        const QString &bridgeExePath,
        const QString &homeDirOverride = QString());

    static AgentHookOperationResult uninstall(
        AgentHookTarget target,
        const QString &homeDirOverride = QString());
};
