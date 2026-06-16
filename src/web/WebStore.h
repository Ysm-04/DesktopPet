#pragma once

#include "WebConfig.h"

#include <QString>

class WebStore
{
public:
    WebConfig load(QString *errorMessage = nullptr) const;
    bool save(const WebConfig &config, QString *errorMessage = nullptr) const;
    QString storePath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenConfig(const QString &filePath) const;
};
