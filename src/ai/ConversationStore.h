#pragma once

#include "LLMTypes.h"

#include <QString>
#include <QVector>

class ConversationStore
{
public:
    explicit ConversationStore(QString filePath);

    QVector<ChatMessage> load(int maxMessages, QString *errorMessage = nullptr) const;
    bool save(const QVector<ChatMessage> &messages, int maxMessages) const;
    bool clear() const;
    QString filePath() const;

private:
    QVector<ChatMessage> readMessages(QString *errorMessage = nullptr) const;
    QVector<ChatMessage> recentMessages(const QVector<ChatMessage> &messages, int maxMessages) const;

    QString m_filePath;
};
