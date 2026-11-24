#pragma once

#include "LLMProvider.h"
#include "LongTermMemoryStore.h"
#include "LongTermMemoryTypes.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

struct ExtractedMemoryCandidate
{
    LongTermMemoryCategory category = LongTermMemoryCategory::Fact;
    QString content;
};

struct ExplicitExtractionResult
{
    bool success = false;
    QVector<LongTermMemoryItem> savedItems;
    QString errorMessage;
};

class LongTermMemoryExtractor
{
public:
    LongTermMemoryExtractor();
    ~LongTermMemoryExtractor();

    bool isBusy() const;
    void cancel();

    void extractAsync(
        std::unique_ptr<LLMProvider> provider,
        const QString &userMessage,
        const QString &assistantMessage,
        const LongTermMemoryStore &store,
        std::function<void(int)> completedCallback = {},
        const QVector<ChatMessage> &recentContext = {});

    void extractFromContextAsync(
        std::unique_ptr<LLMProvider> provider,
        const QString &userInstruction,
        const QVector<ChatMessage> &recentHistory,
        const LongTermMemoryStore &store,
        std::function<void(const ExplicitExtractionResult &)> completedCallback);

    static bool shouldConsiderExchange(const QString &userMessage, const QString &assistantMessage);
    static ChatRequest buildExtractionRequest(
        const QString &userMessage,
        const QString &assistantMessage,
        const QVector<LongTermMemoryItem> &existingItems = {},
        const QVector<ChatMessage> &recentContext = {});
    static ChatRequest buildExplicitContextExtractionRequest(
        const QString &userInstruction,
        const QVector<ChatMessage> &recentHistory,
        const QVector<LongTermMemoryItem> &existingItems = {});
    static QVector<ExtractedMemoryCandidate> parseExtractionResponse(const QString &rawResponse);

private:
    std::unique_ptr<LLMProvider> m_activeProvider;
    std::shared_ptr<bool> m_alive;
    bool m_busy = false;
};
