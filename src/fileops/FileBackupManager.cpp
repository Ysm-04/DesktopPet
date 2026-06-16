#include "FileBackupManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace
{
bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
    return false;
}
}

bool FileBackupManager::createBackup(const QString &sourceFilePath, QString *backupPath, QString *errorMessage) const
{
    if (!m_sandbox.validateReadableFile(sourceFilePath, errorMessage))
    {
        return false;
    }

    const QFileInfo sourceInfo(sourceFilePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString suffix = sourceInfo.suffix().trimmed();
    const QString backupName = suffix.isEmpty()
        ? QStringLiteral("%1.backup.%2").arg(sourceInfo.completeBaseName(), timestamp)
        : QStringLiteral("%1.backup.%2.%3").arg(sourceInfo.completeBaseName(), timestamp, suffix);
    const QString targetPath = QDir(sourceInfo.absolutePath()).filePath(backupName);
    if (!m_sandbox.validateNewFileTarget(targetPath, errorMessage))
    {
        return false;
    }

    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
    {
        return setError(errorMessage, QStringLiteral("创建备份文件失败。"));
    }

    if (backupPath != nullptr)
    {
        *backupPath = targetPath;
    }
    return true;
}
