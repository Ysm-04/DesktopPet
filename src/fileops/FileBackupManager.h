#pragma once

#include "FileSandbox.h"

#include <QString>

class FileBackupManager
{
public:
    bool createBackup(const QString &sourceFilePath, QString *backupPath = nullptr, QString *errorMessage = nullptr) const;

private:
    FileSandbox m_sandbox;
};
