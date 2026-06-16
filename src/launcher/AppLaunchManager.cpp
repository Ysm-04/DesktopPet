#include "AppLaunchManager.h"

#include "../util/Logger.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

namespace
{
bool containsAny(const QString &text, const QStringList &keywords)
{
    for (const QString &keyword : keywords)
    {
        if (text.contains(keyword, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

QString sanitizedId(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("_"));
    if (value.isEmpty())
    {
        value = QStringLiteral("app");
    }
    return value.left(80);
}

QString normalizedExecutablePath(const QString &path)
{
    const QFileInfo info(path.trimmed());
    return info.exists() ? info.absoluteFilePath() : path.trimmed();
}

QString fileSuffix(const QString &path)
{
    return QFileInfo(path.trimmed()).suffix().toLower();
}
}

bool AppLaunchManager::isLaunchIntentText(const QString &text) const
{
    const QString normalized = text.trimmed();
    if (normalized.isEmpty() || isBlockedFileOperationText(normalized))
    {
        return false;
    }

    return containsAny(normalized, {
        QStringLiteral("打开"),
        QStringLiteral("启动"),
        QStringLiteral("运行"),
        QStringLiteral("唤起"),
    });
}

QString AppLaunchManager::requestedAppNameFromText(const QString &text) const
{
    QString requested = text.trimmed();
    requested.remove(QRegularExpression(QStringLiteral("^(请|麻烦|帮我|帮忙|给我)")));
    requested.remove(QRegularExpression(QStringLiteral("(打开一下|启动一下|运行一下|打开|启动|运行|唤起)")));
    requested.remove(QRegularExpression(QStringLiteral("(这个|一下|应用|软件|程序)$")));
    requested = requested.simplified();
    return requested;
}

AppLaunchPlan AppLaunchManager::resolveLaunchPlan(const QString &text, const AppLaunchConfig &config) const
{
    AppLaunchPlan plan;
    plan.requestedName = requestedAppNameFromText(text);
    if (!config.enabled)
    {
        plan.errorMessage = QStringLiteral("应用启动未启用，请先在设置页开启。");
        return plan;
    }

    if (plan.requestedName.isEmpty() || !isLaunchIntentText(text))
    {
        plan.errorMessage = QStringLiteral("没有识别到要启动的应用名称。");
        return plan;
    }

    RegisteredApp app;
    if (m_discovery.findRegisteredApp(plan.requestedName, config, &app))
    {
        return planFromRegisteredApp(plan.requestedName, app, QStringLiteral("用户登记"));
    }

    if (m_discovery.findStartMenuShortcut(plan.requestedName, &app))
    {
        return planFromRegisteredApp(plan.requestedName, app, QStringLiteral("开始菜单"));
    }

    if (m_discovery.findAppPathRegistryEntry(plan.requestedName, &app))
    {
        return planFromRegisteredApp(plan.requestedName, app, QStringLiteral("App Paths 注册表"));
    }

    if (config.allowManualExeSelection)
    {
        plan.needsManualSelection = true;
        plan.errorMessage = QStringLiteral("没有找到匹配的应用，需要手动选择 exe。");
        return plan;
    }

    plan.errorMessage = QStringLiteral("没有找到匹配的应用。可在设置页登记应用别名，或开启未知应用手选 exe。");
    return plan;
}

AppLaunchPlan AppLaunchManager::manualSelectionPlan(const QString &text, const QString &executablePath) const
{
    AppLaunchPlan plan;
    plan.requestedName = requestedAppNameFromText(text);
    const QFileInfo info(executablePath);
    plan.displayName = info.completeBaseName();
    plan.executablePath = normalizedExecutablePath(executablePath);
    plan.workingDirectory = info.absolutePath();
    plan.matchSource = QStringLiteral("手动选择");
    plan.canRemember = true;
    QString errorMessage;
    plan.success = validateLaunchPlan(plan, &errorMessage);
    plan.errorMessage = errorMessage;
    return plan;
}

AppLaunchResult AppLaunchManager::executeLaunchPlan(const AppLaunchPlan &plan) const
{
    QString errorMessage;
    if (!validateLaunchPlan(plan, &errorMessage))
    {
        return {false, {}, errorMessage};
    }

    bool started = false;
    if (!plan.executablePath.isEmpty())
    {
        started = QProcess::startDetached(plan.executablePath, QStringList(), plan.workingDirectory);
    }
    else
    {
        started = QDesktopServices::openUrl(QUrl::fromLocalFile(plan.shortcutPath));
    }

    if (!started)
    {
        Logger::warning(QStringLiteral("Failed to launch app: %1").arg(plan.displayName));
        return {false, {}, QStringLiteral("启动应用失败。")};
    }

    Logger::info(QStringLiteral("App launched: source=%1").arg(plan.matchSource));
    return {true, QStringLiteral("已启动：%1").arg(plan.displayName), {}};
}

RegisteredApp AppLaunchManager::registeredAppFromPlan(const AppLaunchPlan &plan, const QStringList &aliases) const
{
    RegisteredApp app;
    app.id = sanitizedId(plan.requestedName.isEmpty() ? plan.displayName : plan.requestedName);
    app.displayName = plan.displayName;
    app.aliases = aliases;
    if (app.aliases.isEmpty())
    {
        app.aliases = {plan.requestedName.isEmpty() ? plan.displayName : plan.requestedName};
    }
    if (!app.aliases.contains(app.displayName, Qt::CaseInsensitive))
    {
        app.aliases.prepend(app.displayName);
    }
    app.executablePath = plan.executablePath;
    app.shortcutPath = plan.shortcutPath;
    app.workingDirectory = plan.workingDirectory;
    app.userDefined = true;
    return app;
}

bool AppLaunchManager::isBlockedFileOperationText(const QString &text) const
{
    return containsAny(text, {
        QStringLiteral("打开文件"),
        QStringLiteral("打开文件夹"),
        QStringLiteral("打开目录"),
        QStringLiteral("读取文件"),
        QStringLiteral("复制文件"),
        QStringLiteral("备份文件"),
        QStringLiteral("重命名文件"),
        QStringLiteral("删除文件"),
        QStringLiteral("移动文件"),
        QStringLiteral("保存到"),
        QStringLiteral("截图"),
        QStringLiteral("打包"),
        QStringLiteral("压缩"),
        QStringLiteral("脚本"),
        QStringLiteral("命令"),
    });
}

bool AppLaunchManager::validateLaunchPlan(const AppLaunchPlan &plan, QString *errorMessage) const
{
    const QString executablePath = plan.executablePath.trimmed();
    const QString shortcutPath = plan.shortcutPath.trimmed();
    if (executablePath.isEmpty() && shortcutPath.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("启动路径为空。");
        }
        return false;
    }

    if (!executablePath.isEmpty())
    {
        const QFileInfo info(executablePath);
        const QString suffix = fileSuffix(executablePath);
        if (suffix != QStringLiteral("exe"))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("只允许启动 .exe 应用，不支持脚本、安装包或命令文件。");
            }
            return false;
        }
        if (!info.exists() || !info.isFile())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("应用文件不存在。");
            }
            return false;
        }
    }

    if (!shortcutPath.isEmpty())
    {
        const QString suffix = fileSuffix(shortcutPath);
        if (suffix != QStringLiteral("lnk"))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("只允许打开开始菜单快捷方式。");
            }
            return false;
        }
        if (!QFileInfo(shortcutPath).exists())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("快捷方式不存在。");
            }
            return false;
        }
    }

    if (containsAny(executablePath + shortcutPath, {
            QStringLiteral(".bat"),
            QStringLiteral(".cmd"),
            QStringLiteral(".ps1"),
            QStringLiteral(".vbs"),
            QStringLiteral(".js"),
            QStringLiteral(".msi"),
        }))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("不支持启动脚本、命令文件或安装包。");
        }
        return false;
    }

    return true;
}

AppLaunchPlan AppLaunchManager::planFromRegisteredApp(const QString &requestedName, const RegisteredApp &app, const QString &matchSource) const
{
    AppLaunchPlan plan;
    plan.requestedName = requestedName;
    plan.displayName = app.displayName;
    plan.executablePath = app.executablePath;
    plan.shortcutPath = app.shortcutPath;
    plan.workingDirectory = app.workingDirectory;
    plan.matchSource = matchSource;
    QString errorMessage;
    plan.success = validateLaunchPlan(plan, &errorMessage);
    plan.errorMessage = errorMessage;
    return plan;
}
