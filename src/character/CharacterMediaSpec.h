#pragma once

#include "CharacterPackage.h"
#include <QJsonObject>

// Shared by package loading and import/export validation.
namespace CharacterMediaSpec
{
bool isSupportedSchema(const QJsonValue &value);
bool preflightPackage(const QString &directory, QString *error);
bool isSafeRelativePath(QString path);
bool resolveResource(const QString &root, const QString &relativePath, QString *resolved, QString *error);
bool parseState(int schema, const QString &name, const QJsonObject &object,
                const QString &root, CharacterState *state, QString *error);
}
