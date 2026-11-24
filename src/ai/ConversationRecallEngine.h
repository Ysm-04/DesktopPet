#pragma once

#include "LLMTypes.h"

#include <QString>
#include <QVector>

namespace ConversationRecallEngine
{
struct RecallOptions
{
    int maxExchanges = 4;
    int maxTotalChars = 2400;
    int maxExcerptChars = 320;
};

struct RecalledExchange
{
    ChatMessage userMessage;
    ChatMessage assistantMessage;
    double score = 0.0;
};

QVector<RecalledExchange> recallRelevantExchanges(
    const QString &query,
    const QVector<ChatMessage> &archiveMessages,
    int recentWindowMessageCount,
    const RecallOptions &options = {});

QString formatRecalledContextBlock(
    const QVector<RecalledExchange> &exchanges,
    const RecallOptions &options = {});
}
