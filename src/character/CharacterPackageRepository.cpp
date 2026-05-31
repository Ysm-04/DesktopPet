#include "CharacterPackageRepository.h"

#include <QDir>
#include <QFileInfo>

namespace
{
bool isValidCharacterId(const QString &characterId)
{
    const QString trimmed = characterId.trimmed();
    return !trimmed.isEmpty()
        && trimmed != QStringLiteral(".")
        && trimmed != QStringLiteral("..")
        && !trimmed.contains(QLatin1Char('/'))
        && !trimmed.contains(QLatin1Char('\\'));
}
}

QString CharacterPackageRepository::charactersRootPath()
{
    return QDir::cleanPath(QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/resources/characters"));
}

QString CharacterPackageRepository::defaultCharacterId()
{
    return QStringLiteral("shiroko");
}

QString CharacterPackageRepository::defaultPackagePath()
{
    return packagePath(defaultCharacterId());
}

QString CharacterPackageRepository::defaultPreviewPath()
{
    return previewPath(defaultCharacterId());
}

QString CharacterPackageRepository::packagePath(const QString &characterId)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return {};
    }

    return QDir(charactersRootPath()).filePath(trimmed);
}

QString CharacterPackageRepository::previewPath(const QString &characterId)
{
    const QString path = packagePath(characterId);
    if (path.isEmpty())
    {
        return {};
    }

    return QDir(path).filePath(QStringLiteral("preview.png"));
}

QStringList CharacterPackageRepository::availablePackageIds()
{
    const QDir rootDirectory(charactersRootPath());
    if (!rootDirectory.exists())
    {
        return {};
    }

    const QFileInfoList entries = rootDirectory.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);

    QStringList packageIds;
    for (const QFileInfo &entry : entries)
    {
        if (!isValidCharacterId(entry.fileName()))
        {
            continue;
        }

        const QFileInfo manifest(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("character.json")));
        if (manifest.isFile())
        {
            packageIds.append(entry.fileName());
        }
    }

    return packageIds;
}
