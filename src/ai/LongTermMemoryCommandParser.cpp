#include "LongTermMemoryCommandParser.h"

#include <QRegularExpression>

namespace
{
QString stripWrappingQuotesAndPunctuation(QString text)
{
    text = text.trimmed();
    while (text.size() >= 2)
    {
        const QChar first = text.front();
        const QChar last = text.back();
        const bool matchedPair =
            (first == QLatin1Char('"') && last == QLatin1Char('"'))
            || (first == QLatin1Char('\'') && last == QLatin1Char('\''))
            || (first == QChar(0x201C) && last == QChar(0x201D))
            || (first == QChar(0x2018) && last == QChar(0x2019))
            || (first == QChar(0x300C) && last == QChar(0x300D))
            || (first == QChar(0x300A) && last == QChar(0x300B))
            || (first == QChar(0x3010) && last == QChar(0x3011));
        if (!matchedPair)
        {
            break;
        }
        text = text.mid(1, text.size() - 2).trimmed();
    }

    while (!text.isEmpty()
           && (text.back() == QChar(0x3002)
               || text.back() == QLatin1Char('!')
               || text.back() == QChar(0xFF01)))
    {
        text.chop(1);
        text = text.trimmed();
    }

    return text;
}

bool endsLikeQuestion(const QString &trimmed)
{
    static const QRegularExpression TrailingQuestionPattern(
        QStringLiteral("(?:[?？]|(?:吗|嘛|么|了吗)\\s*[。！!?？]*)$"));
    return TrailingQuestionPattern.match(trimmed).hasMatch();
}

bool isClearAllKeyword(const QString &keyword)
{
    const QString compact = keyword.trimmed();
    return compact == QStringLiteral("全部")
        || compact == QStringLiteral("所有")
        || compact == QStringLiteral("一切")
        || compact == QStringLiteral("长期记忆")
        || compact == QStringLiteral("全部记忆")
        || compact == QStringLiteral("所有记忆");
}

bool requiresConversationContext(const QString &payload)
{
    const QString cleaned = stripWrappingQuotesAndPunctuation(payload);
    if (cleaned.isEmpty())
    {
        return false;
    }

    // 包含明确的上文时间/位置指代，如“刚刚讨论的框架”“刚才说的项目”“上面提到的那几点”
    static const QRegularExpression BackReferencePattern(QStringLiteral(
        "(?:刚刚|刚才|方才|上面|前面|上文|上一条|上一轮|前面聊|刚才聊|刚刚聊|刚才说|刚刚说|刚才讨论|刚刚讨论|上面说|前面说)"));
    if (BackReferencePattern.match(cleaned).hasMatch())
    {
        return true;
    }

    // 包含“我们讨论的/聊的/说的...”指代
    static const QRegularExpression DiscussedReferencePattern(QStringLiteral(
        "(?:我们|咱们)?(?:刚|刚刚|刚才)?(?:聊|讨论|说|提到|讲|总结|整理)(?:过)?的"));
    if (DiscussedReferencePattern.match(cleaned).hasMatch())
    {
        return true;
    }

    // 短指代名词短语，如“这个项目”“那个框架”“这件事”“这些内容”“它”
    static const QRegularExpression ShortDemonstrativePattern(QStringLiteral(
        "^(?:我的|我们的|咱们的)?(?:这个|那个|该|这项|那项|这条|那条|这些|那些|这件事|那件事|这一点|那一点|这几点|那几点|它)"
        "(?:项目|框架|工程|仓库|工具|软件|系统|网站|偏好|习惯|事情|内容|信息|结论|要点|问题|方案)?$"));
    if (ShortDemonstrativePattern.match(cleaned).hasMatch())
    {
        return true;
    }

    return false;
}

LongTermMemoryCategory inferCategory(const QString &content)
{
    static const QRegularExpression PreferencePattern(QStringLiteral(
        "(?:喜欢|偏好|习惯|讨厌|不喜欢|爱吃|不爱|称呼我|叫我|回复风格|简短|详细|不要太长|语气温和)"));
    if (PreferencePattern.match(content).hasMatch())
    {
        return LongTermMemoryCategory::Preference;
    }

    static const QRegularExpression ProjectPattern(QStringLiteral(
        "(?:项目|仓库|工程|代码|技术栈|框架|正在做|开发|编译器|CMake|Qt|C\\+\\+|Python|TypeScript|Rust)"),
        QRegularExpression::CaseInsensitiveOption);
    if (ProjectPattern.match(content).hasMatch())
    {
        return LongTermMemoryCategory::Project;
    }

    static const QRegularExpression ProfilePattern(QStringLiteral(
        "(?:我是|我叫|我的名字|住在|来自|工作是|职业是|专业是|城市是|常驻|在读)"));
    if (ProfilePattern.match(content).hasMatch())
    {
        return LongTermMemoryCategory::Profile;
    }

    return LongTermMemoryCategory::Fact;
}
}

LongTermMemoryCommand LongTermMemoryCommandParser::parse(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    static const QRegularExpression ClearPattern(QStringLiteral(
        "^(?:请|帮我)?(?:清空|清除|删除|忘掉|忘记)(?:全部|所有|一切)?(?:我的)?(?:长期记忆|所有记忆|全部记忆)[。！!]?$"));
    if (ClearPattern.match(trimmed).hasMatch())
    {
        return {LongTermMemoryCommandType::ClearRejected, {}, LongTermMemoryCategory::Fact};
    }

    static const QRegularExpression ListExplicitPattern(QStringLiteral(
        "^(?:请|帮我)?(?:查看|列出|显示|看看)(?:一下)?(?:我的)?(?:长期记忆|记忆本|记忆列表)[。！!？?]?$"));
    static const QRegularExpression ListQueryRememberedPattern(QStringLiteral(
        "^你(?:都)?记住了?我(?:什么|哪些(?:信息|事情|东西|偏好)?)[？?]?$"));
    static const QRegularExpression ListQueryRecallPattern(QStringLiteral(
        "^你(?:都)?记得我(?:哪些信息|什么偏好|哪些偏好)[？?]?$"));
    if (ListExplicitPattern.match(trimmed).hasMatch()
        || ListQueryRememberedPattern.match(trimmed).hasMatch()
        || ListQueryRecallPattern.match(trimmed).hasMatch())
    {
        return {LongTermMemoryCommandType::List, {}, LongTermMemoryCategory::Fact};
    }

    if (endsLikeQuestion(trimmed))
    {
        return {};
    }

    static const QRegularExpression ForgetExplicitPattern(QStringLiteral(
        "^(?:请|帮我)?(?:忘掉|忘记|删除|移除|清除)(?:关于)?(.+?)(?:的(?:长期)?记忆|这条记忆|这些记忆)[。！!]?$"));
    const QRegularExpressionMatch forgetMatch = ForgetExplicitPattern.match(trimmed);
    if (forgetMatch.hasMatch())
    {
        const QString keyword = stripWrappingQuotesAndPunctuation(forgetMatch.captured(1));
        if (keyword.isEmpty())
        {
            return {};
        }
        if (isClearAllKeyword(keyword))
        {
            return {LongTermMemoryCommandType::ClearRejected, {}, LongTermMemoryCategory::Fact};
        }
        return {LongTermMemoryCommandType::Forget, keyword, LongTermMemoryCategory::Fact};
    }

    static const QRegularExpression ForgetStopRememberPattern(QStringLiteral(
        "^(?:请|帮我)?(?:别再?记(?:住)?|不要再?记(?:住)?)(?:关于)?(.+?)(?:了)?[。！!]?$"));
    const QRegularExpressionMatch stopMatch = ForgetStopRememberPattern.match(trimmed);
    if (stopMatch.hasMatch())
    {
        const QString keyword = stripWrappingQuotesAndPunctuation(stopMatch.captured(1));
        if (keyword.isEmpty())
        {
            return {};
        }
        if (isClearAllKeyword(keyword))
        {
            return {LongTermMemoryCommandType::ClearRejected, {}, LongTermMemoryCategory::Fact};
        }
        return {LongTermMemoryCommandType::Forget, keyword, LongTermMemoryCategory::Fact};
    }

    // 匹配“把/将...提炼/整理/总结/存入/写入/记到...长期记忆中”
    static const QRegularExpression PutIntoMemoryPattern(QStringLiteral(
        "^(?:以后)?(?:请|帮我|麻烦)?(?:把|将)(.+?)"
        "(提炼|整理|总结|保存|存入|存到|存进|写入|写到|写进|记入|记到|记进|加入|加到|加进|添加|收录)"
        "(?:到|进|入)?(?:我的)?(?:长期记忆|记忆本|记忆列表|记忆)(?:中|里|里面|内)?(?:吧|呀|哦)?[。！!]?$"));
    const QRegularExpressionMatch putMatch = PutIntoMemoryPattern.match(trimmed);
    if (putMatch.hasMatch())
    {
        const QString target = stripWrappingQuotesAndPunctuation(putMatch.captured(1));
        const QString actionVerb = putMatch.captured(2);
        if (target.size() < 2)
        {
            return {};
        }

        const bool isSummarizeVerb = actionVerb == QStringLiteral("提炼")
            || actionVerb == QStringLiteral("整理")
            || actionVerb == QStringLiteral("总结");
        if (isSummarizeVerb || requiresConversationContext(target))
        {
            return {LongTermMemoryCommandType::RememberFromContext, target, inferCategory(target)};
        }

        return {LongTermMemoryCommandType::Remember, target, inferCategory(target)};
    }

    // 匹配“提炼/整理/总结一下...到长期记忆中”
    static const QRegularExpression SummarizeIntoMemoryPattern(QStringLiteral(
        "^(?:以后)?(?:请|帮我|麻烦)?(?:提炼|整理|总结)(?:一下)?(.+?)(?:并)?"
        "(?:保存|存入|存到|存进|写入|写到|写进|记入|记到|记进|加入|加到|加进|添加|收录|到|进|入)"
        "(?:我的)?(?:长期记忆|记忆本|记忆列表)(?:中|里|里面|内)?(?:吧|呀|哦)?[。！!]?$"));
    const QRegularExpressionMatch summarizeMatch = SummarizeIntoMemoryPattern.match(trimmed);
    if (summarizeMatch.hasMatch())
    {
        const QString target = stripWrappingQuotesAndPunctuation(summarizeMatch.captured(1));
        return {
            LongTermMemoryCommandType::RememberFromContext,
            target.isEmpty() ? trimmed : target,
            inferCategory(target),
        };
    }

    static const QRegularExpression RememberPattern(QStringLiteral(
        "^(?:以后)?(?:请|帮我)?(?:记住|记一下|记下来|牢记)(?:一下)?(?:[:：,，\\s]*)(.+)$"));
    const QRegularExpressionMatch rememberMatch = RememberPattern.match(trimmed);
    if (rememberMatch.hasMatch())
    {
        QString payload = stripWrappingQuotesAndPunctuation(rememberMatch.captured(1));
        static const QRegularExpression TrailingIntoMemorySuffix(QStringLiteral(
            "(?:并)?(?:提炼|整理|总结|保存|存入|存到|存进|写入|写到|写进|记入|记到|记进|加入|加到|加进)?"
            "(?:到|进|入)(?:我的)?(?:长期记忆|记忆本|记忆列表|记忆)(?:中|里|里面|内)?(?:吧|呀|哦)?$"));
        payload.remove(TrailingIntoMemorySuffix);
        payload = stripWrappingQuotesAndPunctuation(payload);
        if (payload.size() < 2)
        {
            return {};
        }

        if (requiresConversationContext(payload))
        {
            return {LongTermMemoryCommandType::RememberFromContext, payload, inferCategory(payload)};
        }

        return {LongTermMemoryCommandType::Remember, payload, inferCategory(payload)};
    }

    return {};
}
