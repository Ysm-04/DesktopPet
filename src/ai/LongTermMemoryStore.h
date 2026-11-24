#pragma once

#include "LongTermMemoryTypes.h"

#include <QString>
#include <QVector>

class LongTermMemoryStore
{
public:
    static constexpr int DefaultMaxItems = 50;
    static constexpr int MaxContentChars = 120;
    static constexpr int DefaultPromptMaxChars = 2000;

    explicit LongTermMemoryStore(QString filePath, int maxItems = DefaultMaxItems);

    QVector<LongTermMemoryItem> load(QString *errorMessage = nullptr) const;
    bool saveAll(const QVector<LongTermMemoryItem> &items, QString *errorMessage = nullptr) const;

    bool upsert(
        const QString &content,
        LongTermMemoryCategory category,
        LongTermMemorySource source,
        LongTermMemoryItem *savedItem = nullptr,
        bool *updatedExisting = nullptr,
        QString *errorMessage = nullptr) const;

    bool updateItem(
        const QString &id,
        const QString &content,
        LongTermMemoryCategory category,
        LongTermMemoryItem *updatedItem = nullptr,
        QString *errorMessage = nullptr) const;

    bool removeById(const QString &id, QString *errorMessage = nullptr) const;
    QVector<LongTermMemoryItem> removeMatchingKeyword(
        const QString &keyword,
        QString *errorMessage = nullptr) const;
    bool clear(QString *errorMessage = nullptr) const;

    QString filePath() const;
    int maxItems() const;

    static QString sanitizeContent(const QString &content);
    static bool containsSensitiveSecret(const QString &text);
    static QString formatForSystemPrompt(
        const QVector<LongTermMemoryItem> &items,
        int maxChars = DefaultPromptMaxChars);

private:
    QString m_filePath;
    int m_maxItems = DefaultMaxItems;
};
