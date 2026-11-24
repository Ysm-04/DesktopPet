#include "CharacterPackage.h"

namespace
{
struct DialogueGroupAccessor
{
    const char *name;
    QStringList CharacterDialogues::*member;
};

const DialogueGroupAccessor DialogueGroupAccessors[] = {
    {"tap", &CharacterDialogues::tap},
    {"headpat", &CharacterDialogues::headpat},
    {"dragDrop", &CharacterDialogues::dragDrop},
    {"greetMorning", &CharacterDialogues::greetMorning},
    {"greetAfternoon", &CharacterDialogues::greetAfternoon},
    {"greetEvening", &CharacterDialogues::greetEvening},
    {"greetLateNight", &CharacterDialogues::greetLateNight},
    {"sedentary", &CharacterDialogues::sedentary},
    {"lateNightCare", &CharacterDialogues::lateNightCare},
    {"agentWorking", &CharacterDialogues::agentWorking},
    {"agentDone", &CharacterDialogues::agentDone},
    {"agentNeedsInput", &CharacterDialogues::agentNeedsInput},
    {"agentError", &CharacterDialogues::agentError},
};
}

bool CharacterDialogues::isEmpty() const
{
    for (const DialogueGroupAccessor &accessor : DialogueGroupAccessors)
    {
        if (!(this->*(accessor.member)).isEmpty())
        {
            return false;
        }
    }

    return true;
}

QStringList CharacterDialogues::linesForGroup(const QString &groupName) const
{
    const QString trimmed = groupName.trimmed();
    for (const DialogueGroupAccessor &accessor : DialogueGroupAccessors)
    {
        if (trimmed == QLatin1String(accessor.name))
        {
            return this->*(accessor.member);
        }
    }

    return {};
}

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

