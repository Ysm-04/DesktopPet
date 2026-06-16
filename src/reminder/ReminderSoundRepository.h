#pragma once

#include <QString>
#include <QVector>

struct ReminderSoundInfo
{
    QString id;
    QString displayName;
    QString path;
    bool builtIn = false;
};

class ReminderSoundRepository
{
public:
    static QString defaultSoundId();
    static QVector<ReminderSoundInfo> availableSounds();
    static ReminderSoundInfo soundInfo(const QString &soundId);
    static QString soundPath(const QString &soundId);
    static QString userSoundsRootPath();
    static bool isBuiltInSound(const QString &soundId);
    static bool importSoundFile(const QString &sourcePath, QString *importedSoundId = nullptr, QString *errorMessage = nullptr);
    static bool deleteUserSound(const QString &soundId, QString *errorMessage = nullptr);
    static bool validateWaveFile(const QString &path, QString *errorMessage = nullptr);
};
