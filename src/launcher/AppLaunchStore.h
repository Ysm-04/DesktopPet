#pragma once

#include "AppLaunchTypes.h"

#include <QString>

class AppLaunchStore
{
public:
    AppLaunchConfig load(QString *errorMessage = nullptr) const;
    bool save(const AppLaunchConfig &config, QString *errorMessage = nullptr) const;
    QString storePath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath) const;
};
