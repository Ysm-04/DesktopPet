#pragma once

#include <QString>

class FileSandbox
{
public:
    bool validateReadableFile(const QString &filePath, QString *errorMessage = nullptr) const;
    bool validateReadableTextFile(const QString &filePath, QString *errorMessage = nullptr) const;
    bool validateReadableDirectory(const QString &directoryPath, QString *errorMessage = nullptr) const;
    bool validateWritableDirectory(const QString &directoryPath, QString *errorMessage = nullptr) const;
    bool validateNewFileTarget(const QString &filePath, QString *errorMessage = nullptr) const;
    bool validateSafeFileName(const QString &fileName, QString *errorMessage = nullptr) const;
};
