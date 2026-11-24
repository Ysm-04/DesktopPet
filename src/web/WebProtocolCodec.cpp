#include "WebProtocolCodec.h"

#include "../ai/AIDiagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QtGlobal>

namespace
{
QString normalizedBaseUrl(QString baseUrl)
{
    baseUrl = baseUrl.trimmed();
    while (baseUrl.endsWith(QLatin1Char('/')))
    {
        baseUrl.chop(1);
    }
    return baseUrl;
}

QString ensureLeadingSlash(QString path)
{
    if (!path.startsWith(QLatin1Char('/')))
    {
        path.prepend(QLatin1Char('/'));
    }
    return path;
}

QString normalizedGeminiModel(QString model)
{
    model = model.trimmed();
    if (model.startsWith(QStringLiteral("models/")))
    {
        model.remove(0, QStringLiteral("models/").size());
    }
    return model;
}

QString geminiRole(const QString &role)
{
    if (role == QStringLiteral("assistant") || role == QStringLiteral("model"))
    {
        return QStringLiteral("model");
    }
    return QStringLiteral("user");
}

QJsonObject textPart(const QString &text)
{
    QJsonObject part;
    part.insert(QStringLiteral("text"), text);
    return part;
}

QJsonArray openAIChatMessagesArray(const ChatRequest &request)
{
    QJsonArray messages;
    for (const ChatMessage &message : request.messages)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        QJsonObject item;
        item.insert(QStringLiteral("role"), message.role.trimmed().isEmpty() ? QStringLiteral("user") : message.role.trimmed());
        item.insert(QStringLiteral("content"), message.content);
        messages.append(item);
    }
    return messages;
}

QJsonObject openAIInputMessage(const ChatMessage &message)
{
    QJsonObject item;
    item.insert(QStringLiteral("role"), message.role == QStringLiteral("assistant") ? QStringLiteral("assistant") : QStringLiteral("user"));
    item.insert(QStringLiteral("content"), message.content);
    return item;
}

bool isThinkingContentType(const QString &type)
{
    return type.contains(QStringLiteral("reasoning"), Qt::CaseInsensitive)
        || type.contains(QStringLiteral("thinking"), Qt::CaseInsensitive)
        || type.compare(QStringLiteral("thought"), Qt::CaseInsensitive) == 0
        || type.compare(QStringLiteral("summary_text"), Qt::CaseInsensitive) == 0
        || type.compare(QStringLiteral("refusal"), Qt::CaseInsensitive) == 0;
}

QString stripLeadingThinkBlocks(QString text)
{
    const QString openTag = QStringLiteral("<think>");
    const QString closeTag = QStringLiteral("</think>");

    while (true)
    {
        const QString trimmed = text.trimmed();
        if (!trimmed.startsWith(openTag, Qt::CaseInsensitive))
        {
            break;
        }

        const int closeIndex = trimmed.indexOf(closeTag, openTag.size(), Qt::CaseInsensitive);
        if (closeIndex < 0)
        {
            break;
        }

        text = trimmed.mid(closeIndex + closeTag.size()).trimmed();
    }

    return text;
}

QString extractMessageText(const QJsonObject &message)
{
    const QJsonValue contentValue = message.value(QStringLiteral("content"));
    if (contentValue.isString())
    {
        return stripLeadingThinkBlocks(contentValue.toString());
    }

    if (!contentValue.isArray())
    {
        return {};
    }

    QString result;
    const QJsonArray parts = contentValue.toArray();
    for (const QJsonValue &partValue : parts)
    {
        if (partValue.isString())
        {
            result += partValue.toString();
            continue;
        }
        if (!partValue.isObject())
        {
            continue;
        }

        const QJsonObject partObject = partValue.toObject();
        const QString partType = partObject.value(QStringLiteral("type")).toString();
        if (partObject.value(QStringLiteral("thought")).toBool(false) || isThinkingContentType(partType))
        {
            continue;
        }

        const QString text = partObject.value(QStringLiteral("text")).toString();
        if (!text.isEmpty())
        {
            result += text;
        }
    }
    return stripLeadingThinkBlocks(result);
}

QString textFromGeminiCandidate(const QJsonObject &candidate)
{
    const QJsonObject content = candidate.value(QStringLiteral("content")).toObject();
    const QJsonArray parts = content.value(QStringLiteral("parts")).toArray();

    QString result;
    for (const QJsonValue &partValue : parts)
    {
        if (!partValue.isObject())
        {
            continue;
        }

        const QJsonObject partObject = partValue.toObject();
        if (partObject.value(QStringLiteral("thought")).toBool(false))
        {
            continue;
        }

        const QString text = partObject.value(QStringLiteral("text")).toString();
        if (!text.isEmpty())
        {
            result += text;
        }
    }

    return stripLeadingThinkBlocks(result);
}

bool isHttpUrl(const QString &url)
{
    return url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        || url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

void appendCitation(
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls,
    QString title,
    QString url,
    const QString &sourceName)
{
    if (citations == nullptr || seenUrls == nullptr)
    {
        return;
    }

    title = title.trimmed();
    url = url.trimmed();
    if (url.isEmpty() || seenUrls->contains(url))
    {
        return;
    }

    WebCitation citation;
    citation.index = citations->size() + 1;
    citation.title = title.isEmpty() ? url : title;
    citation.url = url;
    citation.sourceName = sourceName;
    citations->append(citation);
    seenUrls->insert(url);
}

QString urlFromObject(const QJsonObject &object)
{
    QString url = object.value(QStringLiteral("url")).toString().trimmed();
    if (url.isEmpty())
    {
        url = object.value(QStringLiteral("uri")).toString().trimmed();
    }
    return url;
}

void collectOpenAIAnnotations(
    const QJsonArray &annotations,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls)
{
    for (const QJsonValue &annotationValue : annotations)
    {
        if (!annotationValue.isObject())
        {
            continue;
        }

        const QJsonObject annotation = annotationValue.toObject();
        const QString type = annotation.value(QStringLiteral("type")).toString();
        if (type != QStringLiteral("url_citation"))
        {
            continue;
        }

        appendCitation(
            citations,
            seenUrls,
            annotation.value(QStringLiteral("title")).toString(),
            annotation.value(QStringLiteral("url")).toString(),
            QStringLiteral("OpenAI Web Search"));
    }
}

void collectSourceObjectsRecursive(
    const QJsonValue &value,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls,
    const QString &sourceName)
{
    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array)
        {
            collectSourceObjectsRecursive(item, citations, seenUrls, sourceName);
        }
        return;
    }

    if (!value.isObject())
    {
        return;
    }

    const QJsonObject object = value.toObject();
    const auto appendSourceArray = [&object, citations, seenUrls, &sourceName](const QString &fieldName) {
        const QJsonArray sources = object.value(fieldName).toArray();
        for (const QJsonValue &sourceValue : sources)
        {
            if (!sourceValue.isObject())
            {
                continue;
            }

            const QJsonObject source = sourceValue.toObject();
            appendCitation(
                citations,
                seenUrls,
                source.value(QStringLiteral("title")).toString(),
                urlFromObject(source),
                sourceName);
        }
    };

    appendSourceArray(QStringLiteral("sources"));
    appendSourceArray(QStringLiteral("results"));

    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator)
    {
        collectSourceObjectsRecursive(iterator.value(), citations, seenUrls, sourceName);
    }
}

void collectChatAnnotations(
    const QJsonArray &annotations,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls,
    const QString &sourceName,
    bool allowWebSearchResultLocation)
{
    for (const QJsonValue &annotationValue : annotations)
    {
        if (!annotationValue.isObject())
        {
            continue;
        }

        const QJsonObject annotation = annotationValue.toObject();
        const QJsonValue nestedValue = annotation.value(QStringLiteral("url_citation"));
        if (nestedValue.isObject())
        {
            const QJsonObject nested = nestedValue.toObject();
            appendCitation(
                citations,
                seenUrls,
                nested.value(QStringLiteral("title")).toString(),
                urlFromObject(nested),
                sourceName);
            continue;
        }

        const QString type = annotation.value(QStringLiteral("type")).toString();
        const QString directUrl = urlFromObject(annotation);
        const bool typeMatches = type == QStringLiteral("url_citation")
            || (allowWebSearchResultLocation && type == QStringLiteral("web_search_result_location"))
            || (type.isEmpty() && !directUrl.isEmpty());
        if (typeMatches)
        {
            appendCitation(
                citations,
                seenUrls,
                annotation.value(QStringLiteral("title")).toString(),
                directUrl,
                sourceName);
        }
    }
}

void collectRootCitationsArray(
    const QJsonObject &root,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls,
    const QString &sourceName)
{
    const QJsonArray array = root.value(QStringLiteral("citations")).toArray();
    for (const QJsonValue &item : array)
    {
        if (item.isString())
        {
            const QString url = item.toString().trimmed();
            if (isHttpUrl(url))
            {
                appendCitation(citations, seenUrls, QString(), url, sourceName);
            }
            continue;
        }

        if (!item.isObject())
        {
            continue;
        }

        const QJsonObject obj = item.toObject();
        const QString url = urlFromObject(obj);
        if (isHttpUrl(url))
        {
            appendCitation(citations, seenUrls, obj.value(QStringLiteral("title")).toString(), url, sourceName);
        }
    }
}

void collectGeminiGroundingCitations(
    const QJsonObject &candidate,
    QVector<WebCitation> *citations,
    QSet<QString> *seenUrls)
{
    const QJsonObject grounding = candidate.value(QStringLiteral("groundingMetadata")).toObject();
    const QJsonArray chunks = grounding.value(QStringLiteral("groundingChunks")).toArray();
    for (const QJsonValue &chunkValue : chunks)
    {
        if (!chunkValue.isObject())
        {
            continue;
        }

        const QJsonObject web = chunkValue.toObject().value(QStringLiteral("web")).toObject();
        appendCitation(
            citations,
            seenUrls,
            web.value(QStringLiteral("title")).toString(),
            web.value(QStringLiteral("uri")).toString(),
            QStringLiteral("Google Search"));
    }

    const QJsonObject citationMetadata = candidate.value(QStringLiteral("citationMetadata")).toObject();
    const QJsonArray citationSources = citationMetadata.value(QStringLiteral("citationSources")).toArray();
    for (const QJsonValue &sourceValue : citationSources)
    {
        if (!sourceValue.isObject())
        {
            continue;
        }

        const QJsonObject source = sourceValue.toObject();
        appendCitation(
            citations,
            seenUrls,
            source.value(QStringLiteral("title")).toString(),
            source.value(QStringLiteral("uri")).toString(),
            QStringLiteral("Google Search"));
    }
}

bool parseJsonObject(
    const QByteArray &body,
    int httpStatus,
    QJsonObject *rootOut,
    WebChatResponse *errorResponseOut,
    QString *jsonParseErrorOut)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (jsonParseErrorOut != nullptr)
        {
            *jsonParseErrorOut = parseError.errorString();
        }
        if (errorResponseOut != nullptr)
        {
            *errorResponseOut = {false, false, {}, QStringLiteral("联网响应不是有效 JSON。"), {}, httpStatus};
        }
        return false;
    }

    const QJsonObject root = document.object();
    if (AIDiagnostics::hasErrorPayload(root))
    {
        const QString bodyError = AIDiagnostics::errorMessageFromBody(body);
        if (errorResponseOut != nullptr)
        {
            *errorResponseOut = {
                false,
                false,
                {},
                bodyError.isEmpty() ? QStringLiteral("联网响应返回错误。") : bodyError,
                {},
                httpStatus
            };
        }
        return false;
    }

    if (rootOut != nullptr)
    {
        *rootOut = root;
    }
    return true;
}

bool isDeepSeekWebSearchCall(const QJsonObject &call)
{
    const QString type = call.value(QStringLiteral("type")).toString();
    const QString kind = call.value(QStringLiteral("kind")).toString();
    const QString name = call.value(QStringLiteral("name")).toString();
    return type.contains(QStringLiteral("web_search"), Qt::CaseInsensitive)
        || kind.compare(QStringLiteral("search"), Qt::CaseInsensitive) == 0
        || kind.compare(QStringLiteral("web_search"), Qt::CaseInsensitive) == 0
        || name.contains(QStringLiteral("web_search"), Qt::CaseInsensitive);
}
}

namespace WebProtocolCodec
{
QUrl buildOpenAIResponsesUrl(const AIConfig &config)
{
    const QString baseUrl = normalizedBaseUrl(config.baseUrl);
    if (baseUrl.isEmpty())
    {
        return {};
    }

    if (baseUrl.endsWith(QStringLiteral("/responses"), Qt::CaseInsensitive))
    {
        return QUrl(baseUrl);
    }

    const QString path = config.path.trimmed();
    if (path.endsWith(QStringLiteral("/responses"), Qt::CaseInsensitive))
    {
        return QUrl(baseUrl + ensureLeadingSlash(path));
    }

    const QString chatSuffix = QStringLiteral("/chat/completions");
    if (path.endsWith(chatSuffix, Qt::CaseInsensitive))
    {
        const QString prefix = path.left(path.size() - chatSuffix.size());
        if (!prefix.isEmpty())
        {
            return QUrl(baseUrl + ensureLeadingSlash(prefix) + QStringLiteral("/responses"));
        }
    }

    if (baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
    {
        return QUrl(baseUrl + QStringLiteral("/responses"));
    }

    return QUrl(baseUrl + QStringLiteral("/v1/responses"));
}

QUrl buildOpenAIChatUrl(const AIConfig &config)
{
    const QString baseUrl = normalizedBaseUrl(config.baseUrl);
    if (baseUrl.isEmpty())
    {
        return {};
    }

    QString path = config.path.trimmed();
    if (path.isEmpty())
    {
        path = QStringLiteral("/chat/completions");
    }

    const QString normalizedPath = ensureLeadingSlash(path);
    if (baseUrl.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive)
        && normalizedPath.compare(QStringLiteral("/chat/completions"), Qt::CaseInsensitive) == 0)
    {
        return QUrl(baseUrl);
    }

    return QUrl(baseUrl + normalizedPath);
}

QUrl buildGeminiGroundingUrl(const AIConfig &config)
{
    QString baseUrl = normalizedBaseUrl(config.baseUrl);
    if (baseUrl.isEmpty())
    {
        return {};
    }

    const QString encodedModel = QString::fromUtf8(QUrl::toPercentEncoding(normalizedGeminiModel(config.model)));
    if (baseUrl.contains(QStringLiteral(":generateContent"), Qt::CaseInsensitive)
        || baseUrl.contains(QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive))
    {
        baseUrl.replace(QStringLiteral(":streamGenerateContent"), QStringLiteral(":generateContent"), Qt::CaseInsensitive);
        baseUrl.replace(QStringLiteral("{model}"), encodedModel);
        return QUrl(baseUrl);
    }

    QString path = config.path.trimmed();
    if (path.isEmpty())
    {
        path = QStringLiteral("/v1beta/models/{model}:generateContent");
    }

    path = ensureLeadingSlash(path);
    if (baseUrl.endsWith(QStringLiteral("/v1beta"), Qt::CaseInsensitive)
        && path.startsWith(QStringLiteral("/v1beta/"), Qt::CaseInsensitive))
    {
        baseUrl.chop(7);
    }
    else if (baseUrl.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive)
        && (path.startsWith(QStringLiteral("/v1/"), Qt::CaseInsensitive)
            || path.startsWith(QStringLiteral("/v1beta/"), Qt::CaseInsensitive)))
    {
        baseUrl.chop(3);
    }

    if (path.contains(QStringLiteral(":streamGenerateContent"), Qt::CaseInsensitive))
    {
        path.replace(QStringLiteral(":streamGenerateContent"), QStringLiteral(":generateContent"), Qt::CaseInsensitive);
    }

    path.replace(QStringLiteral("{model}"), encodedModel);
    return QUrl(baseUrl + path);
}

QJsonObject buildOpenAIResponsesPayload(const ChatRequest &request, const AIConfig &config)
{
    QStringList instructions;
    QJsonArray input;
    for (const ChatMessage &message : request.messages)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            instructions.append(content);
            continue;
        }

        input.append(openAIInputMessage(message));
    }

    QJsonArray tools;
    QJsonObject webSearchTool;
    webSearchTool.insert(QStringLiteral("type"), QStringLiteral("web_search"));
    tools.append(webSearchTool);

    QJsonArray include;
    include.append(QStringLiteral("web_search_call.action.sources"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), config.model);
    payload.insert(QStringLiteral("input"), input);
    payload.insert(QStringLiteral("tools"), tools);
    payload.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    payload.insert(QStringLiteral("include"), include);
    payload.insert(QStringLiteral("temperature"), config.temperature);
    payload.insert(QStringLiteral("max_output_tokens"), config.maxTokens);
    if (!instructions.isEmpty())
    {
        payload.insert(QStringLiteral("instructions"), instructions.join(QStringLiteral("\n\n")));
    }
    return payload;
}

QJsonObject buildOpenAIChatSearchPayload(const ChatRequest &request, const AIConfig &config)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("model"), config.model);
    payload.insert(QStringLiteral("messages"), openAIChatMessagesArray(request));
    payload.insert(QStringLiteral("web_search_options"), QJsonObject());
    payload.insert(QStringLiteral("stream"), false);
    payload.insert(QStringLiteral("max_tokens"), config.maxTokens);
    return payload;
}

QJsonObject buildDeepSeekWebToolsPayload(const ChatRequest &request, const AIConfig &config)
{
    QJsonArray tools;
    QJsonObject searchTool;
    searchTool.insert(QStringLiteral("type"), QStringLiteral("deepseek_web_search"));
    searchTool.insert(QStringLiteral("name"), QStringLiteral("deepseek_web_search"));
    tools.append(searchTool);

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), config.model);
    payload.insert(QStringLiteral("messages"), openAIChatMessagesArray(request));
    payload.insert(QStringLiteral("tools"), tools);
    payload.insert(QStringLiteral("stream"), false);
    payload.insert(QStringLiteral("temperature"), config.temperature);
    payload.insert(QStringLiteral("max_tokens"), config.maxTokens);
    return payload;
}

QJsonObject buildGeminiGroundingPayload(const ChatRequest &request, const AIConfig &config)
{
    QStringList systemParts;
    QJsonArray contents;

    for (const ChatMessage &message : request.messages)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        if (message.role == QStringLiteral("system"))
        {
            systemParts.append(content);
            continue;
        }

        QJsonArray parts;
        parts.append(textPart(content));

        QJsonObject item;
        item.insert(QStringLiteral("role"), geminiRole(message.role));
        item.insert(QStringLiteral("parts"), parts);
        contents.append(item);
    }

    QJsonArray tools;
    QJsonObject googleSearchTool;
    googleSearchTool.insert(QStringLiteral("google_search"), QJsonObject());
    tools.append(googleSearchTool);

    QJsonObject payload;
    payload.insert(QStringLiteral("contents"), contents);
    payload.insert(QStringLiteral("tools"), tools);

    if (!systemParts.isEmpty())
    {
        QJsonArray parts;
        parts.append(textPart(systemParts.join(QStringLiteral("\n\n"))));

        QJsonObject systemInstruction;
        systemInstruction.insert(QStringLiteral("parts"), parts);
        payload.insert(QStringLiteral("systemInstruction"), systemInstruction);
    }

    QJsonObject generationConfig;
    generationConfig.insert(QStringLiteral("temperature"), config.temperature);
    generationConfig.insert(QStringLiteral("maxOutputTokens"), config.maxTokens);
    payload.insert(QStringLiteral("generationConfig"), generationConfig);
    return payload;
}

WebChatResponse parseOpenAIResponsesBody(const QByteArray &body, int httpStatus, QString *jsonParseError)
{
    QJsonObject root;
    WebChatResponse errorResponse;
    if (!parseJsonObject(body, httpStatus, &root, &errorResponse, jsonParseError))
    {
        return errorResponse;
    }

    QString content = root.value(QStringLiteral("output_text")).toString();
    const bool hasTopLevelOutputText = !content.trimmed().isEmpty();
    QString collectedOutputText;
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;
    bool usedWeb = false;

    const QJsonArray output = root.value(QStringLiteral("output")).toArray();
    for (const QJsonValue &outputValue : output)
    {
        if (!outputValue.isObject())
        {
            continue;
        }

        const QJsonObject outputItem = outputValue.toObject();
        const QString type = outputItem.value(QStringLiteral("type")).toString();
        if (type.contains(QStringLiteral("web_search"), Qt::CaseInsensitive))
        {
            usedWeb = true;
            collectSourceObjectsRecursive(outputItem, &citations, &seenUrls, QStringLiteral("OpenAI Web Search"));
        }

        const bool collectTextFromItem = type.isEmpty()
            || type.compare(QStringLiteral("message"), Qt::CaseInsensitive) == 0;
        const QJsonArray contentItems = outputItem.value(QStringLiteral("content")).toArray();
        for (const QJsonValue &contentValue : contentItems)
        {
            if (!contentValue.isObject())
            {
                continue;
            }

            const QJsonObject contentItem = contentValue.toObject();
            const QString contentType = contentItem.value(QStringLiteral("type")).toString();
            const QString text = contentItem.value(QStringLiteral("text")).toString();
            if (collectTextFromItem
                && !hasTopLevelOutputText
                && !isThinkingContentType(contentType)
                && !text.isEmpty())
            {
                collectedOutputText += text;
            }
            collectOpenAIAnnotations(contentItem.value(QStringLiteral("annotations")).toArray(), &citations, &seenUrls);
        }
    }

    if (!hasTopLevelOutputText)
    {
        content = collectedOutputText;
    }
    content = stripLeadingThinkBlocks(content);

    usedWeb = usedWeb || !citations.isEmpty();
    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("联网响应内容为空。"), citations, httpStatus};
    }

    return {true, usedWeb, content, {}, citations, httpStatus};
}

WebChatResponse parseOpenAIChatSearchBody(const QByteArray &body, int httpStatus, QString *jsonParseError)
{
    QJsonObject root;
    WebChatResponse errorResponse;
    if (!parseJsonObject(body, httpStatus, &root, &errorResponse, jsonParseError))
    {
        return errorResponse;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject())
    {
        return {false, false, {}, QStringLiteral("联网响应没有候选结果。"), {}, httpStatus};
    }

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = extractMessageText(message);
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;

    collectChatAnnotations(
        message.value(QStringLiteral("annotations")).toArray(),
        &citations,
        &seenUrls,
        QStringLiteral("OpenAI Web Search"),
        false);
    collectRootCitationsArray(root, &citations, &seenUrls, QStringLiteral("OpenAI Web Search"));

    const bool usedWeb = !citations.isEmpty();
    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("联网响应内容为空。"), citations, httpStatus};
    }

    return {true, usedWeb, content, {}, citations, httpStatus};
}

WebChatResponse parseDeepSeekWebToolsBody(const QByteArray &body, int httpStatus, QString *jsonParseError)
{
    QJsonObject root;
    WebChatResponse errorResponse;
    if (!parseJsonObject(body, httpStatus, &root, &errorResponse, jsonParseError))
    {
        return errorResponse;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject())
    {
        return {false, false, {}, QStringLiteral("DeepSeek 联网响应没有候选结果。"), {}, httpStatus};
    }

    const QJsonObject message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const QString content = extractMessageText(message);
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;
    bool usedWeb = false;

    collectChatAnnotations(
        message.value(QStringLiteral("annotations")).toArray(),
        &citations,
        &seenUrls,
        QStringLiteral("DeepSeek Web Search"),
        true);

    const QJsonArray serverToolCalls = message.value(QStringLiteral("server_tool_calls")).toArray();
    for (const QJsonValue &callValue : serverToolCalls)
    {
        if (!callValue.isObject())
        {
            continue;
        }

        const QJsonObject call = callValue.toObject();
        if (isDeepSeekWebSearchCall(call))
        {
            usedWeb = true;
            collectSourceObjectsRecursive(call, &citations, &seenUrls, QStringLiteral("DeepSeek Web Search"));
        }
    }

    usedWeb = usedWeb || !citations.isEmpty();
    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("DeepSeek 联网响应内容为空。"), citations, httpStatus};
    }

    return {true, usedWeb, content, {}, citations, httpStatus};
}

WebChatResponse parseGeminiGroundingBody(const QByteArray &body, int httpStatus, QString *jsonParseError)
{
    QJsonObject root;
    WebChatResponse errorResponse;
    if (!parseJsonObject(body, httpStatus, &root, &errorResponse, jsonParseError))
    {
        return errorResponse;
    }

    const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
    if (candidates.isEmpty() || !candidates.first().isObject())
    {
        return {false, false, {}, QStringLiteral("Gemini 联网响应没有候选结果。"), {}, httpStatus};
    }

    const QJsonObject candidate = candidates.first().toObject();
    const QString content = textFromGeminiCandidate(candidate);
    QVector<WebCitation> citations;
    QSet<QString> seenUrls;
    collectGeminiGroundingCitations(candidate, &citations, &seenUrls);

    const QJsonObject grounding = candidate.value(QStringLiteral("groundingMetadata")).toObject();
    const bool hasGroundingEvidence =
        !grounding.value(QStringLiteral("webSearchQueries")).toArray().isEmpty()
        || !grounding.value(QStringLiteral("groundingChunks")).toArray().isEmpty()
        || !grounding.value(QStringLiteral("groundingSupports")).toArray().isEmpty()
        || !grounding.value(QStringLiteral("searchEntryPoint")).toObject().isEmpty();
    const bool usedWeb = hasGroundingEvidence || !citations.isEmpty();

    if (content.trimmed().isEmpty())
    {
        return {false, usedWeb, {}, QStringLiteral("Gemini 联网响应内容为空。"), citations, httpStatus};
    }

    return {true, usedWeb, content, {}, citations, httpStatus};
}
}
