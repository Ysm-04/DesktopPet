#include "AgentHookManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

namespace
{
constexpr auto AntigravityHookKey = "qt-desktop-pet-status";
constexpr auto BridgeExeFileName = "QtDesktopPetBridge.exe";

QString effectiveHomePath(const QString &homeDirOverride)
{
    const QString trimmed = homeDirOverride.trimmed();
    if (!trimmed.isEmpty())
    {
        return QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
    }
    return QDir::cleanPath(QDir::homePath());
}

QString normalizeComparablePath(const QString &rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    QString forward = QDir::fromNativeSeparators(trimmed);
    const QFileInfo info(forward);
    if (info.isAbsolute())
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    return QDir::cleanPath(forward);
}

bool pathsEqual(const QString &left, const QString &right)
{
    const QString cleanLeft = normalizeComparablePath(left);
    const QString cleanRight = normalizeComparablePath(right);
    if (cleanLeft.isEmpty() || cleanRight.isEmpty())
    {
        return false;
    }

#ifdef Q_OS_WIN
    return cleanLeft.compare(cleanRight, Qt::CaseInsensitive) == 0;
#else
    return cleanLeft.compare(cleanRight, Qt::CaseSensitive) == 0;
#endif
}

bool needsCmdShellQuoting(const QString &value)
{
    for (const QChar ch : value)
    {
        if (ch.isSpace()
            || ch == QLatin1Char('"')
            || ch == QLatin1Char('&')
            || ch == QLatin1Char('|')
            || ch == QLatin1Char('^')
            || ch == QLatin1Char('<')
            || ch == QLatin1Char('>')
            || ch == QLatin1Char('(')
            || ch == QLatin1Char(')')
            || ch == QLatin1Char('%')
            || ch == QLatin1Char('!')
            || ch == QLatin1Char('{')
            || ch == QLatin1Char('}'))
        {
            return true;
        }
    }
    return false;
}

QString quoteCliArgument(const QString &arg)
{
    if (!arg.isEmpty() && !needsCmdShellQuoting(arg))
    {
        return arg;
    }

    QString escaped = arg;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

bool isOurBridgeCommandForTarget(AgentHookTarget target, const QString &command)
{
    if (!command.contains(QLatin1String(BridgeExeFileName), Qt::CaseInsensitive))
    {
        return false;
    }

    if (target == AgentHookTarget::Codex)
    {
        return command.contains(QStringLiteral("--source codex"), Qt::CaseInsensitive)
            || command.contains(QStringLiteral("--source \"codex\""), Qt::CaseInsensitive);
    }

    return command.contains(QStringLiteral("--source antigravity"), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("--source \"antigravity\""), Qt::CaseInsensitive)
        || command.contains(QStringLiteral("--source agy"), Qt::CaseInsensitive);
}

QJsonObject buildCommandHandlerObject(const QString &command, int timeoutSec = 5)
{
    QJsonObject handler;
    handler.insert(QStringLiteral("type"), QStringLiteral("command"));
    handler.insert(QStringLiteral("command"), command);
    handler.insert(QStringLiteral("timeout"), timeoutSec);
    return handler;
}

QJsonObject buildAntigravityHookEntry(const QString &bridgeExePath)
{
    const QString startCmd = AgentHookManager::buildSafeCommand(
        bridgeExePath,
        {
            QStringLiteral("--source"), QStringLiteral("antigravity"),
            QStringLiteral("--state"), QStringLiteral("working"),
            QStringLiteral("--summary"), QStringLiteral("Antigravity session started"),
            QStringLiteral("--stdin-sniff"),
            QStringLiteral("--stdout-json"), QStringLiteral("{}"),
        });
    const QString preInvokeCmd = AgentHookManager::buildSafeCommand(
        bridgeExePath,
        {
            QStringLiteral("--source"), QStringLiteral("antigravity"),
            QStringLiteral("--state"), QStringLiteral("working"),
            QStringLiteral("--summary"), QStringLiteral("Antigravity thinking"),
            QStringLiteral("--stdin-sniff"),
            QStringLiteral("--stdout-json"), QStringLiteral("{}"),
        });
    const QString postToolCmd = AgentHookManager::buildSafeCommand(
        bridgeExePath,
        {
            QStringLiteral("--source"), QStringLiteral("antigravity"),
            QStringLiteral("--state"), QStringLiteral("working"),
            QStringLiteral("--stdin-sniff"),
            QStringLiteral("--stdout-json"), QStringLiteral("{}"),
        });
    const QString stopCmd = AgentHookManager::buildSafeCommand(
        bridgeExePath,
        {
            QStringLiteral("--source"), QStringLiteral("antigravity"),
            QStringLiteral("--state"), QStringLiteral("done"),
            QStringLiteral("--stdin-sniff"),
            QStringLiteral("--stdout-json"), QStringLiteral("{}"),
        });

    QJsonObject postToolGroup;
    postToolGroup.insert(QStringLiteral("matcher"), QStringLiteral("*"));
    postToolGroup.insert(QStringLiteral("hooks"), QJsonArray{buildCommandHandlerObject(postToolCmd)});

    // 刻意不挂载 PreToolUse，避免 Hook 响应绕过 Antigravity 工具权限审批门禁
    QJsonObject entry;
    entry.insert(QStringLiteral("enabled"), true);
    entry.insert(QStringLiteral("SessionStart"), QJsonArray{buildCommandHandlerObject(startCmd)});
    entry.insert(QStringLiteral("PreInvocation"), QJsonArray{buildCommandHandlerObject(preInvokeCmd)});
    entry.insert(QStringLiteral("PostToolUse"), QJsonArray{postToolGroup});
    entry.insert(QStringLiteral("Stop"), QJsonArray{buildCommandHandlerObject(stopCmd)});
    return entry;
}

QJsonArray stripCodexBridgeFromHandlersArray(const QJsonArray &handlers, bool *removedAny = nullptr)
{
    QJsonArray kept;
    for (const QJsonValue &item : handlers)
    {
        if (item.isObject())
        {
            const QString cmd = item.toObject().value(QStringLiteral("command")).toString();
            if (isOurBridgeCommandForTarget(AgentHookTarget::Codex, cmd))
            {
                if (removedAny != nullptr)
                {
                    *removedAny = true;
                }
                continue;
            }
        }
        kept.append(item);
    }
    return kept;
}

QJsonArray stripCodexBridgeFromEventGroups(const QJsonArray &groups, bool *removedAny = nullptr)
{
    QJsonArray keptGroups;
    for (const QJsonValue &groupValue : groups)
    {
        if (!groupValue.isObject())
        {
            keptGroups.append(groupValue);
            continue;
        }

        QJsonObject groupObj = groupValue.toObject();
        const QJsonValue innerHooksVal = groupObj.value(QStringLiteral("hooks"));
        if (!innerHooksVal.isArray())
        {
            const QString directCmd = groupObj.value(QStringLiteral("command")).toString();
            if (isOurBridgeCommandForTarget(AgentHookTarget::Codex, directCmd))
            {
                if (removedAny != nullptr)
                {
                    *removedAny = true;
                }
                continue;
            }
            keptGroups.append(groupObj);
            continue;
        }

        const QJsonArray filteredHandlers = stripCodexBridgeFromHandlersArray(innerHooksVal.toArray(), removedAny);
        if (filteredHandlers.isEmpty())
        {
            continue;
        }
        groupObj.insert(QStringLiteral("hooks"), filteredHandlers);
        keptGroups.append(groupObj);
    }
    return keptGroups;
}

void collectCodexBridgeCommands(const QJsonObject &hooksObj, QStringList *commandsOut)
{
    for (auto it = hooksObj.constBegin(); it != hooksObj.constEnd(); ++it)
    {
        if (!it.value().isArray())
        {
            continue;
        }

        const QJsonArray groups = it.value().toArray();
        for (const QJsonValue &groupVal : groups)
        {
            if (!groupVal.isObject())
            {
                continue;
            }
            const QJsonObject groupObj = groupVal.toObject();
            const QString directCmd = groupObj.value(QStringLiteral("command")).toString();
            if (isOurBridgeCommandForTarget(AgentHookTarget::Codex, directCmd))
            {
                commandsOut->append(directCmd);
            }
            const QJsonValue innerVal = groupObj.value(QStringLiteral("hooks"));
            if (innerVal.isArray())
            {
                for (const QJsonValue &handlerVal : innerVal.toArray())
                {
                    if (!handlerVal.isObject())
                    {
                        continue;
                    }
                    const QString cmd = handlerVal.toObject().value(QStringLiteral("command")).toString();
                    if (isOurBridgeCommandForTarget(AgentHookTarget::Codex, cmd))
                    {
                        commandsOut->append(cmd);
                    }
                }
            }
        }
    }
}

void collectAntigravityBridgeCommands(const QJsonObject &entryObj, QStringList *commandsOut)
{
    for (auto it = entryObj.constBegin(); it != entryObj.constEnd(); ++it)
    {
        if (!it.value().isArray())
        {
            continue;
        }
        for (const QJsonValue &item : it.value().toArray())
        {
            if (!item.isObject())
            {
                continue;
            }
            const QJsonObject obj = item.toObject();
            const QString directCmd = obj.value(QStringLiteral("command")).toString();
            if (!directCmd.isEmpty() && directCmd.contains(QLatin1String(BridgeExeFileName), Qt::CaseInsensitive))
            {
                commandsOut->append(directCmd);
            }
            const QJsonValue nested = obj.value(QStringLiteral("hooks"));
            if (nested.isArray())
            {
                for (const QJsonValue &nestedItem : nested.toArray())
                {
                    if (!nestedItem.isObject())
                    {
                        continue;
                    }
                    const QString nestedCmd = nestedItem.toObject().value(QStringLiteral("command")).toString();
                    if (!nestedCmd.isEmpty()
                        && nestedCmd.contains(QLatin1String(BridgeExeFileName), Qt::CaseInsensitive))
                    {
                        commandsOut->append(nestedCmd);
                    }
                }
            }
        }
    }
}

void appendCodexEventGroup(
    QJsonObject *hooksObj,
    const QString &eventName,
    const QString &matcher,
    const QString &command,
    int timeoutSec = 5)
{
    QJsonArray groups = hooksObj->value(eventName).toArray();
    QJsonObject newGroup;
    if (!matcher.isEmpty())
    {
        newGroup.insert(QStringLiteral("matcher"), matcher);
    }
    newGroup.insert(QStringLiteral("hooks"), QJsonArray{buildCommandHandlerObject(command, timeoutSec)});
    groups.append(newGroup);
    hooksObj->insert(eventName, groups);
}

bool readJsonObjectFile(const QString &filePath, QJsonObject *rootOut, bool *existsOut, QString *errorMessage)
{
    *existsOut = QFileInfo::exists(filePath);
    if (!*existsOut)
    {
        *rootOut = QJsonObject();
        return true;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取配置文件：%1").arg(file.errorString());
        }
        return false;
    }

    const QByteArray raw = file.readAll();
    file.close();
    if (raw.trimmed().isEmpty())
    {
        *rootOut = QJsonObject();
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("配置文件不是合法的 JSON 对象（%1），为避免损坏原配置已中止修改。")
                .arg(parseError.errorString());
        }
        return false;
    }

    *rootOut = doc.object();
    return true;
}

bool createBackupIfFileExists(const QString &filePath, QString *backupPathOut, QString *errorMessage)
{
    backupPathOut->clear();
    if (!QFileInfo::exists(filePath))
    {
        return true;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString candidate = filePath + QStringLiteral(".bak.") + stamp;
    int suffix = 1;
    while (QFileInfo::exists(candidate))
    {
        candidate = QStringLiteral("%1.bak.%2-%3").arg(filePath, stamp).arg(suffix);
        ++suffix;
    }

    if (!QFile::copy(filePath, candidate))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("创建备份文件失败：%1").arg(QDir::toNativeSeparators(candidate));
        }
        return false;
    }

    *backupPathOut = candidate;
    return true;
}

bool writeJsonAtomically(const QString &filePath, const QJsonObject &rootObj, QString *errorMessage)
{
    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath()))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建配置目录：%1")
                .arg(QDir::toNativeSeparators(info.absolutePath()));
        }
        return false;
    }

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入配置文件：%1").arg(saveFile.errorString());
        }
        return false;
    }

    const QByteArray encoded = QJsonDocument(rootObj).toJson(QJsonDocument::Indented);
    if (saveFile.write(encoded) != encoded.size() || !saveFile.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交配置文件写入失败：%1").arg(saveFile.errorString());
        }
        saveFile.cancelWriting();
        return false;
    }

    return true;
}
}

QString AgentHookManager::targetDisplayName(AgentHookTarget target)
{
    switch (target)
    {
    case AgentHookTarget::Codex:
        return QStringLiteral("Codex");
    case AgentHookTarget::Antigravity:
        return QStringLiteral("Antigravity");
    }
    return QStringLiteral("Agent");
}

QString AgentHookManager::defaultBridgeExecutablePath(const QString &appDirOverride)
{
    const QString baseDir = appDirOverride.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : QDir::cleanPath(QDir::fromNativeSeparators(appDirOverride.trimmed()));
    if (baseDir.isEmpty())
    {
        return {};
    }

    return QDir::toNativeSeparators(QDir(baseDir).filePath(QLatin1String(BridgeExeFileName)));
}

QString AgentHookManager::resolveCodexDirectory(
    const QString &codexHomeEnvValue,
    const QString &homeDirOverride)
{
    const QString trimmedEnv = codexHomeEnvValue.trimmed();
    if (!trimmedEnv.isEmpty())
    {
        return QDir::cleanPath(QDir::fromNativeSeparators(trimmedEnv));
    }

    return QDir::cleanPath(effectiveHomePath(homeDirOverride) + QStringLiteral("/.codex"));
}

QString AgentHookManager::resolveTargetDirectory(AgentHookTarget target, const QString &homeDirOverride)
{
    const QString home = effectiveHomePath(homeDirOverride);
    switch (target)
    {
    case AgentHookTarget::Codex:
    {
        // 传入 homeDirOverride 时说明处于测试隔离模式，忽略宿主机 CODEX_HOME 防污染真实环境
        const QString codexHomeEnv = homeDirOverride.trimmed().isEmpty()
            ? qEnvironmentVariable("CODEX_HOME")
            : QString();
        return resolveCodexDirectory(codexHomeEnv, homeDirOverride);
    }
    case AgentHookTarget::Antigravity:
        return QDir::cleanPath(home + QStringLiteral("/.gemini/config"));
    }
    return QDir::cleanPath(home);
}

QString AgentHookManager::resolveConfigPath(AgentHookTarget target, const QString &homeDirOverride)
{
    return QDir::cleanPath(resolveTargetDirectory(target, homeDirOverride) + QStringLiteral("/hooks.json"));
}

QString AgentHookManager::buildSafeCommand(const QString &bridgeExePath, const QStringList &arguments)
{
    const QString nativeExe = QDir::toNativeSeparators(normalizeComparablePath(bridgeExePath));
    QStringList formattedArgs;
    formattedArgs.reserve(arguments.size());
    for (const QString &arg : arguments)
    {
        formattedArgs.append(quoteCliArgument(arg));
    }

    const QString argTail = formattedArgs.join(QLatin1Char(' '));
    const bool exeHasSpecialChars = needsCmdShellQuoting(nativeExe);
    if (!exeHasSpecialChars)
    {
        return argTail.isEmpty() ? nativeExe : (nativeExe + QLatin1Char(' ') + argTail);
    }

    // Windows cmd.exe /c 当整条命令以 '"' 开头且后续还包含其它 '"' 时会剥除最外层一对引号；
    // 在带空格路径外再包一层双引号，可同时兼容含空格路径与多个带引号参数。
    const QString inner = QLatin1Char('"') + nativeExe + QLatin1Char('"')
        + (argTail.isEmpty() ? QString() : (QLatin1Char(' ') + argTail));
    return QLatin1Char('"') + inner + QLatin1Char('"');
}

QString AgentHookManager::extractBridgeExecutablePath(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QLatin1String exeToken(BridgeExeFileName);
    const int exeIndex = trimmed.indexOf(exeToken, 0, Qt::CaseInsensitive);
    if (exeIndex < 0)
    {
        return {};
    }

    const int exeEnd = exeIndex + exeToken.size();
    int quoteStart = -1;
    for (int i = exeIndex - 1; i >= 0; --i)
    {
        if (trimmed.at(i) == QLatin1Char('"'))
        {
            quoteStart = i;
            break;
        }
    }

    if (quoteStart >= 0)
    {
        const QString prefixBeforeQuote = trimmed.left(quoteStart).trimmed();
        const bool validQuotePrefix = prefixBeforeQuote.isEmpty()
            || prefixBeforeQuote == QStringLiteral("\"")
            || prefixBeforeQuote.compare(QStringLiteral("call"), Qt::CaseInsensitive) == 0;
        if (validQuotePrefix)
        {
            return trimmed.mid(quoteStart + 1, exeEnd - (quoteStart + 1)).trimmed();
        }
    }

    int tokenStart = exeIndex;
    while (tokenStart > 0 && !trimmed.at(tokenStart - 1).isSpace() && trimmed.at(tokenStart - 1) != QLatin1Char('"'))
    {
        --tokenStart;
    }

    return trimmed.mid(tokenStart, exeEnd - tokenStart).trimmed();
}

AgentHookStatus AgentHookManager::inspect(
    AgentHookTarget target,
    const QString &expectedBridgeExePath,
    const QString &homeDirOverride)
{
    AgentHookStatus status;
    status.target = target;
    const QString targetDir = resolveTargetDirectory(target, homeDirOverride);
    const QString configPath = resolveConfigPath(target, homeDirOverride);
    status.targetDir = QDir::toNativeSeparators(targetDir);
    status.configPath = QDir::toNativeSeparators(configPath);

    const QString home = effectiveHomePath(homeDirOverride);
    const bool toolDetected = (target == AgentHookTarget::Codex)
        ? QDir(targetDir).exists()
        : (QDir(targetDir).exists()
           || QDir(home + QStringLiteral("/.gemini/antigravity")).exists()
           || QDir(home + QStringLiteral("/.gemini")).exists());

    QJsonObject rootObj;
    bool fileExists = false;
    QString readError;
    if (!readJsonObjectFile(configPath, &rootObj, &fileExists, &readError))
    {
        status.state = AgentHookInstallState::ConfigInvalid;
        status.detail = readError;
        return status;
    }

    if (!fileExists)
    {
        status.state = toolDetected ? AgentHookInstallState::NotInstalled : AgentHookInstallState::NotDetected;
        status.detail = toolDetected
            ? QStringLiteral("检测到 %1 配置目录，尚未写入 hooks.json。").arg(targetDisplayName(target))
            : QStringLiteral("未检测到本机 %1 配置目录，仍可点击一键接入主动创建。").arg(targetDisplayName(target));
        return status;
    }

    QStringList matchedCommands;
    bool entryDisabled = false;
    if (target == AgentHookTarget::Antigravity)
    {
        const QJsonValue entryVal = rootObj.value(QLatin1String(AntigravityHookKey));
        if (entryVal.isObject())
        {
            const QJsonObject entryObj = entryVal.toObject();
            if (entryObj.contains(QStringLiteral("enabled")) && !entryObj.value(QStringLiteral("enabled")).toBool(true))
            {
                entryDisabled = true;
            }
            collectAntigravityBridgeCommands(entryObj, &matchedCommands);
        }
    }
    else
    {
        const QJsonValue hooksVal = rootObj.value(QStringLiteral("hooks"));
        if (hooksVal.isObject())
        {
            collectCodexBridgeCommands(hooksVal.toObject(), &matchedCommands);
        }
    }

    if (matchedCommands.isEmpty())
    {
        status.state = AgentHookInstallState::NotInstalled;
        status.detail = QStringLiteral("hooks.json 已存在，但尚未接入 QtDesktopPetBridge。");
        return status;
    }

    const QString expectedNormalized = normalizeComparablePath(expectedBridgeExePath);
    QString firstExtractedPath;
    bool allMatchExpected = !expectedNormalized.isEmpty();
    for (const QString &cmd : matchedCommands)
    {
        const QString extracted = extractBridgeExecutablePath(cmd);
        if (firstExtractedPath.isEmpty() && !extracted.isEmpty())
        {
            firstExtractedPath = QDir::toNativeSeparators(extracted);
        }
        if (extracted.isEmpty() || !pathsEqual(extracted, expectedNormalized))
        {
            allMatchExpected = false;
        }
    }

    status.configuredBridgePath = firstExtractedPath;
    if (entryDisabled)
    {
        status.state = AgentHookInstallState::PathMismatch;
        status.detail = QStringLiteral("检测到 %1 Hook 已配置但当前处于 disabled 状态，可点击修复启用。")
            .arg(targetDisplayName(target));
        return status;
    }

    if (!allMatchExpected)
    {
        status.state = AgentHookInstallState::PathMismatch;
        status.detail = QStringLiteral("Hook 指向的桥接路径与当前程序不一致（%1），建议点击修复路径。")
            .arg(firstExtractedPath.isEmpty() ? QStringLiteral("未知路径") : firstExtractedPath);
        return status;
    }

    if (!QFileInfo::exists(expectedNormalized))
    {
        status.state = AgentHookInstallState::PathMismatch;
        status.detail = QStringLiteral("当前目录缺少 QtDesktopPetBridge.exe，请先同时构建或放置该文件。");
        return status;
    }

    status.state = AgentHookInstallState::Installed;
    status.detail = QStringLiteral("已接入当前目录的 QtDesktopPetBridge.exe。");
    if (target == AgentHookTarget::Codex)
    {
        QStringList interruptCommands;
        const QJsonObject hooks = rootObj.value(QStringLiteral("hooks")).toObject();
        collectCodexBridgeCommands(QJsonObject{{QStringLiteral("Interrupt"), hooks.value(QStringLiteral("Interrupt"))}},
                                  &interruptCommands);
        if (interruptCommands.isEmpty())
        {
            status.detail += QStringLiteral(" 尚缺 Interrupt 中断通知；支持该事件的客户端可点击修复补充，并重新信任。");
        }
    }
    return status;
}

AgentHookOperationResult AgentHookManager::installOrUpdate(
    AgentHookTarget target,
    const QString &bridgeExePath,
    const QString &homeDirOverride)
{
    AgentHookOperationResult result;
    const QString configPath = resolveConfigPath(target, homeDirOverride);
    result.configPath = QDir::toNativeSeparators(configPath);

    const QString normalizedBridge = normalizeComparablePath(bridgeExePath);
    if (normalizedBridge.isEmpty() || !QFileInfo::exists(normalizedBridge))
    {
        result.message = QStringLiteral("未找到可用的 QtDesktopPetBridge.exe：%1")
            .arg(QDir::toNativeSeparators(bridgeExePath));
        return result;
    }

    QJsonObject rootObj;
    bool fileExists = false;
    QString readError;
    if (!readJsonObjectFile(configPath, &rootObj, &fileExists, &readError))
    {
        result.message = readError;
        return result;
    }

    QString backupError;
    if (fileExists && !createBackupIfFileExists(configPath, &result.backupPath, &backupError))
    {
        result.message = backupError;
        return result;
    }

    if (target == AgentHookTarget::Antigravity)
    {
        rootObj.insert(QLatin1String(AntigravityHookKey), buildAntigravityHookEntry(normalizedBridge));
    }
    else
    {
        QJsonObject hooksObj = rootObj.value(QStringLiteral("hooks")).toObject();
        const QStringList managedEvents = {
            QStringLiteral("SessionStart"),
            QStringLiteral("UserPromptSubmit"),
            QStringLiteral("PostToolUse"),
            QStringLiteral("Stop"),
            QStringLiteral("Interrupt"),
        };
        for (const QString &eventName : managedEvents)
        {
            if (hooksObj.value(eventName).isArray())
            {
                const QJsonArray cleaned = stripCodexBridgeFromEventGroups(hooksObj.value(eventName).toArray());
                if (cleaned.isEmpty())
                {
                    hooksObj.remove(eventName);
                }
                else
                {
                    hooksObj.insert(eventName, cleaned);
                }
            }
        }

        const QString startCmd = buildSafeCommand(
            normalizedBridge,
            {
                QStringLiteral("--source"), QStringLiteral("codex"),
                QStringLiteral("--state"), QStringLiteral("working"),
                QStringLiteral("--summary"), QStringLiteral("Codex session started"),
                QStringLiteral("--stdin-sniff"),
            });
        const QString promptCmd = buildSafeCommand(
            normalizedBridge,
            {
                QStringLiteral("--source"), QStringLiteral("codex"),
                QStringLiteral("--state"), QStringLiteral("working"),
                QStringLiteral("--summary"), QStringLiteral("Codex thinking"),
                QStringLiteral("--stdin-sniff"),
            });
        const QString postToolCmd = buildSafeCommand(
            normalizedBridge,
            {
                QStringLiteral("--source"), QStringLiteral("codex"),
                QStringLiteral("--state"), QStringLiteral("working"),
                QStringLiteral("--stdin-sniff"),
            });
        const QString stopCmd = buildSafeCommand(
            normalizedBridge,
            {
                QStringLiteral("--source"), QStringLiteral("codex"),
                QStringLiteral("--state"), QStringLiteral("done"),
                QStringLiteral("--stdin-sniff"),
            });
        const QString interruptCmd = buildSafeCommand(
            normalizedBridge,
            {
                QStringLiteral("--source"), QStringLiteral("codex"),
                QStringLiteral("--state"), QStringLiteral("needs_input"),
                QStringLiteral("--summary"), QStringLiteral("Codex paused"),
                QStringLiteral("--stdin-sniff"),
            });

        appendCodexEventGroup(&hooksObj, QStringLiteral("SessionStart"), QString(), startCmd);
        appendCodexEventGroup(&hooksObj, QStringLiteral("UserPromptSubmit"), QString(), promptCmd);
        appendCodexEventGroup(
            &hooksObj,
            QStringLiteral("PostToolUse"),
            QStringLiteral("*"),
            postToolCmd);
        appendCodexEventGroup(&hooksObj, QStringLiteral("Stop"), QString(), stopCmd);
        appendCodexEventGroup(&hooksObj, QStringLiteral("Interrupt"), QString(), interruptCmd, 1);
        rootObj.insert(QStringLiteral("hooks"), hooksObj);
    }

    QString writeError;
    if (!writeJsonAtomically(configPath, rootObj, &writeError))
    {
        result.message = writeError;
        return result;
    }

    result.backupPath = QDir::toNativeSeparators(result.backupPath);
    result.success = true;
    result.message = QStringLiteral("已写入 %1 Hook 配置（%2）。新开对话或重启 %1 客户端后即可生效。")
        .arg(targetDisplayName(target), result.configPath);
    return result;
}

AgentHookOperationResult AgentHookManager::uninstall(
    AgentHookTarget target,
    const QString &homeDirOverride)
{
    AgentHookOperationResult result;
    const QString configPath = resolveConfigPath(target, homeDirOverride);
    result.configPath = QDir::toNativeSeparators(configPath);

    QJsonObject rootObj;
    bool fileExists = false;
    QString readError;
    if (!readJsonObjectFile(configPath, &rootObj, &fileExists, &readError))
    {
        result.message = readError;
        return result;
    }

    if (!fileExists)
    {
        result.success = true;
        result.message = QStringLiteral("未找到 %1 配置文件，无需解除接入。").arg(targetDisplayName(target));
        return result;
    }

    bool changed = false;
    if (target == AgentHookTarget::Antigravity)
    {
        if (rootObj.contains(QLatin1String(AntigravityHookKey)))
        {
            rootObj.remove(QLatin1String(AntigravityHookKey));
            changed = true;
        }
    }
    else
    {
        if (rootObj.value(QStringLiteral("hooks")).isObject())
        {
            QJsonObject hooksObj = rootObj.value(QStringLiteral("hooks")).toObject();
            const QStringList eventKeys = hooksObj.keys();
            for (const QString &eventKey : eventKeys)
            {
                if (!hooksObj.value(eventKey).isArray())
                {
                    continue;
                }
                bool eventRemoved = false;
                const QJsonArray cleaned = stripCodexBridgeFromEventGroups(hooksObj.value(eventKey).toArray(), &eventRemoved);
                if (eventRemoved)
                {
                    changed = true;
                    if (cleaned.isEmpty())
                    {
                        hooksObj.remove(eventKey);
                    }
                    else
                    {
                        hooksObj.insert(eventKey, cleaned);
                    }
                }
            }
            if (changed)
            {
                if (hooksObj.isEmpty())
                {
                    rootObj.remove(QStringLiteral("hooks"));
                }
                else
                {
                    rootObj.insert(QStringLiteral("hooks"), hooksObj);
                }
            }
        }
    }

    if (!changed)
    {
        result.success = true;
        result.message = QStringLiteral("%1 配置中已不包含 QtDesktopPetBridge 条目。").arg(targetDisplayName(target));
        return result;
    }

    QString backupError;
    if (!createBackupIfFileExists(configPath, &result.backupPath, &backupError))
    {
        result.message = backupError;
        return result;
    }

    QString writeError;
    if (!writeJsonAtomically(configPath, rootObj, &writeError))
    {
        result.message = writeError;
        return result;
    }

    result.backupPath = QDir::toNativeSeparators(result.backupPath);
    result.success = true;
    result.message = QStringLiteral("已从 %1 Hook 配置中安全移除 QtDesktopPetBridge 条目（已保留其余 Hook）。")
        .arg(targetDisplayName(target));
    return result;
}
