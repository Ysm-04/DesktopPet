#include "ConversationRecallEngine.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int MaxQueryTokens = 24;

bool isAsciiWordChar(QChar ch)
{
    const ushort code = ch.unicode();
    return (code >= 'a' && code <= 'z')
        || (code >= 'A' && code <= 'Z')
        || (code >= '0' && code <= '9')
        || code == '_'
        || code == '+'
        || code == '#';
}

bool isCjkChar(QChar ch)
{
    const ushort code = ch.unicode();
    return code >= 0x4E00 && code <= 0x9FFF;
}

const QSet<QString> &stopWordSet()
{
    static const QSet<QString> stopWords = {
        // 中文高频虚词/提问碎片
        QStringLiteral("我们"),
        QStringLiteral("你们"),
        QStringLiteral("他们"),
        QStringLiteral("自己"),
        QStringLiteral("什么"),
        QStringLiteral("怎么"),
        QStringLiteral("怎样"),
        QStringLiteral("如何"),
        QStringLiteral("为什么"),
        QStringLiteral("可以"),
        QStringLiteral("能否"),
        QStringLiteral("是否"),
        QStringLiteral("是不是"),
        QStringLiteral("有没有"),
        QStringLiteral("一下"),
        QStringLiteral("这个"),
        QStringLiteral("那个"),
        QStringLiteral("这里"),
        QStringLiteral("那里"),
        QStringLiteral("之前"),
        QStringLiteral("上次"),
        QStringLiteral("以前"),
        QStringLiteral("记得"),
        QStringLiteral("聊过"),
        QStringLiteral("提过"),
        QStringLiteral("说过"),
        QStringLiteral("回忆"),
        QStringLiteral("历史"),
        QStringLiteral("前面"),
        QStringLiteral("今天"),
        QStringLiteral("昨天"),
        QStringLiteral("明天"),
        QStringLiteral("最近"),
        QStringLiteral("现在"),
        QStringLiteral("请问"),
        QStringLiteral("帮我"),
        QStringLiteral("告诉"),
        QStringLiteral("知道"),
        QStringLiteral("还是"),
        QStringLiteral("就是"),
        QStringLiteral("不是"),
        QStringLiteral("没有"),
        QStringLiteral("一个"),
        QStringLiteral("一些"),
        QStringLiteral("东西"),
        QStringLiteral("事情"),
        QStringLiteral("问题"),
        QStringLiteral("叫做"),
        QStringLiteral("那个叫"),
        QStringLiteral("桌宠"),
        // 英文高频词
        QStringLiteral("the"),
        QStringLiteral("and"),
        QStringLiteral("for"),
        QStringLiteral("that"),
        QStringLiteral("with"),
        QStringLiteral("this"),
        QStringLiteral("what"),
        QStringLiteral("when"),
        QStringLiteral("where"),
        QStringLiteral("which"),
        QStringLiteral("have"),
        QStringLiteral("from"),
        QStringLiteral("your"),
        QStringLiteral("about"),
        QStringLiteral("remember"),
    };
    return stopWords;
}

bool hasExplicitRecallCue(const QString &query)
{
    static const QStringList cues = {
        QStringLiteral("之前"),
        QStringLiteral("上次"),
        QStringLiteral("以前"),
        QStringLiteral("记得"),
        QStringLiteral("聊过"),
        QStringLiteral("提过"),
        QStringLiteral("说过"),
        QStringLiteral("回忆"),
        QStringLiteral("历史"),
        QStringLiteral("前面"),
        QStringLiteral("早些"),
        QStringLiteral("曾经"),
        QStringLiteral("当时"),
    };
    for (const QString &cue : cues)
    {
        if (query.contains(cue, Qt::CaseInsensitive))
        {
            return true;
        }
    }
    return false;
}

QStringList extractQueryTokens(const QString &query)
{
    QString cleaned = query.trimmed();

    static const QStringList stopPhrases = {
        QStringLiteral("请问一下"),
        QStringLiteral("还记得"),
        QStringLiteral("能不能"),
        QStringLiteral("是不是"),
        QStringLiteral("有没有"),
        QStringLiteral("为什么"),
        QStringLiteral("我们"),
        QStringLiteral("你们"),
        QStringLiteral("他们"),
        QStringLiteral("自己"),
        QStringLiteral("什么"),
        QStringLiteral("怎么"),
        QStringLiteral("怎样"),
        QStringLiteral("如何"),
        QStringLiteral("可以"),
        QStringLiteral("能否"),
        QStringLiteral("是否"),
        QStringLiteral("一下"),
        QStringLiteral("这个"),
        QStringLiteral("那个"),
        QStringLiteral("这里"),
        QStringLiteral("那里"),
        QStringLiteral("之前"),
        QStringLiteral("上次"),
        QStringLiteral("以前"),
        QStringLiteral("记得"),
        QStringLiteral("聊过"),
        QStringLiteral("提过"),
        QStringLiteral("说过"),
        QStringLiteral("回忆"),
        QStringLiteral("历史"),
        QStringLiteral("前面"),
        QStringLiteral("早些"),
        QStringLiteral("曾经"),
        QStringLiteral("当时"),
        QStringLiteral("今天"),
        QStringLiteral("昨天"),
        QStringLiteral("明天"),
        QStringLiteral("最近"),
        QStringLiteral("现在"),
        QStringLiteral("请问"),
        QStringLiteral("帮我"),
        QStringLiteral("告诉"),
        QStringLiteral("知道"),
        QStringLiteral("还是"),
        QStringLiteral("就是"),
        QStringLiteral("不是"),
        QStringLiteral("没有"),
        QStringLiteral("一个"),
        QStringLiteral("一些"),
        QStringLiteral("东西"),
        QStringLiteral("事情"),
        QStringLiteral("问题"),
        QStringLiteral("叫做"),
        QStringLiteral("桌宠"),
        QStringLiteral("到底"),
        QStringLiteral("究竟"),
    };
    for (const QString &phrase : stopPhrases)
    {
        cleaned.replace(phrase, QStringLiteral(" "));
    }
    cleaned.replace(
        QRegularExpression(QStringLiteral("[的了吗嘛么呢啊呀吧哦噢哈呐我你他她它还]")),
        QStringLiteral(" "));

    QStringList tokens;
    QSet<QString> seen;
    const QSet<QString> &stopWords = stopWordSet();

    auto appendToken = [&](const QString &rawToken) {
        const QString token = rawToken.trimmed().toLower();
        if (token.size() < 2 || stopWords.contains(token) || seen.contains(token))
        {
            return;
        }
        seen.insert(token);
        tokens.append(token);
    };

    int index = 0;
    while (index < cleaned.size() && tokens.size() < MaxQueryTokens)
    {
        const QChar ch = cleaned.at(index);
        if (isAsciiWordChar(ch))
        {
            const int start = index;
            while (index < cleaned.size() && isAsciiWordChar(cleaned.at(index)))
            {
                ++index;
            }
            appendToken(cleaned.mid(start, index - start));
            continue;
        }

        if (isCjkChar(ch))
        {
            const int start = index;
            while (index < cleaned.size() && isCjkChar(cleaned.at(index)))
            {
                ++index;
            }
            const QString segment = cleaned.mid(start, index - start);
            for (int pos = 0; pos + 1 < segment.size() && tokens.size() < MaxQueryTokens; ++pos)
            {
                appendToken(segment.mid(pos, 2));
            }
            continue;
        }

        ++index;
    }

    return tokens;
}

struct IndexedExchange
{
    int originalIndex = 0;
    ConversationRecallEngine::RecalledExchange exchange;
    QString userLower;
    QString assistantLower;
};

QVector<IndexedExchange> buildEarlyExchanges(
    const QVector<ChatMessage> &archiveMessages,
    int recentWindowMessageCount)
{
    const int safeRecent = qMax(0, recentWindowMessageCount);
    const int earlyEnd = qMax(0, archiveMessages.size() - safeRecent);
    QVector<IndexedExchange> exchanges;

    int index = 0;
    while (index + 1 < earlyEnd)
    {
        const ChatMessage &first = archiveMessages.at(index);
        if (first.role != QStringLiteral("user"))
        {
            ++index;
            continue;
        }

        const ChatMessage &second = archiveMessages.at(index + 1);
        if (second.role != QStringLiteral("assistant"))
        {
            ++index;
            continue;
        }

        if (!first.content.trimmed().isEmpty() && !second.content.trimmed().isEmpty())
        {
            IndexedExchange item;
            item.originalIndex = exchanges.size();
            item.exchange.userMessage = first;
            item.exchange.assistantMessage = second;
            item.userLower = first.content.toLower();
            item.assistantLower = second.content.toLower();
            exchanges.append(item);
        }
        index += 2;
    }

    return exchanges;
}

QString clippedExcerpt(QString text, int maxChars)
{
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text = text.trimmed();
    if (maxChars <= 0 || text.size() <= maxChars)
    {
        return text;
    }

    const int cut = qMax(16, maxChars - 3);
    return text.left(cut).trimmed() + QStringLiteral("...");
}
}

namespace ConversationRecallEngine
{
QVector<RecalledExchange> recallRelevantExchanges(
    const QString &query,
    const QVector<ChatMessage> &archiveMessages,
    int recentWindowMessageCount,
    const RecallOptions &options)
{
    const int maxExchanges = qBound(1, options.maxExchanges, 12);
    const QVector<IndexedExchange> earlyExchanges = buildEarlyExchanges(archiveMessages, recentWindowMessageCount);
    if (earlyExchanges.isEmpty())
    {
        return {};
    }

    const bool explicitCue = hasExplicitRecallCue(query);
    const QStringList tokens = extractQueryTokens(query);
    if (tokens.isEmpty())
    {
        if (!explicitCue)
        {
            return {};
        }

        const int fallbackCount = qMin(2, qMin(maxExchanges, static_cast<int>(earlyExchanges.size())));
        QVector<RecalledExchange> fallbackResults;
        fallbackResults.reserve(fallbackCount);
        for (int index = earlyExchanges.size() - fallbackCount; index < earlyExchanges.size(); ++index)
        {
            RecalledExchange entry = earlyExchanges.at(index).exchange;
            entry.score = 1.0;
            fallbackResults.append(entry);
        }
        return fallbackResults;
    }

    const int totalExchanges = static_cast<int>(earlyExchanges.size());
    QVector<int> documentFrequencies(tokens.size(), 0);
    for (int tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
    {
        const QString &token = tokens.at(tokenIndex);
        int df = 0;
        for (const IndexedExchange &item : earlyExchanges)
        {
            if (item.userLower.contains(token) || item.assistantLower.contains(token))
            {
                ++df;
            }
        }
        documentFrequencies[tokenIndex] = df;
    }

    struct ScoredEntry
    {
        int originalIndex = 0;
        RecalledExchange exchange;
    };
    QVector<ScoredEntry> scored;

    for (const IndexedExchange &item : earlyExchanges)
    {
        int matchedTokens = 0;
        int rareSingleMatches = 0;
        double weightedHits = 0.0;

        for (int tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex)
        {
            const int df = documentFrequencies.at(tokenIndex);
            if (df == 0)
            {
                continue;
            }

            const QString &token = tokens.at(tokenIndex);
            const bool inUser = item.userLower.contains(token);
            const bool inAssistant = item.assistantLower.contains(token);
            if (!inUser && !inAssistant)
            {
                continue;
            }

            ++matchedTokens;
            if (df <= qMax(1, totalExchanges / 3))
            {
                ++rareSingleMatches;
            }

            const double idf = std::log((static_cast<double>(totalExchanges) + 1.0)
                / (static_cast<double>(df) + 0.5)) + 1.0;
            const double roleWeight = (inUser ? 1.2 : 0.0) + (inAssistant ? 0.9 : 0.0);
            weightedHits += idf * roleWeight;
        }

        if (matchedTokens == 0)
        {
            continue;
        }

        if (!explicitCue)
        {
            if (tokens.size() >= 3 && matchedTokens < 2)
            {
                continue;
            }
            if (tokens.size() <= 2 && matchedTokens == 1 && rareSingleMatches == 0)
            {
                continue;
            }
        }

        const double coverage = static_cast<double>(matchedTokens) / static_cast<double>(tokens.size());
        const double recencyBoost = 1.0 + 0.15 * (static_cast<double>(item.originalIndex + 1) / static_cast<double>(totalExchanges));
        const double totalScore = weightedHits * (0.7 + 0.3 * coverage) * recencyBoost;

        RecalledExchange candidate = item.exchange;
        candidate.score = totalScore;
        scored.append({item.originalIndex, candidate});
    }

    if (scored.isEmpty())
    {
        return {};
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredEntry &left, const ScoredEntry &right) {
        if (!qFuzzyCompare(left.exchange.score, right.exchange.score))
        {
            return left.exchange.score > right.exchange.score;
        }
        return left.originalIndex > right.originalIndex;
    });

    QVector<ScoredEntry> topEntries = scored.mid(0, qMin(maxExchanges, static_cast<int>(scored.size())));
    std::sort(topEntries.begin(), topEntries.end(), [](const ScoredEntry &left, const ScoredEntry &right) {
        return left.originalIndex < right.originalIndex;
    });

    QVector<RecalledExchange> results;
    results.reserve(topEntries.size());
    for (const ScoredEntry &entry : topEntries)
    {
        results.append(entry.exchange);
    }
    return results;
}

QString formatRecalledContextBlock(
    const QVector<RecalledExchange> &exchanges,
    const RecallOptions &options)
{
    if (exchanges.isEmpty())
    {
        return {};
    }

    const int maxTotalChars = qMax(80, options.maxTotalChars);
    const int maxExcerptChars = qBound(20, options.maxExcerptChars, 1000);
    const QString header = QStringLiteral("[本地历史对话参考（按需检索，仅作背景参考）]");
    QString result = header;
    int lineNumber = 0;

    for (const RecalledExchange &exchange : exchanges)
    {
        const QDateTime &ts = exchange.userMessage.timestamp.isValid()
            ? exchange.userMessage.timestamp
            : exchange.assistantMessage.timestamp;
        const QString dateLabel = ts.isValid()
            ? ts.toString(QStringLiteral("yyyy-MM-dd"))
            : QStringLiteral("早期记录");

        const QString userExcerpt = clippedExcerpt(exchange.userMessage.content, maxExcerptChars);
        const QString assistantExcerpt = clippedExcerpt(exchange.assistantMessage.content, maxExcerptChars);
        if (userExcerpt.isEmpty() && assistantExcerpt.isEmpty())
        {
            continue;
        }

        const QString line = QStringLiteral("\n%1. [%2] 用户：%3 | 助手：%4")
            .arg(++lineNumber)
            .arg(dateLabel, userExcerpt, assistantExcerpt);

        if (result.size() + line.size() > maxTotalChars)
        {
            if (lineNumber == 1)
            {
                const int available = maxTotalChars - result.size();
                if (available > 32)
                {
                    result += line.left(available - 3) + QStringLiteral("...");
                }
            }
            break;
        }

        result += line;
    }

    return result.size() > header.size() ? result : QString();
}
}
