#include "CharacterPackageRepository.h"

#include "CharacterPackageLoader.h"
#include "CharacterMediaSpec.h"
#include "WebpAnimationBackend.h"
#include "../config/ConfigManager.h"
#include "../util/ResourcePaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

namespace
{

bool setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}

QString fallbackUserDataPath()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty())
    {
        return appDataPath;
    }

    return QDir::currentPath();
}

QString effectivePersonaStorePath(const QString &overrideFilePath)
{
    const QString trimmed = overrideFilePath.trimmed();
    return trimmed.isEmpty() ? ConfigManager().characterPersonasPath() : trimmed;
}

bool hasReadablePngFrame(const QStringList &framePaths)
{
    for (const QString &framePath : framePaths)
    {
        QImageReader reader(framePath);
        if (reader.canRead())
        {
            return true;
        }
    }

    return false;
}

bool isAsciiLetterOrNumber(QChar character)
{
    return (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
        || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
        || (character >= QLatin1Char('0') && character <= QLatin1Char('9'));
}

QStringList collectPackageIds(const QString &rootPath)
{
    const QDir rootDirectory(rootPath);
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
        if (!CharacterPackageRepository::isValidCharacterId(entry.fileName()))
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

bool copyDirectoryRecursively(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    const QDir sourceDirectory(sourcePath);
    if (!sourceDirectory.exists())
    {
        return setError(errorMessage, QStringLiteral("源角色文件夹不存在。"));
    }

    QDir targetDirectory(targetPath);
    if (!targetDirectory.exists() && !QDir().mkpath(targetPath))
    {
        return setError(errorMessage, QStringLiteral("无法创建导入目标目录。"));
    }

    const QFileInfoList entries = sourceDirectory.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
        QDir::Name);
    for (const QFileInfo &entry : entries)
    {
        if (entry.isSymLink() || entry.isJunction())
        {
            return setError(errorMessage, QStringLiteral("角色包中不允许包含符号链接或目录联接：") + entry.fileName());
        }

        const QString targetEntryPath = targetDirectory.filePath(entry.fileName());
        if (entry.isDir())
        {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), targetEntryPath, errorMessage))
            {
                return false;
            }
            continue;
        }

        if (!QFile::copy(entry.absoluteFilePath(), targetEntryPath))
        {
            return setError(errorMessage, QStringLiteral("复制文件失败：") + entry.fileName());
        }
    }

    return true;
}

bool replaceDirectoryWithPreparedImport(const QString &preparedPath, const QString &targetPath, bool overwrite, QString *errorMessage)
{
    QDir rootDirectory(QFileInfo(targetPath).absolutePath());
    if (!rootDirectory.exists() && !rootDirectory.mkpath(QStringLiteral(".")))
    {
        return setError(errorMessage, QStringLiteral("无法创建用户角色目录。"));
    }

    const QFileInfo targetInfo(targetPath);
    if (targetInfo.exists())
    {
        if (!overwrite)
        {
            return setError(errorMessage, QStringLiteral("同名角色已存在。"));
        }

        const QString backupPath = targetPath
            + QStringLiteral(".replacing.")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!rootDirectory.rename(targetPath, backupPath))
        {
            return setError(errorMessage, QStringLiteral("无法替换已有角色目录。"));
        }

        if (!rootDirectory.rename(preparedPath, targetPath))
        {
            rootDirectory.rename(backupPath, targetPath);
            return setError(errorMessage, QStringLiteral("写入新角色目录失败，已尝试恢复旧目录。"));
        }

        QDir(backupPath).removeRecursively();
        return true;
    }

    if (!rootDirectory.rename(preparedPath, targetPath))
    {
        return setError(errorMessage, QStringLiteral("写入角色目录失败。"));
    }

    return true;
}

bool validateManifest(const QString &directoryPath, QString *characterId, QString *displayName, QString *errorMessage)
{
    if (!CharacterMediaSpec::preflightPackage(directoryPath, errorMessage)) return false;
    const QDir packageDirectory(directoryPath);
    if (!packageDirectory.exists())
    {
        return setError(errorMessage, QStringLiteral("角色文件夹不存在。"));
    }

    QFile manifestFile(packageDirectory.filePath(QStringLiteral("character.json")));
    if (!manifestFile.exists())
    {
        return setError(errorMessage, QStringLiteral("缺少 character.json。"));
    }

    if (!manifestFile.open(QIODevice::ReadOnly))
    {
        return setError(errorMessage, QStringLiteral("无法读取 character.json。"));
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return setError(errorMessage, QStringLiteral("character.json 不是有效 JSON。"));
    }

    const QJsonObject root = document.object();
    if (!CharacterMediaSpec::isSupportedSchema(root.value(QStringLiteral("schemaVersion"))))
    {
        return setError(errorMessage, QStringLiteral("schemaVersion 不受支持。"));
    }

    QString packageId = root.value(QStringLiteral("id")).toString().trimmed();
    if (packageId.isEmpty())
    {
        packageId = QFileInfo(directoryPath).fileName().trimmed();
    }
    if (!CharacterPackageRepository::isValidCharacterId(packageId))
    {
        return setError(errorMessage, QStringLiteral("角色 id 不是安全的目录名。"));
    }

    const QJsonValue baseValue = root.value(QStringLiteral("base"));
    if (baseValue.isObject())
    {
        const QJsonObject base = baseValue.toObject();
        if (base.contains(QStringLiteral("width")) && base.value(QStringLiteral("width")).toInt(0) <= 0)
        {
            return setError(errorMessage, QStringLiteral("base.width 必须大于 0。"));
        }
        if (base.contains(QStringLiteral("height")) && base.value(QStringLiteral("height")).toInt(0) <= 0)
        {
            return setError(errorMessage, QStringLiteral("base.height 必须大于 0。"));
        }
        if (base.contains(QStringLiteral("scale")) && base.value(QStringLiteral("scale")).toDouble(0.0) <= 0.0)
        {
            return setError(errorMessage, QStringLiteral("base.scale 必须大于 0。"));
        }
        const double anchorX = base.value(QStringLiteral("anchorX")).toDouble(0.5);
        const double anchorY = base.value(QStringLiteral("anchorY")).toDouble(0.0);
        if (anchorX < 0.0 || anchorX > 1.0 || anchorY < 0.0 || anchorY > 1.0)
        {
            return setError(errorMessage, QStringLiteral("base.anchorX/anchorY 必须在 0 到 1 之间。"));
        }
    }

    const QString defaultState = root.value(QStringLiteral("defaultState")).toString().trimmed();
    if (defaultState.isEmpty())
    {
        return setError(errorMessage, QStringLiteral("defaultState 不能为空。"));
    }

    const QJsonValue statesValue = root.value(QStringLiteral("states"));
    if (!statesValue.isObject() || statesValue.toObject().isEmpty())
    {
        return setError(errorMessage, QStringLiteral("states 不能为空。"));
    }

    QSet<QString> validStates;
    const QJsonObject states = statesValue.toObject();
    for (auto iterator = states.constBegin(); iterator != states.constEnd(); ++iterator)
    {
        if (!iterator.value().isObject())
        {
            return setError(errorMessage, QStringLiteral("状态配置必须是对象：") + iterator.key());
        }

        CharacterState state;
        if (!CharacterMediaSpec::parseState(root.value(QStringLiteral("schemaVersion")).toInt(),
                                           iterator.key(), iterator.value().toObject(),
                                           directoryPath, &state, errorMessage))
            return false;
        if (state.mediaType == QStringLiteral("webp"))
        {
            if (!WebpAnimationBackend::validateFile(state.mediaPath, errorMessage)) return false;
        }
        else if (state.mediaType == QStringLiteral("frames") && !hasReadablePngFrame(state.framePaths))
            return setError(errorMessage, QStringLiteral("状态没有可读 PNG 帧：") + iterator.key());

        validStates.insert(iterator.key());
    }

    if (!validStates.contains(QStringLiteral("idle")))
    {
        return setError(errorMessage, QStringLiteral("缺少可用 idle 状态。"));
    }
    if (!validStates.contains(defaultState))
    {
        return setError(errorMessage, QStringLiteral("defaultState 不存在或不可用。"));
    }

    QString loadError;
    const CharacterPackage package = CharacterPackageLoader::load(directoryPath, &loadError);
    if (!loadError.isEmpty() || !package.hasState(QStringLiteral("idle")))
    {
        return setError(errorMessage, loadError.isEmpty() ? QStringLiteral("角色包无法被当前加载器读取。") : loadError);
    }
    if (!package.effects.closedFrame.isEmpty())
    {
        QImageReader closed(package.effects.closedFrame);
        const QImage frame = closed.read();
        if (frame.isNull())
            return setError(errorMessage, QStringLiteral("闭眼 PNG 无法完整解码。"));
    }

    if (characterId != nullptr)
    {
        *characterId = packageId;
    }
    if (displayName != nullptr)
    {
        *displayName = root.value(QStringLiteral("displayName")).toString(packageId).trimmed();
        if (displayName->isEmpty())
        {
            *displayName = packageId;
        }
    }

    return true;
}

QString bubbleAnchorWarning(const QString &directoryPath)
{
    QString loadError;
    const CharacterPackage package = CharacterPackageLoader::load(directoryPath, &loadError);
    if (!loadError.isEmpty())
    {
        return {};
    }

    const double anchorY = static_cast<double>(package.base.height) * package.base.anchorY + package.bubble.offsetY;
    if (package.base.height > 0 && anchorY > static_cast<double>(package.base.height) * 0.6)
    {
        return QStringLiteral("当前 bubble.offsetY 可能会让输出气泡显示在角色中部或偏下位置。建议检查 character.json 中的 base.anchorY 和 bubble.offsetY。");
    }

    return {};
}
}

bool CharacterPackageRepository::isValidCharacterId(const QString &characterId)
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

    for (const QChar character : trimmed)
    {
        if (!isAsciiLetterOrNumber(character)
            && character != QLatin1Char('.')
            && character != QLatin1Char('_')
            && character != QLatin1Char('-'))
        {
            return false;
        }
    }

    return true;
}

QString CharacterPackageRepository::charactersRootPath()
{
    return builtInCharactersRootPath();
}

QString CharacterPackageRepository::builtInCharactersRootPath()
{
    return ResourcePaths::charactersRootPath();
}

QString CharacterPackageRepository::userCharactersRootPath()
{
    return QDir(fallbackUserDataPath()).filePath(QStringLiteral("characters"));
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
    const QString userPath = userPackagePath(characterId);
    if (!userPath.isEmpty() && QFileInfo::exists(QDir(userPath).filePath(QStringLiteral("character.json"))))
    {
        return userPath;
    }

    return builtInPackagePath(characterId);
}

QString CharacterPackageRepository::builtInPackagePath(const QString &characterId)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return {};
    }

    return QDir(builtInCharactersRootPath()).filePath(trimmed);
}

QString CharacterPackageRepository::userPackagePath(const QString &characterId)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return {};
    }

    return QDir(userCharactersRootPath()).filePath(trimmed);
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

bool CharacterPackageRepository::hasPackage(const QString &characterId)
{
    return hasUserPackage(characterId) || hasBuiltInPackage(characterId);
}

bool CharacterPackageRepository::hasBuiltInPackage(const QString &characterId)
{
    const QString path = builtInPackagePath(characterId);
    return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath(QStringLiteral("character.json")));
}

bool CharacterPackageRepository::hasUserPackage(const QString &characterId)
{
    const QString path = userPackagePath(characterId);
    return !path.isEmpty() && QFileInfo::exists(QDir(path).filePath(QStringLiteral("character.json")));
}

QVector<CharacterPackageInfo> CharacterPackageRepository::availablePackages()
{
    QVector<CharacterPackageInfo> packages;
    QSet<QString> seenIds;

    const auto appendPackage = [&packages, &seenIds](const QString &id, const QString &path, bool userPackage) {
        if (seenIds.contains(id))
        {
            return;
        }

        QString loadError;
        const CharacterPackage package = CharacterPackageLoader::load(path, &loadError);
        if (!loadError.isEmpty())
        {
            return;
        }

        CharacterPackageInfo info;
        info.id = id;
        info.displayName = package.displayName.trimmed().isEmpty() ? id : package.displayName.trimmed();
        info.packagePath = path;
        info.previewPath = package.previewPath.isEmpty() ? QDir(path).filePath(QStringLiteral("preview.png")) : package.previewPath;
        info.userPackage = userPackage;
        packages.append(info);
        seenIds.insert(id);
    };

    for (const QString &id : collectPackageIds(builtInCharactersRootPath()))
    {
        appendPackage(id, builtInPackagePath(id), false);
    }

    for (const QString &id : collectPackageIds(userCharactersRootPath()))
    {
        appendPackage(id, userPackagePath(id), true);
    }

    return packages;
}

CharacterPackageValidationResult CharacterPackageRepository::validatePackageDirectoryWithDetails(const QString &directoryPath)
{
    CharacterPackageValidationResult result;
    result.valid = validateManifest(
        QDir::cleanPath(directoryPath),
        &result.characterId,
        &result.displayName,
        &result.errorMessage);
    if (result.valid)
    {
        result.warningMessage = bubbleAnchorWarning(QDir::cleanPath(directoryPath));
    }
    return result;
}

bool CharacterPackageRepository::validatePackageDirectory(
    const QString &directoryPath,
    QString *characterId,
    QString *displayName,
    QString *errorMessage)
{
    return validateManifest(QDir::cleanPath(directoryPath), characterId, displayName, errorMessage);
}

bool CharacterPackageRepository::importPackageDirectory(
    const QString &sourceDirectoryPath,
    bool overwrite,
    QString *importedCharacterId,
    QString *errorMessage)
{
    QString characterId;
    QString displayName;
    if (!validatePackageDirectory(sourceDirectoryPath, &characterId, &displayName, errorMessage))
    {
        return false;
    }

    if (hasBuiltInPackage(characterId))
    {
        return setError(errorMessage, QStringLiteral("角色 id 与内置角色重复，不能覆盖内置角色。"));
    }

    const QString targetPath = userPackagePath(characterId);
    const QString sourcePath = QDir::cleanPath(sourceDirectoryPath);
    if (sourcePath == QDir::cleanPath(targetPath))
    {
        if (importedCharacterId != nullptr)
        {
            *importedCharacterId = characterId;
        }
        return true;
    }

    QDir userRoot(userCharactersRootPath());
    if (!userRoot.exists() && !QDir().mkpath(userRoot.absolutePath()))
    {
        return setError(errorMessage, QStringLiteral("无法创建用户角色目录。"));
    }

    const QString importStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString preparedPath = userRoot.filePath(QStringLiteral(".importing-") + characterId + QStringLiteral("-") + importStamp);
    QDir(preparedPath).removeRecursively();

    if (!copyDirectoryRecursively(sourcePath, preparedPath, errorMessage))
    {
        QDir(preparedPath).removeRecursively();
        return false;
    }

    if (!replaceDirectoryWithPreparedImport(preparedPath, targetPath, overwrite, errorMessage))
    {
        QDir(preparedPath).removeRecursively();
        return false;
    }

    if (importedCharacterId != nullptr)
    {
        *importedCharacterId = characterId;
    }
    return true;
}

bool CharacterPackageRepository::exportPackageDirectory(
    const QString &characterId,
    const QString &targetParentDirectoryPath,
    bool overwrite,
    QString *exportedPath,
    QString *errorMessage)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return setError(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    const QString sourcePath = packagePath(trimmed);
    if (sourcePath.isEmpty() || !QFileInfo::exists(QDir(sourcePath).filePath(QStringLiteral("character.json"))))
    {
        return setError(errorMessage, QStringLiteral("角色包不存在或不可导出。"));
    }

    QString validationId;
    QString displayName;
    if (!validatePackageDirectory(sourcePath, &validationId, &displayName, errorMessage))
    {
        return false;
    }

    const QFileInfo targetParentInfo(QDir::cleanPath(targetParentDirectoryPath));
    if (!targetParentInfo.exists() || !targetParentInfo.isDir())
    {
        return setError(errorMessage, QStringLiteral("导出目标目录不存在。"));
    }

    const QString targetPath = QDir(targetParentInfo.absoluteFilePath()).filePath(trimmed);
    const QString cleanedSourcePath = QDir::cleanPath(sourcePath);
    const QString cleanedTargetPath = QDir::cleanPath(targetPath);
    if (cleanedSourcePath == cleanedTargetPath)
    {
        return setError(errorMessage, QStringLiteral("导出目标不能与源角色目录相同。"));
    }

    QDir targetRoot(targetParentInfo.absoluteFilePath());
    const QString exportStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString preparedPath = targetRoot.filePath(QStringLiteral(".exporting-") + trimmed + QStringLiteral("-") + exportStamp);
    QDir(preparedPath).removeRecursively();

    if (!copyDirectoryRecursively(cleanedSourcePath, preparedPath, errorMessage))
    {
        QDir(preparedPath).removeRecursively();
        return false;
    }

    const ResolvedCharacterPersona resolvedPersona = resolveCharacterPersona(trimmed);
    if (resolvedPersona.hasUserOverride && !resolvedPersona.persona.isEmpty())
    {
        if (!CharacterPersonaStore::updateManifestPersonaAtomic(
                QDir(preparedPath).filePath(QStringLiteral("character.json")),
                resolvedPersona.persona,
                errorMessage))
        {
            QDir(preparedPath).removeRecursively();
            return false;
        }
    }

    if (!replaceDirectoryWithPreparedImport(preparedPath, cleanedTargetPath, overwrite, errorMessage))
    {
        QDir(preparedPath).removeRecursively();
        return false;
    }

    if (exportedPath != nullptr)
    {
        *exportedPath = cleanedTargetPath;
    }
    return true;
}

bool CharacterPackageRepository::deleteUserPackage(const QString &characterId, QString *errorMessage)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return setError(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    const QString targetPath = userPackagePath(trimmed);
    const QString userRootPath = QDir::cleanPath(userCharactersRootPath());
    const QString cleanedTargetPath = QDir::cleanPath(targetPath);
    if (cleanedTargetPath == userRootPath || !cleanedTargetPath.startsWith(userRootPath + QLatin1Char('/')))
    {
        return setError(errorMessage, QStringLiteral("删除目标不在用户角色目录内。"));
    }

    QDir targetDirectory(cleanedTargetPath);
    if (!targetDirectory.exists())
    {
        return setError(errorMessage, QStringLiteral("用户角色目录不存在。"));
    }

    const QFileInfo manifest(targetDirectory.filePath(QStringLiteral("character.json")));
    if (!manifest.isFile())
    {
        return setError(errorMessage, QStringLiteral("目标目录不是有效的用户角色包。"));
    }

    if (!targetDirectory.removeRecursively())
    {
        return setError(errorMessage, QStringLiteral("删除用户角色目录失败。"));
    }

    if (!hasBuiltInPackage(trimmed))
    {
        const CharacterPersonaStore personaStore(effectivePersonaStorePath(QString()));
        personaStore.removeOverride(trimmed);
    }

    return true;
}

ResolvedCharacterPersona CharacterPackageRepository::resolveCharacterPersona(
    const QString &characterId,
    const QString &overrideFilePath)
{
    const QString trimmed = characterId.trimmed();
    QString packagePersona;
    if (isValidCharacterId(trimmed) && hasPackage(trimmed))
    {
        const QString path = packagePath(trimmed);
        if (!path.isEmpty())
        {
            QString loadError;
            const CharacterPackage package = CharacterPackageLoader::load(path, &loadError);
            if (loadError.isEmpty())
            {
                packagePersona = package.persona;
            }
        }
    }

    const CharacterPersonaStore personaStore(effectivePersonaStorePath(overrideFilePath));
    return personaStore.resolvePersona(trimmed, packagePersona);
}

bool CharacterPackageRepository::saveCharacterPersona(
    const QString &characterId,
    const QString &persona,
    QString *errorMessage,
    const QString &overrideFilePath)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return setError(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    const CharacterPersonaStore personaStore(effectivePersonaStorePath(overrideFilePath));
    if (hasUserPackage(trimmed))
    {
        const QString manifestPath = QDir(userPackagePath(trimmed)).filePath(QStringLiteral("character.json"));
        if (!CharacterPersonaStore::updateManifestPersonaAtomic(manifestPath, persona, errorMessage))
        {
            return false;
        }

        // 写入用户包后同步清理同 id 覆盖层条目，避免旧覆盖层遮蔽刚写回的人设
        personaStore.removeOverride(trimmed);
        return true;
    }

    if (hasBuiltInPackage(trimmed))
    {
        return personaStore.setOverride(trimmed, persona, errorMessage);
    }

    return setError(errorMessage, QStringLiteral("角色包不存在。"));
}

bool CharacterPackageRepository::resetCharacterPersonaToDefault(
    const QString &characterId,
    QString *errorMessage,
    const QString &overrideFilePath)
{
    const QString trimmed = characterId.trimmed();
    if (!isValidCharacterId(trimmed))
    {
        return setError(errorMessage, QStringLiteral("角色 id 无效。"));
    }

    const CharacterPersonaStore personaStore(effectivePersonaStorePath(overrideFilePath));
    if (hasUserPackage(trimmed))
    {
        const QString manifestPath = QDir(userPackagePath(trimmed)).filePath(QStringLiteral("character.json"));
        if (!CharacterPersonaStore::updateManifestPersonaAtomic(manifestPath, QString(), errorMessage))
        {
            return false;
        }

        personaStore.removeOverride(trimmed);
        return true;
    }

    if (hasBuiltInPackage(trimmed))
    {
        return personaStore.removeOverride(trimmed, errorMessage);
    }

    return setError(errorMessage, QStringLiteral("角色包不存在。"));
}
