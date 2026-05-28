#pragma once

#include "CharacterPackage.h"

#include <QString>

class CharacterPackageLoader
{
public:
    static CharacterPackage load(const QString &packagePath, QString *errorMessage = nullptr);

private:
    static QStringList collectPngFrames(const QString &stateDirectoryPath);
    static bool setError(QString *errorMessage, const QString &message);
};

