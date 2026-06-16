#include "CharacterPackageLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QtGlobal>

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

int optionalPositiveInt(const QJsonObject &object, const QString &key, int fallback)
{
    const int value = object.value(key).toInt(fallback);
    return value > 0 ? value : fallback;
}

double optionalPositiveDouble(const QJsonObject &object, const QString &key, double fallback)
{
    const double value = object.value(key).toDouble(fallback);
    return value > 0.0 ? value : fallback;
}

double optionalNormalizedDouble(const QJsonObject &object, const QString &key, double fallback)
{
    const double value = object.value(key).toDouble(fallback);
    return qBound(0.0, value, 1.0);
}

double optionalDouble(const QJsonObject &object, const QString &key, double fallback)
{
    return object.value(key).toDouble(fallback);
}

CharacterBase parseBase(const QJsonObject &root)
{
    CharacterBase base;
    const QJsonValue baseValue = root.value(QStringLiteral("base"));
    if (!baseValue.isObject())
    {
        return base;
    }

    const QJsonObject baseObject = baseValue.toObject();
    base.width = optionalPositiveInt(baseObject, QStringLiteral("width"), base.width);
    base.height = optionalPositiveInt(baseObject, QStringLiteral("height"), base.height);
    base.scale = optionalPositiveDouble(baseObject, QStringLiteral("scale"), base.scale);
    base.anchorX = optionalNormalizedDouble(baseObject, QStringLiteral("anchorX"), base.anchorX);
    base.anchorY = optionalNormalizedDouble(baseObject, QStringLiteral("anchorY"), base.anchorY);
    return base;
}

CharacterBubble parseBubble(const QJsonObject &root)
{
    CharacterBubble bubble;
    const QJsonValue bubbleValue = root.value(QStringLiteral("bubble"));
    if (!bubbleValue.isObject())
    {
        return bubble;
    }

    const QJsonObject bubbleObject = bubbleValue.toObject();
    bubble.offsetX = optionalDouble(bubbleObject, QStringLiteral("offsetX"), bubble.offsetX);
    bubble.offsetY = optionalDouble(bubbleObject, QStringLiteral("offsetY"), bubble.offsetY);
    return bubble;
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
    if (package.id.isEmpty())
    {
        package.id = QFileInfo(package.packagePath).fileName().trimmed();
    }
    package.displayName = requiredString(root, QStringLiteral("displayName"));
    package.author = requiredString(root, QStringLiteral("author"));
    package.version = requiredString(root, QStringLiteral("version"));
    package.defaultState = requiredString(root, QStringLiteral("defaultState"));
    package.base = parseBase(root);
    package.bubble = parseBubble(root);

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
