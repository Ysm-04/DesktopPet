#include "CharacterPackageLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace
{
constexpr int SupportedSchemaVersion = 1;

QString requiredString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString())
    {
        return QString();
    }

    return value.toString().trimmed();
}
}

CharacterPackage CharacterPackageLoader::load(const QString &packagePath, QString *errorMessage)
{
    CharacterPackage package;
    package.packagePath = QDir::cleanPath(packagePath);

    const QDir packageDirectory(package.packagePath);
    if (!packageDirectory.exists())
    {
        setError(errorMessage, QStringLiteral("Character package directory does not exist."));
        return package;
    }

    QFile file(packageDirectory.filePath(QStringLiteral("character.json")));
    if (!file.exists())
    {
        setError(errorMessage, QStringLiteral("character.json does not exist."));
        return package;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        setError(errorMessage, QStringLiteral("Unable to read character.json."));
        return package;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        setError(errorMessage, QStringLiteral("character.json has invalid format."));
        return package;
    }

    const QJsonObject root = document.object();
    package.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (package.schemaVersion != SupportedSchemaVersion)
    {
        setError(errorMessage, QStringLiteral("Character package schemaVersion is not supported."));
        return package;
    }

    package.id = requiredString(root, QStringLiteral("id"));
    package.displayName = requiredString(root, QStringLiteral("displayName"));
    package.author = requiredString(root, QStringLiteral("author"));
    package.version = requiredString(root, QStringLiteral("version"));
    package.defaultState = requiredString(root, QStringLiteral("defaultState"));

    if (package.id.isEmpty())
    {
        setError(errorMessage, QStringLiteral("Character package id is empty."));
        return CharacterPackage();
    }

    if (package.defaultState.isEmpty())
    {
        setError(errorMessage, QStringLiteral("Character package defaultState is empty."));
        return CharacterPackage();
    }

    const QString previewRelativePath = requiredString(root, QStringLiteral("preview"));
    if (!previewRelativePath.isEmpty())
    {
        package.previewPath = packageDirectory.filePath(previewRelativePath);
    }

    const QJsonValue statesValue = root.value(QStringLiteral("states"));
    if (!statesValue.isObject() || statesValue.toObject().isEmpty())
    {
        setError(errorMessage, QStringLiteral("Character package states is empty."));
        return CharacterPackage();
    }

    const QJsonObject statesObject = statesValue.toObject();
    for (auto iterator = statesObject.constBegin(); iterator != statesObject.constEnd(); ++iterator)
    {
        if (!iterator.value().isObject())
        {
            continue;
        }

        const QString stateName = iterator.key();
        const QJsonObject stateObject = iterator.value().toObject();
        CharacterState state;
        state.name = stateName;
        state.relativePath = requiredString(stateObject, QStringLiteral("path"));
        state.fps = stateObject.value(QStringLiteral("fps")).toInt(0);
        state.loop = stateObject.value(QStringLiteral("loop")).toBool(true);
        state.nextState = requiredString(stateObject, QStringLiteral("next"));

        if (state.relativePath.isEmpty() || state.fps <= 0)
        {
            continue;
        }

        state.framePaths = collectPngFrames(packageDirectory.filePath(state.relativePath));
        if (state.framePaths.isEmpty())
        {
            continue;
        }

        package.states.insert(state.name, state);
    }

    if (!package.hasState(package.defaultState))
    {
        setError(errorMessage, QStringLiteral("defaultState does not exist or has no usable frames."));
        return CharacterPackage();
    }

    if (!package.hasState(QStringLiteral("idle")))
    {
        setError(errorMessage, QStringLiteral("idle state does not exist or has no usable frames."));
        return CharacterPackage();
    }

    return package;
}

QStringList CharacterPackageLoader::collectPngFrames(const QString &stateDirectoryPath)
{
    const QDir stateDirectory(stateDirectoryPath);
    if (!stateDirectory.exists())
    {
        return {};
    }

    const QFileInfoList frameFiles = stateDirectory.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.PNG")},
        QDir::Files,
        QDir::Name);

    QStringList framePaths;
    for (const QFileInfo &frameFile : frameFiles)
    {
        framePaths.append(frameFile.absoluteFilePath());
    }

    return framePaths;
}

bool CharacterPackageLoader::setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}
