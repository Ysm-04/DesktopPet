#include "ConversationManager.h"

#include <QtGlobal>
#include <utility>

ConversationManager::ConversationManager()
    : m_systemPrompt(QStringLiteral("你是一个桌面宠物助手。回复要简短、自然，适合显示在桌宠气泡里。"))
{
}

ConversationManager::~ConversationManager()
{
    cancel();
}

bool ConversationManager::isBusy() const
{
    return m_provider && m_provider->isBusy();
}

bool ConversationManager::hasHistory() const
{
    return !m_history.isEmpty();
}

QVector<ChatMessage> ConversationManager::history() const
{
    return m_history;
}

int ConversationManager::prunedHistoryMessageCount() const
{
    return m_prunedHistoryMessageCount;
}

void ConversationManager::setHistory(const QVector<ChatMessage> &history)
{
    m_history.clear();
    for (const ChatMessage &message : history)
    {
        if (message.role.trimmed().isEmpty() || message.content.trimmed().isEmpty())
        {
            continue;
        }

        m_history.append(message);
    }

    m_prunedHistoryMessageCount = 0;
    if (m_history.size() % 2 != 0)
    {
        m_history.erase(m_history.begin());
        ++m_prunedHistoryMessageCount;
    }
    pruneHistory();
}

void ConversationManager::setRequestContextMessageLimit(int maxMessages)
{
    m_maxRequestContextMessages = qMax(0, maxMessages);
}

void ConversationManager::setMemoryHistoryMessageLimit(int maxMessages)
{
    m_maxStoredHistoryMessages = qMax(0, maxMessages);
    pruneHistory();
}

bool ConversationManager::setProvider(std::unique_ptr<LLMProvider> provider)
{
    if (isBusy())
    {
        return false;
    }

    m_provider = std::move(provider);
    return true;
}

void ConversationManager::sendUserMessage(const QString &message, ResponseCallback callback)
{
    const QString content = message.trimmed();
    if (content.isEmpty())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("Message is empty."), 0});
        }
        return;
    }

    if (!m_provider)
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("AI provider is not ready."), 0});
        }
        return;
    }

    if (isBusy())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("AI request is already running."), 0});
        }
        return;
    }

    const ChatMessage userMessage{QStringLiteral("user"), content};
    m_provider->sendChatRequest(buildRequest(userMessage), [this, userMessage, callback = std::move(callback)](const ChatResponse &response) mutable {
        if (response.success)
        {
            appendExchange(userMessage, {QStringLiteral("assistant"), response.content});
        }

        if (callback)
        {
            callback(response);
        }
    });
}

void ConversationManager::sendUserMessageStreaming(const QString &message, StreamCallback streamCallback, ResponseCallback callback)
{
    const QString content = message.trimmed();
    if (content.isEmpty())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("Message is empty."), 0});
        }
        return;
    }

    if (!m_provider)
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("AI provider is not ready."), 0});
        }
        return;
    }

    if (isBusy())
    {
        if (callback)
        {
            callback({false, {}, QStringLiteral("AI request is already running."), 0});
        }
        return;
    }

    const ChatMessage userMessage{QStringLiteral("user"), content};
    m_provider->sendStreamingChatRequest(
        buildRequest(userMessage),
        std::move(streamCallback),
        [this, userMessage, callback = std::move(callback)](const ChatResponse &response) mutable {
            if (response.success)
            {
                appendExchange(userMessage, {QStringLiteral("assistant"), response.content});
            }

            if (callback)
            {
                callback(response);
            }
        });
}

void ConversationManager::cancel()
{
    if (m_provider)
    {
        m_provider->cancel();
    }
}

void ConversationManager::clear()
{
    if (isBusy())
    {
        cancel();
    }

    m_history.clear();
    m_prunedHistoryMessageCount = 0;
}

ChatRequest ConversationManager::buildRequest(const ChatMessage &userMessage) const
{
    ChatRequest request;
    if (!m_systemPrompt.trimmed().isEmpty())
    {
        request.messages.append({QStringLiteral("system"), m_systemPrompt});
    }

    const int firstHistoryIndex = qMax(0, m_history.size() - m_maxRequestContextMessages);
    for (int index = firstHistoryIndex; index < m_history.size(); ++index)
    {
        request.messages.append(m_history.at(index));
    }
    request.messages.append(userMessage);
    return request;
}

void ConversationManager::appendExchange(const ChatMessage &userMessage, const ChatMessage &assistantMessage)
{
    m_history.append(userMessage);
    m_history.append(assistantMessage);
    pruneHistory();
}

void ConversationManager::pruneHistory()
{
    const int maxMessages = normalizedStoredHistoryLimit();
    if (m_history.size() <= maxMessages)
    {
        return;
    }

    const int removeCount = m_history.size() - maxMessages;
    m_history.erase(m_history.begin(), m_history.begin() + removeCount);
    m_prunedHistoryMessageCount += removeCount;
}

int ConversationManager::normalizedStoredHistoryLimit() const
{
    if (m_maxStoredHistoryMessages <= 0)
    {
        return 0;
    }

    if (m_maxStoredHistoryMessages == 1)
    {
        return 2;
    }

    return m_maxStoredHistoryMessages - (m_maxStoredHistoryMessages % 2);
}
