#pragma once

#include "LongTermMemoryTypes.h"

#include <QString>

enum class LongTermMemoryCommandType
{
    None,
    Remember,
    RememberFromContext,
    Forget,
    List,
    ClearRejected,
};

struct LongTermMemoryCommand
{
    LongTermMemoryCommandType type = LongTermMemoryCommandType::None;
    QString payload;
    LongTermMemoryCategory suggestedCategory = LongTermMemoryCategory::Fact;
};

class LongTermMemoryCommandParser
{
public:
    static LongTermMemoryCommand parse(const QString &input);
};
