#include "FileOperationManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace
{
constexpr qint64 MaxReadBytes = 64 * 1024;
constexpr int MaxDirectoryEntries = 200;

FileOperationResult failedResult(const QString &message)
{
    FileOperationResult result;
    result.success = false;
    result.errorMessage = message;
    return result;
}

QString fileDisplayName(const QString &path)
{
    const QFileInfo info(path);
    return info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
}

QString copyTargetPath(const QString &sourceFilePath, const QString &targetDirectoryPath)
{
    return QDir(targetDirectoryPath).filePath(QFileInfo(sourceFilePath).fileName());
}
}

FileOperationPlan FileOperationManager::readTextFilePlan(const QString &filePath) const
{
    FileOperationPlan plan;
    plan.type = FileOperationType::ReadTextFile;
    plan.title = QStringLiteral("读取文本文件");
    plan.sourcePath = QFileInfo(filePath).absoluteFilePath();
    plan.description = QStringLiteral("读取文本文件：%1").arg(plan.sourcePath);
    return plan;
}

FileOperationPlan FileOperationManager::listDirectoryPlan(const QString &directoryPath) const
{
    FileOperationPlan plan;
    plan.type = FileOperationType::ListDirectory;
    plan.title = QStringLiteral("列出文件夹");
    plan.sourcePath = QFileInfo(directoryPath).absoluteFilePath();
    plan.description = QStringLiteral("列出文件夹内容：%1").arg(plan.sourcePath);
    return plan;
}

FileOperationPlan FileOperationManager::copyFilePlan(const QString &sourceFilePath, const QString &targetDirectoryPath) const
{
    FileOperationPlan plan;
    plan.type = FileOperationType::CopyFile;
    plan.title = QStringLiteral("复制文件");
    plan.sourcePath = QFileInfo(sourceFilePath).absoluteFilePath();
    plan.targetPath = copyTargetPath(plan.sourcePath, QFileInfo(targetDirectoryPath).absoluteFilePath());
    plan.description = QStringLiteral("复制文件：\n%1\n到：\n%2").arg(plan.sourcePath, plan.targetPath);
    plan.requiresConfirmation = true;
    return plan;
}

FileOperationPlan FileOperationManager::backupFilePlan(const QString &sourceFilePath) const
{
    FileOperationPlan plan;
    plan.type = FileOperationType::BackupFile;
    plan.title = QStringLiteral("创建备份");
    plan.sourcePath = QFileInfo(sourceFilePath).absoluteFilePath();
    plan.description = QStringLiteral("为文件创建同目录备份：\n%1").arg(plan.sourcePath);
    plan.requiresConfirmation = true;
    return plan;
}

FileOperationPlan FileOperationManager::renameFilePlan(const QString &sourceFilePath, const QString &newFileName) const
{
    FileOperationPlan plan;
    plan.type = FileOperationType::RenameFile;
    plan.title = QStringLiteral("重命名文件");
    plan.sourcePath = QFileInfo(sourceFilePath).absoluteFilePath();
    plan.targetPath = QDir(QFileInfo(plan.sourcePath).absolutePath()).filePath(newFileName.trimmed());
    plan.description = QStringLiteral("重命名文件：\n%1\n为：\n%2").arg(plan.sourcePath, plan.targetPath);
    plan.requiresConfirmation = true;
    return plan;
}

FileOperationResult FileOperationManager::executeReadTextFile(const FileOperationPlan &plan) const
{
    QString errorMessage;
    if (!m_sandbox.validateReadableTextFile(plan.sourcePath, &errorMessage))
    {
        return failedResult(errorMessage);
    }

    QFile file(plan.sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return failedResult(QStringLiteral("无法读取文本文件。"));
    }

    const QByteArray content = file.read(MaxReadBytes + 1);
    QString text = QString::fromUtf8(content.left(MaxReadBytes));
    if (content.size() > MaxReadBytes)
    {
        text += QStringLiteral("\n\n[文件较大，仅显示前 64KB。]");
    }

    FileOperationResult result;
    result.success = true;
    result.outputText = text;
    result.message = QStringLiteral("已读取：%1").arg(fileDisplayName(plan.sourcePath));
    return result;
}

FileOperationResult FileOperationManager::executeListDirectory(const FileOperationPlan &plan) const
{
    QString errorMessage;
    if (!m_sandbox.validateReadableDirectory(plan.sourcePath, &errorMessage))
    {
        return failedResult(errorMessage);
    }

    const QDir directory(plan.sourcePath);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::DirsFirst | QDir::Name);

    QStringList lines;
    lines.append(QStringLiteral("文件夹内容：%1").arg(directory.absolutePath()));
    const int count = qMin(entries.size(), MaxDirectoryEntries);
    for (int index = 0; index < count; ++index)
    {
        const QFileInfo &entry = entries.at(index);
        lines.append(QStringLiteral("%1 %2").arg(entry.isDir() ? QStringLiteral("[目录]") : QStringLiteral("[文件]"), entry.fileName()));
    }
    if (entries.size() > MaxDirectoryEntries)
    {
        lines.append(QStringLiteral("[仅显示前 %1 项，共 %2 项]").arg(MaxDirectoryEntries).arg(entries.size()));
    }

    FileOperationResult result;
    result.success = true;
    result.outputText = lines.join(QLatin1Char('\n'));
    result.message = QStringLiteral("已列出文件夹。");
    return result;
}

FileOperationResult FileOperationManager::executeCopyFile(const FileOperationPlan &plan) const
{
    QString errorMessage;
    if (!m_sandbox.validateReadableFile(plan.sourcePath, &errorMessage)
        || !m_sandbox.validateNewFileTarget(plan.targetPath, &errorMessage))
    {
        return failedResult(errorMessage);
    }

    if (!QFile::copy(plan.sourcePath, plan.targetPath))
    {
        return failedResult(QStringLiteral("复制文件失败。"));
    }

    FileOperationResult result;
    result.success = true;
    result.targetPath = plan.targetPath;
    result.message = QStringLiteral("已复制到：%1").arg(plan.targetPath);
    return result;
}

FileOperationResult FileOperationManager::executeBackupFile(const FileOperationPlan &plan) const
{
    QString backupPath;
    QString errorMessage;
    if (!m_backupManager.createBackup(plan.sourcePath, &backupPath, &errorMessage))
    {
        return failedResult(errorMessage);
    }

    FileOperationResult result;
    result.success = true;
    result.targetPath = backupPath;
    result.message = QStringLiteral("已创建备份：%1").arg(backupPath);
    return result;
}

FileOperationResult FileOperationManager::executeRenameFile(const FileOperationPlan &plan) const
{
    QString errorMessage;
    if (!m_sandbox.validateReadableFile(plan.sourcePath, &errorMessage)
        || !m_sandbox.validateSafeFileName(QFileInfo(plan.targetPath).fileName(), &errorMessage)
        || !m_sandbox.validateNewFileTarget(plan.targetPath, &errorMessage))
    {
        return failedResult(errorMessage);
    }

    QFile file(plan.sourcePath);
    if (!file.rename(plan.targetPath))
    {
        return failedResult(QStringLiteral("重命名文件失败。"));
    }

    FileOperationResult result;
    result.success = true;
    result.targetPath = plan.targetPath;
    result.message = QStringLiteral("已重命名为：%1").arg(plan.targetPath);
    return result;
}
