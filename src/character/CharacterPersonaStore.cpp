#include "CharacterPersonaStore.h"

#include "CharacterPackage.h"
#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>

#include <utility>

namespace
{
constexpr int PersonaStoreVersion = 1;

bool setErrorMessage(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

bool isValidOverrideCharacterKey(const QString &characterId)
{
    const QString trimmed = characterId.trimmed();
    if (trimmed.isEmpty()
        || trimmed == QStringLiteral(".")
        || trimmed == QStringLiteral("..")
        || trimmed.startsWith(QLatin1Char('.'))
        || trimmed.endsWith(QLatin1Char('.')))
    {
        return false;
    }

    for (const QChar ch : trimmed)
    {
        const bool allowed = (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            || ch == QLatin1Char('.')
            || ch == QLatin1Char('_')
            || ch == QLatin1Char('-');
        if (!allowed)
        {
            return false;
        }
    }

    return true;
}

QString normalizeLineEndingsAndStripControlChars(const QString &text)
{
    QString normalized;
    normalized.reserve(text.size());

    for (int index = 0; index < text.size(); ++index)
    {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('\r'))
        {
            if (index + 1 < text.size() && text.at(index + 1) == QLatin1Char('\n'))
            {
                ++index;
            }
            normalized.append(QLatin1Char('\n'));
            continue;
        }

        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t'))
        {
            normalized.append(ch);
            continue;
        }

        if (ch.unicode() < 0x20 || ch.unicode() == 0x7F)
        {
            continue;
        }

        normalized.append(ch);
    }

    return normalized.trimmed();
}

void backupBrokenPersonaStoreFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(
        fileInfo.completeBaseName() + QStringLiteral(".broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            fileInfo.completeBaseName()
            + QStringLiteral(".broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken character personas file was backed up: %1").arg(backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken character personas file: %1").arg(filePath));
}

bool writeJsonObjectAtomically(
    const QString &filePath,
    const QJsonObject &root,
    const QString &targetName,
    QString *errorMessage)
{
    const QFileInfo fileInfo(filePath);
    QDir directory(fileInfo.absolutePath());
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        Logger::warning(QStringLiteral("Unable to create directory for %1.").arg(targetName));
        return setErrorMessage(errorMessage, QStringLiteral("无法创建存储目录。"));
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        Logger::warning(QStringLiteral("Unable to open %1 for writing.").arg(targetName));
        return setErrorMessage(errorMessage, QStringLiteral("无法打开目标文件进行写入。"));
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        Logger::warning(QStringLiteral("Write was incomplete for %1.").arg(targetName));
        return setErrorMessage(errorMessage, QStringLiteral("写入目标文件不完整。"));
    }

    if (!file.commit())
    {
        Logger::warning(QStringLiteral("Unable to commit %1.").arg(targetName));
        return setErrorMessage(errorMessage, QStringLiteral("提交保存文件失败。"));
    }

    return true;
}
}

CharacterPersonaStore::CharacterPersonaStore(QString filePath)
    : m_filePath(std::move(filePath))
{
}

QString CharacterPersonaStore::filePath() const
{
    return m_filePath;
}

QString CharacterPersonaStore::sanitizePersona(const QString &persona)
{
    QString cleaned = normalizeLineEndingsAndStripControlChars(persona);
    if (cleaned.size() > CharacterPersonaMaxChars)
    {
        cleaned = cleaned.left(CharacterPersonaMaxChars).trimmed();
    }
    return cleaned;
}

QString CharacterPersonaStore::normalizeDraftText(const QString &text)
{
    return normalizeLineEndingsAndStripControlChars(text);
}

QMap<QString, QString> CharacterPersonaStore::loadOverrides(QString *errorMessage) const
{
    QMap<QString, QString> overrides;

    QFile file(m_filePath);
    if (!file.exists())
    {
        return overrides;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::warning(QStringLiteral("Unable to read character personas file."));
        setErrorMessage(errorMessage, QStringLiteral("无法读取角色人设覆盖文件。"));
        return overrides;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenPersonaStoreFile(m_filePath);
        Logger::warning(QStringLiteral("Character personas file is broken; it will be ignored."));
        setErrorMessage(errorMessage, QStringLiteral("角色人设覆盖文件损坏，已备份并忽略。"));
        return overrides;
    }

    const QJsonObject personas = document.object().value(QStringLiteral("personas")).toObject();
    for (auto iterator = personas.constBegin(); iterator != personas.constEnd(); ++iterator)
    {
        const QString key = iterator.key().trimmed();
        if (!isValidOverrideCharacterKey(key) || !iterator.value().isString())
        {
            continue;
        }

        const QString cleaned = sanitizePersona(iterator.value().toString());
        if (!cleaned.isEmpty())
        {
            overrides.insert(key, cleaned);
        }
    }

    return overrides;
}

bool CharacterPersonaStore::saveOverrides(const QMap<QString, QString> &overrides, QString *errorMessage) const
{
    QJsonObject personasObject;
    for (auto iterator = overrides.constBegin(); iterator != overrides.constEnd(); ++iterator)
    {
        const QString key = iterator.key().trimmed();
        if (!isValidOverrideCharacterKey(key))
        {
            continue;
        }

        const QString cleaned = sanitizePersona(iterator.value());
        if (!cleaned.isEmpty())
        {
            personasObject.insert(key, cleaned);
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), PersonaStoreVersion);
    root.insert(QStringLiteral("personas"), personasObject);

    return writeJsonObjectAtomically(
        m_filePath,
        root,
        QStringLiteral("character personas file"),
        errorMessage);
}

bool CharacterPersonaStore::setOverride(
    const QString &characterId,
    const QString &persona,
    QString *errorMessage) const
{
    const QString key = characterId.trimmed();
    if (!isValidOverrideCharacterKey(key))
    {
        return setErrorMessage(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    const QString cleaned = sanitizePersona(persona);
    if (cleaned.isEmpty())
    {
        return removeOverride(key, errorMessage);
    }

    QMap<QString, QString> overrides = loadOverrides();
    overrides.insert(key, cleaned);
    return saveOverrides(overrides, errorMessage);
}

bool CharacterPersonaStore::removeOverride(const QString &characterId, QString *errorMessage) const
{
    const QString key = characterId.trimmed();
    if (!isValidOverrideCharacterKey(key))
    {
        return setErrorMessage(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    if (!QFileInfo::exists(m_filePath))
    {
        return true;
    }

    QMap<QString, QString> overrides = loadOverrides();
    if (!overrides.contains(key))
    {
        return true;
    }

    overrides.remove(key);
    return saveOverrides(overrides, errorMessage);
}

ResolvedCharacterPersona CharacterPersonaStore::resolvePersona(
    const QString &characterId,
    const QString &packagePersona) const
{
    ResolvedCharacterPersona result;
    result.packagePersona = sanitizePersona(packagePersona);

    const QString key = characterId.trimmed();
    if (isValidOverrideCharacterKey(key))
    {
        const QMap<QString, QString> overrides = loadOverrides();
        const auto iterator = overrides.constFind(key);
        if (iterator != overrides.constEnd() && !iterator.value().isEmpty())
        {
            result.persona = iterator.value();
            result.source = CharacterPersonaSource::UserOverride;
            result.hasUserOverride = true;
            return result;
        }
    }

    if (!result.packagePersona.isEmpty())
    {
        result.persona = result.packagePersona;
        result.source = CharacterPersonaSource::PackageBuiltIn;
        return result;
    }

    result.source = CharacterPersonaSource::ProgramDefault;
    return result;
}

bool CharacterPersonaStore::updateManifestPersonaAtomic(
    const QString &manifestFilePath,
    const QString &persona,
    QString *errorMessage)
{
    QFile file(manifestFilePath);
    if (!file.exists())
    {
        return setErrorMessage(errorMessage, QStringLiteral("角色清单 character.json 不存在。"));
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        return setErrorMessage(errorMessage, QStringLiteral("无法读取角色清单 character.json。"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return setErrorMessage(errorMessage, QStringLiteral("角色清单 character.json 格式无效，无法更新人设。"));
    }

    QJsonObject root = document.object();
    const QString cleaned = sanitizePersona(persona);
    if (cleaned.isEmpty())
    {
        root.remove(QStringLiteral("persona"));
    }
    else
    {
        root.insert(QStringLiteral("persona"), cleaned);
    }

    return writeJsonObjectAtomically(
        manifestFilePath,
        root,
        QStringLiteral("character manifest"),
        errorMessage);
}
