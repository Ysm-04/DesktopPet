#pragma once

#include "LLMProvider.h"

#include <QString>
#include <QVector>

#include <memory>

class ConversationManager
{
public:
    using ResponseCallback = LLMProvider::ResponseCallback;

    ConversationManager();
    ~ConversationManager();

    bool isBusy() const;
    bool hasHistory() const;
    bool setProvider(std::unique_ptr<LLMProvider> provider);
    void sendUserMessage(const QString &message, ResponseCallback callback);
    void cancel();
    void clear();

private:
    ChatRequest buildRequest(const ChatMessage &userMessage) const;
    void appendExchange(const ChatMessage &userMessage, const ChatMessage &assistantMessage);
    void trimHistory();

    std::unique_ptr<LLMProvider> m_provider;
    QVector<ChatMessage> m_history;
    QString m_systemPrompt;
    int m_maxHistoryMessages = 12;
};
