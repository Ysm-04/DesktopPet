#pragma once

#include "LLMProvider.h"

#include <QString>
#include <QVector>

#include <memory>

class ConversationManager
{
public:
    using ResponseCallback = LLMProvider::ResponseCallback;
    using StreamCallback = LLMProvider::StreamCallback;

    ConversationManager();
    ~ConversationManager();

    bool isBusy() const;
    bool hasHistory() const;
    QVector<ChatMessage> history() const;
    int prunedHistoryMessageCount() const;
    void setHistory(const QVector<ChatMessage> &history);
    void setRequestContextMessageLimit(int maxMessages);
    void setMemoryHistoryMessageLimit(int maxMessages);
    bool setProvider(std::unique_ptr<LLMProvider> provider);
    void sendUserMessage(const QString &message, ResponseCallback callback);
    void sendUserMessageStreaming(const QString &message, StreamCallback streamCallback, ResponseCallback callback);
    void cancel();
    void clear();

private:
    ChatRequest buildRequest(const ChatMessage &userMessage) const;
    void appendExchange(const ChatMessage &userMessage, const ChatMessage &assistantMessage);
    void pruneHistory();
    int normalizedStoredHistoryLimit() const;

    std::unique_ptr<LLMProvider> m_provider;
    QVector<ChatMessage> m_history;
    QString m_systemPrompt;
    int m_maxRequestContextMessages = 12;
    int m_maxStoredHistoryMessages = 200;
    int m_prunedHistoryMessageCount = 0;
};
