#pragma once

#include "LLMProvider.h"
#include "LongTermMemoryTypes.h"

#include <QString>
#include <QVector>

#include <functional>
#include <memory>

class ConversationManager
{
public:
    using ResponseCallback = LLMProvider::ResponseCallback;
    using StreamCallback = LLMProvider::StreamCallback;
    using ArchiveHistoryLoader = std::function<QVector<ChatMessage>()>;
    using LongTermMemoryLoader = std::function<QVector<LongTermMemoryItem>()>;

    ConversationManager();
    ~ConversationManager();

    bool isBusy() const;
    bool hasHistory() const;
    QVector<ChatMessage> history() const;
    QVector<ChatMessage> fullHistory() const;
    int prunedHistoryMessageCount() const;
    void setHistory(const QVector<ChatMessage> &history);
    void setRequestContextMessageLimit(int maxMessages);
    void setRequestContextUseMax(bool useMax);
    void setRequestContextMaxChars(int maxChars);
    void setMemoryHistoryMessageLimit(int maxMessages);
    void setHistoryRecallEnabled(bool enabled);
    void setArchiveHistoryLoader(ArchiveHistoryLoader loader);
    void setLongTermMemoryEnabled(bool enabled);
    void setLongTermMemoryLoader(LongTermMemoryLoader loader);
    void setConversationMetadata(const QString &provider, const QString &model);
    void setCurrentCharacterId(const QString &characterId);
    QString currentCharacterId() const;
    void setCharacterPersona(const QString &persona);
    QString characterPersona() const;
    QString effectiveSystemPrompt() const;
    static QString defaultPersonaPrompt();
    static QString fixedCapabilityPrompt();
    bool setProvider(std::unique_ptr<LLMProvider> provider);
    ChatRequest buildRequestForUserMessage(const QString &message) const;
    void appendExternalExchange(const QString &userMessage, const QString &assistantMessage);
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
    ArchiveHistoryLoader m_archiveHistoryLoader;
    LongTermMemoryLoader m_longTermMemoryLoader;
    QString m_characterPersona;
    QString m_currentCharacterId = QStringLiteral("shiroko");
    QString m_currentProvider;
    QString m_currentModel;
    int m_maxRequestContextMessages = 12;
    bool m_requestContextUseMax = false;
    int m_requestContextMaxChars = 32000;
    int m_maxStoredHistoryMessages = 200;
    bool m_historyRecallEnabled = true;
    bool m_longTermMemoryEnabled = true;
    int m_prunedHistoryMessageCount = 0;
};
