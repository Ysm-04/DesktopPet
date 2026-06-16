#include "ReminderSoundRepository.h"

#include "../util/ResourcePaths.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace
{
constexpr qint64 MaxSoundFileBytes = 5 * 1024 * 1024;
constexpr int MaxSoundDurationSeconds = 30;

QString appDataPath()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path.isEmpty() ? QDir::currentPath() : path;
}

QString sanitizedSoundId(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("_"));
    while (value.startsWith(QLatin1Char('.')) || value.startsWith(QLatin1Char('_')) || value.startsWith(QLatin1Char('-')))
    {
        value.remove(0, 1);
    }
    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char('_')) || value.endsWith(QLatin1Char('-')))
    {
        value.chop(1);
    }
    return value.isEmpty() ? QStringLiteral("reminder_sound") : value;
}

QString displayNameForBuiltInSound(const QString &id)
{
    if (id == QStringLiteral("reminder_default"))
    {
        return QStringLiteral("默认提醒");
    }

    if (id == QStringLiteral("reminder_soft"))
    {
        return QStringLiteral("柔和提醒");
    }

    return id;
}

ReminderSoundInfo builtInSound(const QString &id)
{
    return {
        id,
        displayNameForBuiltInSound(id),
        ResourcePaths::reminderSoundsRootPath() + QLatin1Char('/') + id + QStringLiteral(".wav"),
        true,
    };
}

bool readChunkHeader(QDataStream &stream, QByteArray *id, quint32 *size)
{
    char chunkId[4] = {};
    if (stream.readRawData(chunkId, 4) != 4)
    {
        return false;
    }

    *id = QByteArray(chunkId, 4);
    stream >> *size;
    return stream.status() == QDataStream::Ok;
}

bool isSafeSoundId(const QString &soundId)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return expression.match(soundId).hasMatch()
        && !soundId.contains(QLatin1Char('/'))
        && !soundId.contains(QLatin1Char('\\'));
}

bool pathStaysInsideDirectory(const QString &filePath, const QString &directoryPath)
{
    const QString absoluteFilePath = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
    const QString absoluteDirectoryPath = QDir::cleanPath(QDir(directoryPath).absolutePath());
    return absoluteFilePath == absoluteDirectoryPath
        || absoluteFilePath.startsWith(absoluteDirectoryPath + QLatin1Char('/'));
}
}

QString ReminderSoundRepository::defaultSoundId()
{
    return QStringLiteral("reminder_default");
}

QVector<ReminderSoundInfo> ReminderSoundRepository::availableSounds()
{
    QVector<ReminderSoundInfo> sounds;
    const QStringList builtInIds = {
        QStringLiteral("reminder_default"),
        QStringLiteral("reminder_soft"),
    };

    for (const QString &id : builtInIds)
    {
        const ReminderSoundInfo info = builtInSound(id);
        if (QFileInfo::exists(info.path))
        {
            sounds.append(info);
        }
    }

    QDir userRoot(userSoundsRootPath());
    const QFileInfoList userFiles = userRoot.entryInfoList({QStringLiteral("*.wav")}, QDir::Files, QDir::Name);
    for (const QFileInfo &fileInfo : userFiles)
    {
        const QString id = fileInfo.completeBaseName();
        if (isBuiltInSound(id))
        {
            continue;
        }

        sounds.append({
            id,
            id,
            QDir::cleanPath(fileInfo.absoluteFilePath()),
            false,
        });
    }

    return sounds;
}

ReminderSoundInfo ReminderSoundRepository::soundInfo(const QString &soundId)
{
    const QString normalizedId = soundId.trimmed().isEmpty() ? defaultSoundId() : soundId.trimmed();
    for (const ReminderSoundInfo &info : availableSounds())
    {
        if (info.id == normalizedId)
        {
            return info;
        }
    }

    return builtInSound(defaultSoundId());
}

QString ReminderSoundRepository::soundPath(const QString &soundId)
{
    return soundInfo(soundId).path;
}

QString ReminderSoundRepository::userSoundsRootPath()
{
    return QDir(appDataPath()).filePath(QStringLiteral("sounds/reminders"));
}

bool ReminderSoundRepository::isBuiltInSound(const QString &soundId)
{
    return soundId == QStringLiteral("reminder_default")
        || soundId == QStringLiteral("reminder_soft");
}

bool ReminderSoundRepository::importSoundFile(const QString &sourcePath, QString *importedSoundId, QString *errorMessage)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效文件不存在。");
        }
        return false;
    }

    if (sourceInfo.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("只支持导入 wav 音效。");
        }
        return false;
    }

    if (!validateWaveFile(sourceInfo.absoluteFilePath(), errorMessage))
    {
        return false;
    }

    QDir userRoot(userSoundsRootPath());
    if (!userRoot.exists() && !userRoot.mkpath(QStringLiteral(".")))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法创建用户音效目录。");
        }
        return false;
    }

    QString id = sanitizedSoundId(sourceInfo.completeBaseName());
    if (isBuiltInSound(id))
    {
        id.prepend(QStringLiteral("user_"));
    }

    QString targetPath = userRoot.filePath(id + QStringLiteral(".wav"));
    int suffix = 2;
    while (QFileInfo::exists(targetPath))
    {
        targetPath = userRoot.filePath(id + QStringLiteral("_") + QString::number(suffix) + QStringLiteral(".wav"));
        ++suffix;
    }

    const QString finalId = QFileInfo(targetPath).completeBaseName();
    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("复制音效文件失败。");
        }
        return false;
    }

    if (importedSoundId != nullptr)
    {
        *importedSoundId = finalId;
    }
    return true;
}

bool ReminderSoundRepository::deleteUserSound(const QString &soundId, QString *errorMessage)
{
    const QString id = soundId.trimmed();
    if (id.isEmpty() || isBuiltInSound(id))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("内置提醒音效不能删除。");
        }
        return false;
    }

    if (!isSafeSoundId(id))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效 id 不安全。");
        }
        return false;
    }

    const QString userRootPath = userSoundsRootPath();
    const QString path = QDir(userRootPath).filePath(id + QStringLiteral(".wav"));
    if (!pathStaysInsideDirectory(path, userRootPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效路径不安全。");
        }
        return false;
    }

    QFile file(path);
    if (!file.exists())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("没有找到可删除的用户音效。");
        }
        return false;
    }

    if (!file.remove())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("删除用户音效失败。");
        }
        return false;
    }

    return true;
}

bool ReminderSoundRepository::validateWaveFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取音效文件。");
        }
        return false;
    }

    if (file.size() <= 44 || file.size() > MaxSoundFileBytes)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效文件大小不符合要求。");
        }
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    char riff[4] = {};
    char wave[4] = {};
    quint32 riffSize = 0;
    if (stream.readRawData(riff, 4) != 4)
    {
        return false;
    }
    stream >> riffSize;
    if (stream.readRawData(wave, 4) != 4
        || QByteArray(riff, 4) != QByteArray("RIFF", 4)
        || QByteArray(wave, 4) != QByteArray("WAVE", 4))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("不是有效的 RIFF/WAVE 文件。");
        }
        return false;
    }

    quint16 audioFormat = 0;
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bitsPerSample = 0;
    quint32 dataSize = 0;
    bool hasFmt = false;
    bool hasData = false;

    while (!stream.atEnd())
    {
        QByteArray chunkId;
        quint32 chunkSize = 0;
        if (!readChunkHeader(stream, &chunkId, &chunkSize))
        {
            break;
        }

        const qint64 chunkStart = file.pos();
        if (chunkId == QByteArray("fmt ", 4))
        {
            quint32 byteRate = 0;
            quint16 blockAlign = 0;
            stream >> audioFormat >> channels >> sampleRate >> byteRate >> blockAlign >> bitsPerSample;
            hasFmt = true;
        }
        else if (chunkId == QByteArray("data", 4))
        {
            dataSize = chunkSize;
            hasData = true;
        }

        file.seek(chunkStart + chunkSize + (chunkSize % 2));
    }

    if (!hasFmt || !hasData || audioFormat != 1 || channels == 0 || sampleRate == 0 || bitsPerSample == 0 || dataSize == 0)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效文件格式不受支持。请使用 PCM wav。");
        }
        return false;
    }

    const double durationSeconds = static_cast<double>(dataSize) / static_cast<double>(sampleRate * channels * (bitsPerSample / 8.0));
    if (durationSeconds <= 0.0 || durationSeconds > MaxSoundDurationSeconds)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("音效时长不符合要求。");
        }
        return false;
    }

    return true;
}
