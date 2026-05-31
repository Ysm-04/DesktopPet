#pragma once

#include <QString>
#include <QStringList>

class CharacterPackageRepository
{
public:
    static QString charactersRootPath();
    static QString defaultCharacterId();
    static QString defaultPackagePath();
    static QString defaultPreviewPath();
    static QString packagePath(const QString &characterId);
    static QString previewPath(const QString &characterId);
    static QStringList availablePackageIds();
};
