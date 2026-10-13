#pragma once

#include "IntentRouter.h"

#include <QString>

enum class CommandDispatchAction
{
    Chat,
    Reminder,
    Weather,
};

struct CommandDispatchResult
{
    CommandDispatchAction action = CommandDispatchAction::Chat;
    UserIntent intent;
    QString message;
};

class CommandDispatcher
{
public:
    CommandDispatchResult dispatch(const QString &text) const;

private:
    IntentRouter m_intentRouter;
};
