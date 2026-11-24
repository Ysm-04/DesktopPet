#pragma once

#include "CharacterPackage.h"
#include <QJsonObject>

namespace CharacterEffects
{
// Core-only validation shared by loading, import and the author tools.
bool parse(const QJsonObject &root, const QString &directory,
           const QMap<QString, CharacterState> &states, CharacterEffectSpec *out, QString *error);
}
