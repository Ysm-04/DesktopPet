#pragma once

#include <QMap>
#include <QString>

enum class CharacterPersonaSource
{
    UserOverride,
    PackageBuiltIn,
    ProgramDefault
};

struct ResolvedCharacterPersona
{
    QString persona;
    QString packagePersona;
    CharacterPersonaSource source = CharacterPersonaSource::ProgramDefault;
    bool hasUserOverride = false;
};

class CharacterPersonaStore
{
public:
    explicit CharacterPersonaStore(QString filePath);

    QString filePath() const;

    static QString sanitizePersona(const QString &persona);
    static QString normalizeDraftText(const QString &text);

    QMap<QString, QString> loadOverrides(QString *errorMessage = nullptr) const;
    bool saveOverrides(const QMap<QString, QString> &overrides, QString *errorMessage = nullptr) const;
    bool setOverride(const QString &characterId, const QString &persona, QString *errorMessage = nullptr) const;
    bool removeOverride(const QString &characterId, QString *errorMessage = nullptr) const;
    ResolvedCharacterPersona resolvePersona(const QString &characterId, const QString &packagePersona) const;

    static bool updateManifestPersonaAtomic(
        const QString &manifestFilePath,
        const QString &persona,
        QString *errorMessage = nullptr);

private:
    QString m_filePath;
};
