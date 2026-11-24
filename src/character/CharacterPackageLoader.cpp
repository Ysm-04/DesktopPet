#include "CharacterPackageLoader.h"

#include "CharacterPersonaStore.h"
#include "CharacterMediaSpec.h"
#include "CharacterEffects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QtGlobal>

namespace
{

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

QString parseOptionalPersona(const QJsonObject &root)
{
    const QJsonValue personaValue = root.value(QStringLiteral("persona"));
    if (personaValue.isString())
    {
        return CharacterPersonaStore::sanitizePersona(personaValue.toString());
    }

    if (personaValue.isObject())
    {
        const QJsonObject personaObject = personaValue.toObject();
        const QJsonValue textValue = personaObject.value(QStringLiteral("text"));
        if (textValue.isString())
        {
            return CharacterPersonaStore::sanitizePersona(textValue.toString());
        }

        const QJsonValue promptValue = personaObject.value(QStringLiteral("prompt"));
        if (promptValue.isString())
        {
            return CharacterPersonaStore::sanitizePersona(promptValue.toString());
        }
    }

    return QString();
}

QStringList parseDialogueGroup(const QJsonObject &dialoguesObject, const QString &key)
{
    const QJsonValue groupValue = dialoguesObject.value(key);
    if (!groupValue.isArray())
    {
        return {};
    }

    const QJsonArray array = groupValue.toArray();
    QStringList lines;
    QSet<QString> seen;
    for (const QJsonValue &entry : array)
    {
        if (!entry.isString())
        {
            continue;
        }

        QString cleaned = entry.toString().simplified();
        if (cleaned.isEmpty())
        {
            continue;
        }

        if (cleaned.size() > CharacterDialogueMaxLineChars)
        {
            cleaned = cleaned.left(CharacterDialogueMaxLineChars).trimmed();
        }

        if (cleaned.isEmpty() || seen.contains(cleaned))
        {
            continue;
        }

        seen.insert(cleaned);
        lines.append(cleaned);
        if (lines.size() >= CharacterDialogueMaxLinesPerGroup)
        {
            break;
        }
    }

    return lines;
}

CharacterDialogues parseOptionalDialogues(const QJsonObject &root)
{
    CharacterDialogues dialogues;
    const QJsonValue dialoguesValue = root.value(QStringLiteral("dialogues"));
    if (!dialoguesValue.isObject())
    {
        return dialogues;
    }

    const QJsonObject dialoguesObject = dialoguesValue.toObject();
    dialogues.tap = parseDialogueGroup(dialoguesObject, QStringLiteral("tap"));
    dialogues.headpat = parseDialogueGroup(dialoguesObject, QStringLiteral("headpat"));
    dialogues.dragDrop = parseDialogueGroup(dialoguesObject, QStringLiteral("dragDrop"));
    dialogues.greetMorning = parseDialogueGroup(dialoguesObject, QStringLiteral("greetMorning"));
    dialogues.greetAfternoon = parseDialogueGroup(dialoguesObject, QStringLiteral("greetAfternoon"));
    dialogues.greetEvening = parseDialogueGroup(dialoguesObject, QStringLiteral("greetEvening"));
    dialogues.greetLateNight = parseDialogueGroup(dialoguesObject, QStringLiteral("greetLateNight"));
    dialogues.sedentary = parseDialogueGroup(dialoguesObject, QStringLiteral("sedentary"));
    dialogues.lateNightCare = parseDialogueGroup(dialoguesObject, QStringLiteral("lateNightCare"));
    dialogues.agentWorking = parseDialogueGroup(dialoguesObject, QStringLiteral("agentWorking"));
    dialogues.agentDone = parseDialogueGroup(dialoguesObject, QStringLiteral("agentDone"));
    dialogues.agentNeedsInput = parseDialogueGroup(dialoguesObject, QStringLiteral("agentNeedsInput"));
    dialogues.agentError = parseDialogueGroup(dialoguesObject, QStringLiteral("agentError"));
    return dialogues;
}
}

CharacterPackage CharacterPackageLoader::load(const QString &packagePath, QString *errorMessage)
{
    if (errorMessage) errorMessage->clear();
    CharacterPackage package;
    package.packagePath = QDir::cleanPath(packagePath);
    if (!CharacterMediaSpec::preflightPackage(package.packagePath, errorMessage))
        return CharacterPackage();

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
    if (!CharacterMediaSpec::isSupportedSchema(root.value(QStringLiteral("schemaVersion"))))
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
    package.persona = parseOptionalPersona(root);
    package.base = parseBase(root);
    package.bubble = parseBubble(root);
    package.dialogues = parseOptionalDialogues(root);

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
        // v1 permits a missing optional preview; v2 validates every declared resource.
        if (package.schemaVersion == 2)
        {
            if (!CharacterMediaSpec::resolveResource(package.packagePath, previewRelativePath, &package.previewPath, errorMessage))
                return CharacterPackage();
            if (!QFileInfo(package.previewPath).isFile())
            {
                setError(errorMessage, QStringLiteral("preview 必须是包内文件。"));
                return CharacterPackage();
            }
        }
        else package.previewPath = packageDirectory.filePath(previewRelativePath);
    }

    const QJsonValue statesValue = root.value(QStringLiteral("states"));
    if (!statesValue.isObject() || statesValue.toObject().isEmpty())
    {
        setError(errorMessage, QStringLiteral("Character package states is empty."));
        return CharacterPackage();
    }

    const QJsonObject statesObject = statesValue.toObject();
    if (statesObject.size() > 64)
    {
        setError(errorMessage, QStringLiteral("角色包最多 64 个状态。"));
        return CharacterPackage();
    }
    for (auto iterator = statesObject.constBegin(); iterator != statesObject.constEnd(); ++iterator)
    {
        if (!iterator.value().isObject())
        {
            if (package.schemaVersion == 2)
            {
                setError(errorMessage, QStringLiteral("v2 状态必须是对象：") + iterator.key());
                return CharacterPackage();
            }
            continue;
        }

        const QString stateName = iterator.key();
        const QJsonObject stateObject = iterator.value().toObject();
        CharacterState state;
        QString stateError;
        if (!CharacterMediaSpec::parseState(package.schemaVersion, stateName, stateObject,
                                            package.packagePath, &state, &stateError))
        {
            if (package.schemaVersion == 2)
            {
                setError(errorMessage, stateError);
                return CharacterPackage();
            }
            continue;
        }

        package.states.insert(state.name, state);
    }

    if (package.schemaVersion == 2)
    {
        for (const CharacterState &state : package.states)
        {
            if (!state.nextState.isEmpty() && !package.hasState(state.nextState))
            {
                setError(errorMessage, QStringLiteral("next 指向不存在的状态：") + state.name);
                return CharacterPackage();
            }
        }
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

    if (!CharacterEffects::parse(root, package.packagePath, package.states, &package.effects, errorMessage))
        return CharacterPackage();
    return package;
}

bool CharacterPackageLoader::setError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }

    return false;
}
