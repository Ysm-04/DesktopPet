#pragma once

#include "CharacterPersonaStore.h"

#include <QString>
#include <QVector>

struct CharacterPackageInfo
{
    QString id;
    QString displayName;
    QString packagePath;
    QString previewPath;
    bool userPackage = false;
};

struct CharacterPackageValidationResult
{
    bool valid = false;
    QString characterId;
    QString displayName;
    QString warningMessage;
    QString errorMessage;
};

class CharacterPackageRepository
{
public:
    static QString charactersRootPath();
    static QString builtInCharactersRootPath();
    static QString userCharactersRootPath();
    static QString defaultCharacterId();
    static QString defaultPackagePath();
    static QString defaultPreviewPath();
    static QString packagePath(const QString &characterId);
    static QString builtInPackagePath(const QString &characterId);
    static QString userPackagePath(const QString &characterId);
    static QString previewPath(const QString &characterId);
    static bool isValidCharacterId(const QString &characterId);
    static bool hasPackage(const QString &characterId);
    static bool hasBuiltInPackage(const QString &characterId);
    static bool hasUserPackage(const QString &characterId);
    static QVector<CharacterPackageInfo> availablePackages();
    static CharacterPackageValidationResult validatePackageDirectoryWithDetails(const QString &directoryPath);
    static bool validatePackageDirectory(
        const QString &directoryPath,
        QString *characterId = nullptr,
        QString *displayName = nullptr,
        QString *errorMessage = nullptr);
    static bool importPackageDirectory(
        const QString &sourceDirectoryPath,
        bool overwrite,
        QString *importedCharacterId = nullptr,
        QString *errorMessage = nullptr);
    static bool exportPackageDirectory(
        const QString &characterId,
        const QString &targetParentDirectoryPath,
        bool overwrite,
        QString *exportedPath = nullptr,
        QString *errorMessage = nullptr);
    static bool deleteUserPackage(const QString &characterId, QString *errorMessage = nullptr);
    static ResolvedCharacterPersona resolveCharacterPersona(
        const QString &characterId,
        const QString &overrideFilePath = {});
    static bool saveCharacterPersona(
        const QString &characterId,
        const QString &persona,
        QString *errorMessage = nullptr,
        const QString &overrideFilePath = {});
    static bool resetCharacterPersonaToDefault(
        const QString &characterId,
        QString *errorMessage = nullptr,
        const QString &overrideFilePath = {});
};
