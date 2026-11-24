#include "LongTermMemoryExtractor.h"

#include "../util/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <utility>

namespace
{
constexpr int MinUsefulUserMessageChars = 6;
constexpr int MaxSnippetCharsForExtraction = 1000;
constexpr int MaxContextMessageChars = 500;
constexpr int MaxBackgroundContextMessages = 4;
constexpr int MaxExplicitContextMessages = 8;
constexpr int MaxExistingMemoriesInPrompt = 30;
constexpr int MinExtractedContentChars = 4;
constexpr int MaxExtractedItemsPerTurn = 3;

enum class ExtractionParseStatus
{
    EmptyArray,
    Parsed,
    FilteredAll,
    ParseFailed,
};

QString parseStatusToString(ExtractionParseStatus status)
{
    switch (status)
    {
    case ExtractionParseStatus::EmptyArray:
        return QStringLiteral("empty_array");
    case ExtractionParseStatus::Parsed:
        return QStringLiteral("parsed");
    case ExtractionParseStatus::FilteredAll:
        return QStringLiteral("filtered_all");
    case ExtractionParseStatus::ParseFailed:
        return QStringLiteral("parse_failed");
    }

    return QStringLiteral("unknown");
}

QString compactAlphaNumericText(const QString &text)
{
    QString compact;
    compact.reserve(text.size());
    for (const QChar ch : text)
    {
        if (ch.isLetterOrNumber())
        {
            compact.append(ch.toLower());
        }
    }
    return compact;
}

bool isTrivialGreetingOrThanks(const QString &compact)
{
    static const QSet<QString> TrivialSet = {
        QStringLiteral("你好"),
        QStringLiteral("您好"),
        QStringLiteral("你好啊"),
        QStringLiteral("你好呀"),
        QStringLiteral("谢谢"),
        QStringLiteral("谢谢你"),
        QStringLiteral("多谢"),
        QStringLiteral("感谢"),
        QStringLiteral("再见"),
        QStringLiteral("拜拜"),
        QStringLiteral("早安"),
        QStringLiteral("晚安"),
        QStringLiteral("好的"),
        QStringLiteral("收到"),
        QStringLiteral("明白了"),
        QStringLiteral("知道了"),
        QStringLiteral("哈哈"),
        QStringLiteral("哈哈哈"),
        QStringLiteral("嗯嗯"),
        QStringLiteral("ok"),
        QStringLiteral("okay"),
        QStringLiteral("hello"),
        QStringLiteral("hi"),
        QStringLiteral("thanks"),
        QStringLiteral("thankyou"),
        QStringLiteral("bye"),
    };
    return TrivialSet.contains(compact);
}

bool isVaguePlaceholderMemory(const QString &content)
{
    static const QRegularExpression VaguePattern(QStringLiteral(
        "^(?:用户|我)?(?:目前)?(?:正在|一直在)?(?:开发|维护|学习|研究|讨论|说的|聊的|提到的)?"
        "(?:我们|咱们)?(?:刚刚|刚才|上面|前面)?(?:说的|聊的|讨论的|提到的)?"
        "(?:这个|那个|该|上述|这项|那项|这条|那条|这些|那些|它)"
        "(?:项目|框架|工程|仓库|工具|软件|系统|事情|内容|信息|问题|方案)?$"));
    return VaguePattern.match(content.trimmed()).hasMatch();
}

QString stripWebCitationFooter(QString text)
{
    const int citationHeaderIndex = text.indexOf(QStringLiteral("\n\n**来源：**"));
    if (citationHeaderIndex >= 0)
    {
        text = text.left(citationHeaderIndex);
    }

    text.remove(QStringLiteral("\n\n（模型未使用联网来源）"));
    text.remove(QStringLiteral("\n\n（模型使用了联网能力，但未返回可展示来源）"));
    return text.trimmed();
}

QString stripThinkingTags(QString text)
{
    static const QRegularExpression ClosedThinkPattern(
        QStringLiteral("<think(?:ing)?>[\\s\\S]*?</think(?:ing)?>"),
        QRegularExpression::CaseInsensitiveOption);
    text.remove(ClosedThinkPattern);

    const int unclosedThinkIndex = text.indexOf(QStringLiteral("<think"), 0, Qt::CaseInsensitive);
    if (unclosedThinkIndex >= 0)
    {
        const int fenceIndex = text.indexOf(QStringLiteral("```"), unclosedThinkIndex);
        const int jsonStartIndex = text.indexOf(QStringLiteral("[{"), unclosedThinkIndex);
        int cutEnd = -1;
        if (fenceIndex >= 0 && jsonStartIndex >= 0)
        {
            cutEnd = qMin(fenceIndex, jsonStartIndex);
        }
        else
        {
            cutEnd = qMax(fenceIndex, jsonStartIndex);
        }

        if (cutEnd > unclosedThinkIndex)
        {
            text.remove(unclosedThinkIndex, cutEnd - unclosedThinkIndex);
        }
    }

    return text.trimmed();
}

QString extractPrimaryJsonCandidate(const QString &rawText)
{
    const QString text = stripThinkingTags(rawText);
    if (text.isEmpty())
    {
        return {};
    }

    static const QRegularExpression ClosedFencePattern(
        QStringLiteral("```(?:json)?\\s*([\\s\\S]*?)```"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch closedMatch = ClosedFencePattern.match(text);
    if (closedMatch.hasMatch())
    {
        return closedMatch.captured(1).trimmed();
    }

    static const QRegularExpression OpenFencePattern(
        QStringLiteral("```(?:json)?\\s*([\\s\\S]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch openMatch = OpenFencePattern.match(text);
    if (openMatch.hasMatch())
    {
        return openMatch.captured(1).trimmed();
    }

    return text;
}

bool extractJsonObjectsFromDocument(const QJsonDocument &document, QVector<QJsonObject> *outObjects, bool *wasValidContainer)
{
    if (wasValidContainer != nullptr)
    {
        *wasValidContainer = false;
    }

    if (document.isArray())
    {
        if (wasValidContainer != nullptr)
        {
            *wasValidContainer = true;
        }
        for (const QJsonValue &value : document.array())
        {
            if (value.isObject())
            {
                outObjects->append(value.toObject());
            }
        }
        return true;
    }

    if (document.isObject())
    {
        const QJsonObject root = document.object();
        const QStringList arrayKeys = {
            QStringLiteral("items"),
            QStringLiteral("memories"),
            QStringLiteral("data"),
            QStringLiteral("list"),
            QStringLiteral("results"),
        };
        for (const QString &key : arrayKeys)
        {
            if (root.value(key).isArray())
            {
                if (wasValidContainer != nullptr)
                {
                    *wasValidContainer = true;
                }
                for (const QJsonValue &value : root.value(key).toArray())
                {
                    if (value.isObject())
                    {
                        outObjects->append(value.toObject());
                    }
                }
                return true;
            }
        }

        if (root.contains(QStringLiteral("content")))
        {
            if (wasValidContainer != nullptr)
            {
                *wasValidContainer = true;
            }
            outObjects->append(root);
            return true;
        }
    }

    return false;
}

QVector<ExtractedMemoryCandidate> parseExtractionResponseInternal(
    const QString &rawResponse,
    ExtractionParseStatus *outStatus)
{
    if (outStatus != nullptr)
    {
        *outStatus = ExtractionParseStatus::ParseFailed;
    }

    const QString candidateText = extractPrimaryJsonCandidate(rawResponse);
    if (candidateText.isEmpty())
    {
        return {};
    }

    QVector<QJsonObject> rawObjects;
    bool validContainer = false;

    auto tryParseJsonSlice = [&](const QString &slice) -> bool {
        if (slice.trimmed().isEmpty())
        {
            return false;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(slice.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
        {
            return false;
        }
        return extractJsonObjectsFromDocument(doc, &rawObjects, &validContainer);
    };

    if (!tryParseJsonSlice(candidateText))
    {
        // 优先定位 JSON 数组区间 [...]（向后查找包含 '{' 或空数组 '[]' 的位置，避免被说明文字中的 [分类] 干扰）
        int searchFrom = 0;
        bool parsedArraySlice = false;
        const int lastRightBracket = candidateText.lastIndexOf(QLatin1Char(']'));
        while (lastRightBracket > searchFrom)
        {
            const int leftBracket = candidateText.indexOf(QLatin1Char('['), searchFrom);
            if (leftBracket < 0 || leftBracket >= lastRightBracket)
            {
                break;
            }
            const QString slice = candidateText.mid(leftBracket, lastRightBracket - leftBracket + 1);
            if (tryParseJsonSlice(slice))
            {
                parsedArraySlice = true;
                break;
            }
            searchFrom = leftBracket + 1;
        }

        if (!parsedArraySlice)
        {
            // 退一步：扫描文本中所有完整闭合的 {"category":..., "content":...} 对象（兼容输出被 maxTokens 截断的情况）
            static const QRegularExpression ObjectPattern(
                QStringLiteral("\\{[^{}]*\"content\"\\s*:\\s*\"(?:[^\"\\\\]|\\\\.)*\"[^{}]*\\}"));
            QRegularExpressionMatchIterator it = ObjectPattern.globalMatch(candidateText);
            while (it.hasNext())
            {
                const QRegularExpressionMatch match = it.next();
                QJsonParseError objError;
                const QJsonDocument objDoc = QJsonDocument::fromJson(match.captured(0).toUtf8(), &objError);
                if (objError.error == QJsonParseError::NoError && objDoc.isObject())
                {
                    rawObjects.append(objDoc.object());
                    validContainer = true;
                }
            }
        }
    }

    if (!validContainer)
    {
        return {};
    }

    if (rawObjects.isEmpty())
    {
        if (outStatus != nullptr)
        {
            *outStatus = ExtractionParseStatus::EmptyArray;
        }
        return {};
    }

    QVector<ExtractedMemoryCandidate> results;
    QSet<QString> seenKeys;
    for (const QJsonObject &object : rawObjects)
    {
        const QString content = LongTermMemoryStore::sanitizeContent(
            object.value(QStringLiteral("content")).toString());
        if (content.size() < MinExtractedContentChars)
        {
            continue;
        }

        if (isVaguePlaceholderMemory(content))
        {
            continue;
        }

        if (LongTermMemoryStore::containsSensitiveSecret(content))
        {
            continue;
        }

        const QString dedupKey = compactAlphaNumericText(content);
        if (dedupKey.isEmpty() || seenKeys.contains(dedupKey))
        {
            continue;
        }
        seenKeys.insert(dedupKey);

        ExtractedMemoryCandidate candidate;
        candidate.category = longTermMemoryCategoryFromString(
            object.value(QStringLiteral("category")).toString());
        candidate.content = content;
        results.append(candidate);

        if (results.size() >= MaxExtractedItemsPerTurn)
        {
            break;
        }
    }

    if (outStatus != nullptr)
    {
        *outStatus = results.isEmpty()
            ? ExtractionParseStatus::FilteredAll
            : ExtractionParseStatus::Parsed;
    }
    return results;
}

void appendExistingMemoriesSection(QString &systemPrompt, const QVector<LongTermMemoryItem> &existingItems)
{
    if (existingItems.isEmpty())
    {
        return;
    }

    systemPrompt += QStringLiteral("\n\n已有长期记忆（若语义已覆盖或仅是悬空代词则不要重复输出）：");
    const int limit = qMin(static_cast<int>(existingItems.size()), MaxExistingMemoriesInPrompt);
    for (int index = 0; index < limit; ++index)
    {
        const LongTermMemoryItem &item = existingItems.at(index);
        if (isVaguePlaceholderMemory(item.content))
        {
            continue;
        }
        systemPrompt += QStringLiteral("\n- [%1] %2")
                            .arg(longTermMemoryCategoryToString(item.category),
                                 LongTermMemoryStore::sanitizeContent(item.content));
    }
}

QString formatRecentHistoryTranscript(const QVector<ChatMessage> &history, int maxMessages, int maxCharsPerMessage)
{
    QStringList lines;
    const int total = static_cast<int>(history.size());
    const int startIndex = qMax(0, total - qMax(1, maxMessages));
    for (int index = startIndex; index < total; ++index)
    {
        const ChatMessage &msg = history.at(index);
        const QString roleName = msg.role == QStringLiteral("assistant")
            ? QStringLiteral("助手")
            : QStringLiteral("用户");
        const QString cleaned = stripWebCitationFooter(msg.content).left(maxCharsPerMessage);
        if (!cleaned.isEmpty())
        {
            lines.append(QStringLiteral("%1：%2").arg(roleName, cleaned));
        }
    }
    return lines.join(QStringLiteral("\n\n"));
}
}

LongTermMemoryExtractor::LongTermMemoryExtractor()
    : m_alive(std::make_shared<bool>(true))
{
}

LongTermMemoryExtractor::~LongTermMemoryExtractor()
{
    cancel();
}

bool LongTermMemoryExtractor::isBusy() const
{
    return m_busy || (m_activeProvider && m_activeProvider->isBusy());
}

void LongTermMemoryExtractor::cancel()
{
    if (m_alive)
    {
        *m_alive = false;
    }
    m_alive = std::make_shared<bool>(true);
    m_busy = false;
    if (m_activeProvider)
    {
        m_activeProvider->cancel();
        m_activeProvider.reset();
    }
}

void LongTermMemoryExtractor::extractAsync(
    std::unique_ptr<LLMProvider> provider,
    const QString &userMessage,
    const QString &assistantMessage,
    const LongTermMemoryStore &store,
    std::function<void(int)> completedCallback,
    const QVector<ChatMessage> &recentContext)
{
    if (!provider || isBusy())
    {
        return;
    }

    if (!shouldConsiderExchange(userMessage, assistantMessage))
    {
        return;
    }

    const QVector<LongTermMemoryItem> existingItems = store.load();
    const ChatRequest request = buildExtractionRequest(userMessage, assistantMessage, existingItems, recentContext);
    const QString storePath = store.filePath();
    const int maxItems = store.maxItems();
    const std::shared_ptr<bool> alive = m_alive;

    m_activeProvider = std::move(provider);
    m_busy = true;
    m_activeProvider->sendChatRequest(
        request,
        [this, alive, storePath, maxItems, completedCallback = std::move(completedCallback)](const ChatResponse &response) mutable {
            if (!alive || !(*alive))
            {
                return;
            }

            m_busy = false;
            if (!response.success)
            {
                Logger::warning(QStringLiteral("Long-term memory background extraction skipped due to request failure: httpStatus=%1")
                    .arg(response.httpStatus));
                if (completedCallback)
                {
                    completedCallback(0);
                }
                return;
            }

            ExtractionParseStatus parseStatus = ExtractionParseStatus::ParseFailed;
            const QVector<ExtractedMemoryCandidate> candidates =
                parseExtractionResponseInternal(response.content, &parseStatus);
            int savedCount = 0;
            if (!candidates.isEmpty())
            {
                const LongTermMemoryStore targetStore(storePath, maxItems);
                for (const ExtractedMemoryCandidate &candidate : candidates)
                {
                    if (targetStore.upsert(candidate.content, candidate.category, LongTermMemorySource::Auto))
                    {
                        ++savedCount;
                    }
                }
            }

            const QString logLine = QStringLiteral(
                "Long-term memory extraction completed: mode=auto status=%1 responseChars=%2 candidates=%3 saved=%4")
                                        .arg(parseStatusToString(parseStatus))
                                        .arg(response.content.size())
                                        .arg(candidates.size())
                                        .arg(savedCount);
            if (parseStatus == ExtractionParseStatus::ParseFailed)
            {
                Logger::warning(logLine);
            }
            else
            {
                Logger::info(logLine);
            }

            if (completedCallback)
            {
                completedCallback(savedCount);
            }
        });
}

void LongTermMemoryExtractor::extractFromContextAsync(
    std::unique_ptr<LLMProvider> provider,
    const QString &userInstruction,
    const QVector<ChatMessage> &recentHistory,
    const LongTermMemoryStore &store,
    std::function<void(const ExplicitExtractionResult &)> completedCallback)
{
    if (!provider)
    {
        if (completedCallback)
        {
            completedCallback({false, {}, QStringLiteral("AI 服务未就绪，无法提炼长期记忆。")});
        }
        return;
    }

    if (recentHistory.isEmpty())
    {
        if (completedCallback)
        {
            completedCallback({false, {}, QStringLiteral("当前没有可参考的近期对话，请直接说明要记住的具体内容。")});
        }
        return;
    }

    if (isBusy())
    {
        cancel();
    }

    const QVector<LongTermMemoryItem> existingItems = store.load();
    const ChatRequest request = buildExplicitContextExtractionRequest(userInstruction, recentHistory, existingItems);
    const QString storePath = store.filePath();
    const int maxItems = store.maxItems();
    const std::shared_ptr<bool> alive = m_alive;

    m_activeProvider = std::move(provider);
    m_busy = true;
    m_activeProvider->sendChatRequest(
        request,
        [this, alive, storePath, maxItems, completedCallback = std::move(completedCallback)](const ChatResponse &response) mutable {
            if (!alive || !(*alive))
            {
                return;
            }

            m_busy = false;
            if (!response.success)
            {
                Logger::warning(QStringLiteral("Long-term memory explicit context extraction failed: httpStatus=%1")
                    .arg(response.httpStatus));
                if (completedCallback)
                {
                    completedCallback({false, {}, QStringLiteral("提炼长期记忆请求失败，请稍后重试或直接说“记住我…”。")});
                }
                return;
            }

            ExtractionParseStatus parseStatus = ExtractionParseStatus::ParseFailed;
            const QVector<ExtractedMemoryCandidate> candidates =
                parseExtractionResponseInternal(response.content, &parseStatus);

            QVector<LongTermMemoryItem> savedItems;
            QString lastStoreError;
            if (!candidates.isEmpty())
            {
                const LongTermMemoryStore targetStore(storePath, maxItems);
                // 若此前误存入过“这个项目”等纯悬空代词条目，顺手清理掉
                for (const LongTermMemoryItem &oldItem : targetStore.load())
                {
                    if (isVaguePlaceholderMemory(oldItem.content))
                    {
                        targetStore.removeById(oldItem.id);
                    }
                }

                for (const ExtractedMemoryCandidate &candidate : candidates)
                {
                    LongTermMemoryItem savedItem;
                    QString error;
                    if (targetStore.upsert(
                            candidate.content,
                            candidate.category,
                            LongTermMemorySource::Explicit,
                            &savedItem,
                            nullptr,
                            &error))
                    {
                        savedItems.append(savedItem);
                    }
                    else if (!error.isEmpty())
                    {
                        lastStoreError = error;
                    }
                }
            }

            const QString logLine = QStringLiteral(
                "Long-term memory extraction completed: mode=explicit status=%1 responseChars=%2 candidates=%3 saved=%4")
                                        .arg(parseStatusToString(parseStatus))
                                        .arg(response.content.size())
                                        .arg(candidates.size())
                                        .arg(savedItems.size());
            if (parseStatus == ExtractionParseStatus::ParseFailed)
            {
                Logger::warning(logLine);
            }
            else
            {
                Logger::info(logLine);
            }

            if (completedCallback)
            {
                if (!savedItems.isEmpty())
                {
                    completedCallback({true, savedItems, {}});
                    return;
                }

                const QString message = !lastStoreError.isEmpty()
                    ? lastStoreError
                    : QStringLiteral("未能从最近对话中提炼出明确的长期记忆，你可以直接对我说“记住我正在开发 QtDesktopPet 桌面宠物项目”。");
                completedCallback({false, {}, message});
            }
        });
}

bool LongTermMemoryExtractor::shouldConsiderExchange(
    const QString &userMessage,
    const QString &assistantMessage)
{
    const QString cleanedUser = userMessage.trimmed();
    const QString cleanedAssistant = stripWebCitationFooter(assistantMessage);
    if (cleanedUser.size() < MinUsefulUserMessageChars || cleanedAssistant.isEmpty())
    {
        return false;
    }

    const QString compact = compactAlphaNumericText(cleanedUser);
    if (compact.size() < 4 || isTrivialGreetingOrThanks(compact))
    {
        return false;
    }

    if (LongTermMemoryStore::containsSensitiveSecret(cleanedUser)
        || LongTermMemoryStore::containsSensitiveSecret(cleanedAssistant))
    {
        return false;
    }

    return true;
}

ChatRequest LongTermMemoryExtractor::buildExtractionRequest(
    const QString &userMessage,
    const QString &assistantMessage,
    const QVector<LongTermMemoryItem> &existingItems,
    const QVector<ChatMessage> &recentContext)
{
    QString systemPrompt = QStringLiteral(
        "你是用户的长期记忆整理助手。请从下面的对话中提取“对未来跨会话长期有价值、且相对稳定”的用户信息：\n"
        "- 可提取类别：\n"
        "  - profile：身份、职业、常住城市、名字/称呼等个人画像；\n"
        "  - preference：沟通偏好、作息习惯、兴趣爱好、工具偏好等；\n"
        "  - project：用户开发、维护或学习的固定项目、框架名称、技术栈、工程背景；\n"
        "  - fact：其他值得长期记住的重要个人事实。\n"
        "- 提取准则：\n"
        "  - 每条记忆必须自包含、指代明确，写出具体项目名或具体事项，严禁只写“这个项目”“该框架”等悬空代词；\n"
        "  - 如果用户在对话中明确说明某项目是自己维护/开发的，或要求总结记住前文项目要点，应提取为用户自身的 project 记忆；\n"
        "  - 严禁提取与用户无关的通用百科问答、寒暄、临时天气或提醒；\n"
        "  - 严禁提取任何 API Key、Token、密码、口令或账号凭证。\n"
        "- 请直接输出严格 JSON 数组，不要输出任何解释或代码块以外的文字。\n"
        "- 数组元素格式：[{\"category\":\"profile|preference|project|fact\",\"content\":\"简短第三人称陈述（不超过80字）\"}]\n"
        "- 如果没有值得保存的长期信息，请严格输出 []。");

    appendExistingMemoriesSection(systemPrompt, existingItems);

    const QString userSnippet = userMessage.trimmed().left(MaxSnippetCharsForExtraction);
    const QString assistantSnippet = stripWebCitationFooter(assistantMessage).left(MaxSnippetCharsForExtraction);

    QString userPrompt;
    const QString contextTranscript = formatRecentHistoryTranscript(
        recentContext,
        MaxBackgroundContextMessages,
        MaxContextMessageChars);
    if (!contextTranscript.isEmpty())
    {
        userPrompt += QStringLiteral("【近期上文参考】\n%1\n\n").arg(contextTranscript);
    }
    userPrompt += QStringLiteral("【本轮最新对话】\n用户：%1\n\n助手：%2").arg(userSnippet, assistantSnippet);

    ChatRequest request;
    request.messages.append(ChatMessage{QStringLiteral("system"), systemPrompt});
    request.messages.append(ChatMessage{QStringLiteral("user"), userPrompt});
    return request;
}

ChatRequest LongTermMemoryExtractor::buildExplicitContextExtractionRequest(
    const QString &userInstruction,
    const QVector<ChatMessage> &recentHistory,
    const QVector<LongTermMemoryItem> &existingItems)
{
    QString systemPrompt = QStringLiteral(
        "你是用户的长期记忆提炼助手。用户刚刚发出了显式的“结合上文记住/提炼到长期记忆”指令。\n"
        "请根据下方提供的【近期对话上下文】和【用户记忆指令】，把用户要求记住的目标（例如用户维护或开发的项目名称、技术栈、核心定位与进展，或用户刚才提到的个人偏好、身份与重要事实）提炼为 1～3 条清晰、自包含的长期记忆：\n"
        "- 类别取值：profile（个人画像）、preference（偏好习惯）、project（项目背景）、fact（重要事实）；\n"
        "- 每条 content 必须写出具体的项目名/框架名/事实全称（例如“用户正在开发基于 C++17 与 Qt6 的桌面宠物项目 QtDesktopPet”），严禁只写“这个项目”“该框架”等无具体含义的代词；\n"
        "- 严禁包含任何 API Key、Token、密码等敏感凭据；\n"
        "- 请只输出严格 JSON 数组，格式：[{\"category\":\"profile|preference|project|fact\",\"content\":\"简短第三人称陈述（不超过80字）\"}]。");

    appendExistingMemoriesSection(systemPrompt, existingItems);

    const QString transcript = formatRecentHistoryTranscript(
        recentHistory,
        MaxExplicitContextMessages,
        MaxSnippetCharsForExtraction);
    const QString userPrompt = QStringLiteral("【近期对话上下文】\n%1\n\n【用户记忆指令】\n%2")
                                   .arg(transcript, userInstruction.trimmed());

    ChatRequest request;
    request.messages.append(ChatMessage{QStringLiteral("system"), systemPrompt});
    request.messages.append(ChatMessage{QStringLiteral("user"), userPrompt});
    return request;
}

QVector<ExtractedMemoryCandidate> LongTermMemoryExtractor::parseExtractionResponse(const QString &rawResponse)
{
    return parseExtractionResponseInternal(rawResponse, nullptr);
}
