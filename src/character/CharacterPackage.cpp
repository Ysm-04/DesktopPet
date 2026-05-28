#include "CharacterPackage.h"

bool CharacterPackage::hasState(const QString &stateName) const
{
    return states.contains(stateName);
}

const CharacterState *CharacterPackage::state(const QString &stateName) const
{
    auto iterator = states.constFind(stateName);
    if (iterator == states.constEnd())
    {
        return nullptr;
    }

    return &iterator.value();
}

