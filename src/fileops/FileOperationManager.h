#pragma once

#include "FileBackupManager.h"
#include "FileOperationTypes.h"
#include "FileSandbox.h"

#include <QString>

class FileOperationManager
{
public:
    FileOperationPlan readTextFilePlan(const QString &filePath) const;
    FileOperationPlan listDirectoryPlan(const QString &directoryPath) const;
    FileOperationPlan copyFilePlan(const QString &sourceFilePath, const QString &targetDirectoryPath) const;
    FileOperationPlan backupFilePlan(const QString &sourceFilePath) const;
    FileOperationPlan renameFilePlan(const QString &sourceFilePath, const QString &newFileName) const;

    FileOperationResult executeReadTextFile(const FileOperationPlan &plan) const;
    FileOperationResult executeListDirectory(const FileOperationPlan &plan) const;
    FileOperationResult executeCopyFile(const FileOperationPlan &plan) const;
    FileOperationResult executeBackupFile(const FileOperationPlan &plan) const;
    FileOperationResult executeRenameFile(const FileOperationPlan &plan) const;

private:
    FileSandbox m_sandbox;
    FileBackupManager m_backupManager;
};
