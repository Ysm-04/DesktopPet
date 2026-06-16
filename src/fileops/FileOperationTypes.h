#pragma once

#include <QString>
#include <QStringList>

enum class FileOperationType
{
    ReadTextFile,
    ListDirectory,
    CopyFile,
    BackupFile,
    RenameFile,
    ZipPackage,
};

struct FileOperationPlan
{
    FileOperationType type = FileOperationType::ReadTextFile;
    QString title;
    QString description;
    QString sourcePath;
    QString targetPath;
    QStringList warnings;
    bool requiresConfirmation = false;
};

struct FileOperationResult
{
    bool success = false;
    QString message;
    QString errorMessage;
    QString outputText;
    QString targetPath;
};
