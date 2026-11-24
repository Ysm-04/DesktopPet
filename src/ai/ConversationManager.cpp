#include "ConversationManager.h"

#include "ConversationRecallEngine.h"
#include "LongTermMemoryStore.h"
#include "../character/CharacterPersonaStore.h"

#include <QDateTime>
#include <QtGlobal>

#include <utility>

namespace
{
bool sameArchiveMessage(const ChatMessage &left, const ChatMessage &right)
{
    if (left.role != right.role || left.content != right.content)
    {
        return false;
    }

    if (left.timestamp.isValid() && right.timestamp.isValid())
    {
        return left.timestamp == right.timestamp;
    }

    return true;
}

int overlappingMessageCount(const QVector<ChatMessage> &archive, const QVector<ChatMessage> &memory)
{
    const int maxOverlap = qMin(archive.size(), memory.size());
    for (int overlap = maxOverlap; overlap > 0; --overlap)
    {
        bool matched = true;
        const int archiveStart = archive.size() - overlap;
        for (int index = 0; index < overlap; ++index)
        {
            if (!sameArchiveMessage(archive.at(archiveStart + index), memory.at(index)))
            {
                matched = false;
                break;
            }
        }
        if (matched)
        {
            return overlap;
        }
    }
    return 0;
}

QVector<ChatMessage> mergedArchiveAndMemory(QVector<ChatMessage> archive, const QVector<ChatMessage> &memory)
{
    if (archive.isEmpty())
    {
        return memory;
    }

    if (!memory.isEmpty())
    {
        const int overlap = overlappingMessageCount(archive, memory);
        for (int index = overlap; index < memory.size(); ++index)
        {
            const ChatMessage &message = memory.at(index);
            if (!message.role.trimmed().isEmpty() && !message.content.trimmed().isEmpty())
            {
                archive.append(message);
            }
        }
    }

    if (archive.size() % 2 != 0)
    {
        archive.erase(archive.begin());
    }
    return archive;
}
}

ConversationManager::ConversationManager() = default;

QString ConversationManager::defaultPersonaPrompt()
{
    return QStringLiteral(
        "你是一个 Windows 桌面宠物助手，常驻在用户桌面上。语气自然、友好、亲切，像贴心陪伴在桌面上的伙伴。");
}

QString ConversationManager::fixedCapabilityPrompt()
{
    return QStringLiteral(
        "无论角色设定如何，以下桌宠回复格式、能力说明与安全边界均不可违反：\n"
        "- 回复要简短、自然，适合显示在桌宠气泡里；除非用户明确要求详细解释，否则不要长篇输出。气泡展示空间较小，尽量使用简短段落或精简列表，避免输出大标题或宽表格。\n"
        "\n"
        "你可以帮助用户：\n"
        "1. 普通聊天、解释问题、整理信息。\n"
        "2. 创建和管理提醒：\n"
        "   - 例：“10分钟后提醒我喝水”“明天9点提醒我开会”“每天9点提醒我打卡”“每周一上午10点提醒我周会”“提醒列表”“取消喝水提醒”。\n"
        "   - 支持一次性提醒和每天/每周/每月重复提醒。\n"
        "3. 查询天气：\n"
        "   - 例：“西安天气怎么样”“明天北京天气”“未来三天上海天气”。\n"
        "   - 建议用户使用市级城市名；区县、乡镇、街道不保证精确。\n"
        "4. 联网模式：\n"
        "   - 用户需要打开输入框里的“联网”开关。\n"
        "   - 支持 OpenAI 官方、DeepSeek 官方、Gemini 原生协议，以及在设置页手动指定联网协议并验证可用的中转站或自定义配置。\n"
        "   - 如果当前请求已经提供了服务端搜索/联网工具，请直接按需搜索并基于搜索结果简短作答；若未开启联网或当前配置不可用，再提醒用户检查联网设置，不要假装已经搜索。\n"
        "5. 长期记忆：\n"
        "   - 用户可以直接说“记住我…”、“把我们刚刚讨论的…提炼到长期记忆中”、“查看长期记忆”或“忘掉关于…的记忆”，应用会自动拦截或结合上下文提炼保存到长期记忆本，也可在 设置 → 长期记忆 中管理。\n"
        "\n"
        "回答规则：\n"
        "- 如果用户只是闲聊，直接自然回复。\n"
        "- 如果用户问“你能做什么”，用简短列表说明上述功能和示例。\n"
        "- 如果用户要创建提醒或查询天气，可以直接提示用户按示例输入。\n"
        "- 当前应用不能读取、搜索、创建、修改或删除电脑上的其他本地文件，不能启动本地应用，也不能执行命令或脚本；遇到这类请求要明确说明能力边界。\n"
        "- 如果当次请求中附带了本应用保存的 `[用户长期记忆]`，请在相关时自然运用其中的用户背景与偏好，不要生硬复述。\n"
        "- 如果当次请求中附带了本应用检索出的 `[本地历史对话参考]`，可以自然结合其中的早期记录回答用户回顾。\n"
        "- 如果用户询问最新信息但没有开启联网，提醒用户打开“联网”开关；如果当前 AI 配置不支持联网，说明无法使用联网模式。\n"
        "- 不要声称已经访问电脑本地文件、启动应用或执行系统操作，也不要编造任何未发生的操作结果。");
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

QVector<ChatMessage> ConversationManager::fullHistory() const
{
    if (!m_archiveHistoryLoader)
    {
        return m_history;
    }

    return mergedArchiveAndMemory(m_archiveHistoryLoader(), m_history);
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

void ConversationManager::setRequestContextUseMax(bool useMax)
{
    m_requestContextUseMax = useMax;
}

void ConversationManager::setRequestContextMaxChars(int maxChars)
{
    m_requestContextMaxChars = qBound(4000, maxChars, 120000);
}

void ConversationManager::setMemoryHistoryMessageLimit(int maxMessages)
{
    m_maxStoredHistoryMessages = qMax(0, maxMessages);
    pruneHistory();
}

void ConversationManager::setHistoryRecallEnabled(bool enabled)
{
    m_historyRecallEnabled = enabled;
}

void ConversationManager::setArchiveHistoryLoader(ArchiveHistoryLoader loader)
{
    m_archiveHistoryLoader = std::move(loader);
}

void ConversationManager::setLongTermMemoryEnabled(bool enabled)
{
    m_longTermMemoryEnabled = enabled;
}

void ConversationManager::setLongTermMemoryLoader(LongTermMemoryLoader loader)
{
    m_longTermMemoryLoader = std::move(loader);
}

void ConversationManager::setConversationMetadata(const QString &provider, const QString &model)
{
    m_currentProvider = provider.trimmed();
    m_currentModel = model.trimmed();
}

void ConversationManager::setCharacterPersona(const QString &persona)
{
    m_characterPersona = CharacterPersonaStore::sanitizePersona(persona);
}

QString ConversationManager::characterPersona() const
{
    return m_characterPersona;
}

QString ConversationManager::effectiveSystemPrompt() const
{
    const QString personaLayer = m_characterPersona.isEmpty()
        ? defaultPersonaPrompt()
        : (QStringLiteral("[角色人设]\n") + m_characterPersona);
    const QString capabilityLayer = fixedCapabilityPrompt().trimmed();
    if (capabilityLayer.isEmpty())
    {
        return personaLayer;
    }

    return personaLayer + QStringLiteral("\n\n") + capabilityLayer;
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

ChatRequest ConversationManager::buildRequestForUserMessage(const QString &message) const
{
    const QString content = message.trimmed();
    if (content.isEmpty())
    {
        return {};
    }

    const ChatMessage userMessage{QStringLiteral("user"), content, QDateTime::currentDateTime(), m_currentProvider, m_currentModel};
    return buildRequest(userMessage);
}

void ConversationManager::appendExternalExchange(const QString &userMessage, const QString &assistantMessage)
{
    const QString userContent = userMessage.trimmed();
    const QString assistantContent = assistantMessage.trimmed();
    if (userContent.isEmpty() || assistantContent.isEmpty())
    {
        return;
    }

    const ChatMessage user{QStringLiteral("user"), userContent, QDateTime::currentDateTime(), m_currentProvider, m_currentModel};
    const ChatMessage assistant{QStringLiteral("assistant"), assistantContent, QDateTime::currentDateTime(), m_currentProvider, m_currentModel};
    appendExchange(user, assistant);
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

    const QDateTime timestamp = QDateTime::currentDateTime();
    const ChatMessage userMessage{QStringLiteral("user"), content, timestamp, m_currentProvider, m_currentModel};
    m_provider->sendStreamingChatRequest(
        buildRequest(userMessage),
        std::move(streamCallback),
        [this, userMessage, callback = std::move(callback)](const ChatResponse &response) mutable {
            if (response.success)
            {
                ChatMessage assistantMessage{QStringLiteral("assistant"), response.content, QDateTime::currentDateTime(), userMessage.provider, userMessage.model};
                appendExchange(userMessage, assistantMessage);
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
    const QVector<ChatMessage> archivePool = fullHistory();
    const int poolSize = static_cast<int>(archivePool.size());
    const int historyCharBudget = qMax(400, m_requestContextMaxChars);

    int earliestAllowedIndex = poolSize;
    if (m_requestContextUseMax)
    {
        earliestAllowedIndex = 0;
    }
    else if (m_maxRequestContextMessages > 0)
    {
        const int desiredCount = m_maxRequestContextMessages == 1
            ? 2
            : (m_maxRequestContextMessages - (m_maxRequestContextMessages % 2));
        earliestAllowedIndex = qMax(0, poolSize - desiredCount);
    }

    int startIndex = poolSize;
    int accumulatedChars = 0;
    for (int index = poolSize - 2; index >= earliestAllowedIndex; index -= 2)
    {
        const int pairChars = archivePool.at(index).content.size()
            + archivePool.at(index + 1).content.size();
        if (startIndex < poolSize && accumulatedChars + pairChars > historyCharBudget)
        {
            break;
        }
        accumulatedChars += pairChars;
        startIndex = index;
    }

    const int includedRecentCount = poolSize - startIndex;
    QString systemText = effectiveSystemPrompt().trimmed();
    if (m_longTermMemoryEnabled && m_longTermMemoryLoader)
    {
        const QString memoryBlock = LongTermMemoryStore::formatForSystemPrompt(m_longTermMemoryLoader());
        if (!memoryBlock.isEmpty())
        {
            if (!systemText.isEmpty())
            {
                systemText += QStringLiteral("\n\n");
            }
            systemText += memoryBlock;
        }
    }
    if (m_historyRecallEnabled && poolSize > includedRecentCount)
    {
        const QVector<ConversationRecallEngine::RecalledExchange> recalled =
            ConversationRecallEngine::recallRelevantExchanges(
                userMessage.content,
                archivePool,
                includedRecentCount);
        const QString recalledBlock = ConversationRecallEngine::formatRecalledContextBlock(recalled);
        if (!recalledBlock.isEmpty())
        {
            if (!systemText.isEmpty())
            {
                systemText += QStringLiteral("\n\n");
            }
            systemText += recalledBlock;
        }
    }

    ChatRequest request;
    if (!systemText.isEmpty())
    {
        request.messages.append(ChatMessage{QStringLiteral("system"), systemText});
    }

    for (int index = startIndex; index < poolSize; ++index)
    {
        request.messages.append(archivePool.at(index));
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
