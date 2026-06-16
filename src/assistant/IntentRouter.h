#pragma once

#include "UserIntent.h"

#include <QString>

class IntentRouter
{
public:
    UserIntent route(const QString &text) const;
};
