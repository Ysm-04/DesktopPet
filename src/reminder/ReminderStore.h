#pragma once

#include "ReminderTypes.h"

#include <QString>
#include <QVector>

class ReminderStore
{
public:
    QVector<ReminderItem> load(QString *errorMessage = nullptr) const;
    bool save(const QVector<ReminderItem> &items, QString *errorMessage = nullptr) const;
    QString storePath() const;

private:
    QString configDirectoryPath() const;
    void backupBrokenStore(const QString &filePath) const;
};
