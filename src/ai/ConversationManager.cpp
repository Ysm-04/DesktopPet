#include "ConversationManager.h"

#include <QDateTime>
#include <QtGlobal>
#include <utility>

ConversationManager::ConversationManager()
    : m_systemPrompt(QStringLiteral(
        "你是一个 Windows 桌面宠物助手，常驻在用户桌面上。回复要简短、自然、友好，适合显示在桌宠气泡里；除非用户明确要求详细解释，否则不要长篇输出。\n"
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
        "   - 只有当前 AI 配置支持原生联网时才可用，例如 OpenAI 官方联网能力或 Gemini Google Search grounding。\n"
        "   - DeepSeek 官方 API、第三方 OpenAI-Compatible API 通常不具备可确认的原生联网能力；遇到这类配置时，应明确提醒用户当前配置无法联网，不要假装已经搜索。\n"
        "\n"
        "回答规则：\n"
        "- 如果用户只是闲聊，直接自然回复。\n"
        "- 如果用户问“你能做什么”，用简短列表说明上述功能和示例。\n"
        "- 如果用户要创建提醒或查询天气，可以直接提示用户按示例输入。\n"
        "- 当前应用不能读取、搜索、创建、修改或删除本地文件，不能启动本地应用，也不能执行命令或脚本；遇到这类请求要明确说明能力边界。\n"
        "- 如果用户询问最新信息但没有开启联网，提醒用户打开“联网”开关；如果当前 AI 配置不支持联网，说明无法使用联网模式。\n"
        "- 不要声称已经访问本地文件、启动应用或执行系统操作，也不要编造任何未发生的操作结果。"))
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

void ConversationManager::setConversationMetadata(const QString &provider, const QString &model)
{
    m_currentProvider = provider.trimmed();
    m_currentModel = model.trimmed();
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
    ChatRequest request;
    if (!m_systemPrompt.trimmed().isEmpty())
    {
        request.messages.append(ChatMessage{QStringLiteral("system"), m_systemPrompt});
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
