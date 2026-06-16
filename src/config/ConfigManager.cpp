#include "ConfigManager.h"

#include "../util/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>
#include <QtGlobal>

namespace
{
const QString AppConfigFileName = QStringLiteral("app_config.json");
const QString AIConfigFileName = QStringLiteral("ai_config.json");
const QString ConversationHistoryFileName = QStringLiteral("conversation_history.json");

QJsonObject windowObjectFromConfig(const AppConfig &config)
{
    QJsonObject window;
    window.insert(QStringLiteral("x"), config.windowPosition.x());
    window.insert(QStringLiteral("y"), config.windowPosition.y());
    window.insert(QStringLiteral("alwaysOnTop"), config.alwaysOnTop);
    window.insert(QStringLiteral("scale"), config.scale);
    return window;
}

QJsonObject systemObjectFromConfig(const AppConfig &config)
{
    QJsonObject system;
    system.insert(QStringLiteral("launchAtStartup"), config.launchAtStartup);
    return system;
}

QJsonObject performanceObjectFromConfig(const AppConfig &config)
{
    QJsonObject performance;
    performance.insert(QStringLiteral("mode"), config.performanceMode);
    performance.insert(QStringLiteral("pauseWhenHidden"), config.pauseWhenHidden);
    performance.insert(QStringLiteral("enableLazyLoad"), config.enableLazyLoad);
    performance.insert(QStringLiteral("enableAnimationPrewarm"), config.enableAnimationPrewarm);
    performance.insert(QStringLiteral("animationCacheLimitMb"), config.animationCacheLimitMb);
    performance.insert(QStringLiteral("unloadAnimationsWhenHidden"), config.unloadAnimationsWhenHidden);
    return performance;
}

QJsonObject chatObjectFromConfig(const AppConfig &config)
{
    QJsonObject chat;
    chat.insert(QStringLiteral("requestContextMessageLimit"), config.requestContextMessageLimit);
    chat.insert(QStringLiteral("memoryHistoryMessageLimit"), config.memoryHistoryMessageLimit);
    chat.insert(QStringLiteral("saveConversationHistory"), config.saveConversationHistory);
    chat.insert(QStringLiteral("savedHistoryMessageLimit"), config.savedHistoryMessageLimit);
    return chat;
}

QJsonObject characterObjectFromConfig(const AppConfig &config)
{
    QJsonObject character;
    character.insert(QStringLiteral("id"), config.characterId);
    return character;
}

QJsonObject reminderObjectFromConfig(const AppConfig &config)
{
    QJsonObject reminder;
    reminder.insert(QStringLiteral("soundId"), config.reminderSoundId);
    reminder.insert(QStringLiteral("soundEnabled"), config.reminderSoundEnabled);
    reminder.insert(QStringLiteral("soundVolume"), qBound(0.0, config.reminderSoundVolume, 1.0));
    return reminder;
}

QString normalizedProviderName(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("custom") : normalized;
}

bool isRemovedProviderName(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    return normalized == QStringLiteral("claude")
        || normalized == QStringLiteral("calude");
}

QJsonObject objectFromAIProviderConfig(const AIConfig &config)
{
    QJsonObject root;
    root.insert(QStringLiteral("provider"), normalizedProviderName(config.provider));
    root.insert(QStringLiteral("protocol"), config.protocol);
    root.insert(QStringLiteral("baseUrl"), config.baseUrl);
    root.insert(QStringLiteral("model"), config.model);
    root.insert(QStringLiteral("path"), config.path);
    root.insert(QStringLiteral("apiKeyStorage"), config.apiKeyStorage);
    root.insert(QStringLiteral("apiKeyEncrypted"), config.apiKeyEncrypted);
    root.insert(QStringLiteral("allowPlainApiKey"), config.allowPlainApiKey);
    if (config.allowPlainApiKey && config.apiKeyStorage == QStringLiteral("plain-json"))
    {
        root.insert(QStringLiteral("apiKey"), config.apiKey);
    }
    root.insert(QStringLiteral("stream"), config.stream);
    root.insert(QStringLiteral("timeoutMs"), config.timeoutMs);
    root.insert(QStringLiteral("temperature"), config.temperature);
    root.insert(QStringLiteral("maxTokens"), config.maxTokens);
    return root;
}

QJsonObject objectFromAIConfigStore(const AIConfigStore &store)
{
    QJsonObject providers;
    for (auto iterator = store.providers.constBegin(); iterator != store.providers.constEnd(); ++iterator)
    {
        const QString provider = normalizedProviderName(iterator.key());
        if (isRemovedProviderName(provider))
        {
            continue;
        }

        AIConfig config = iterator.value();
        config.provider = provider;
        providers.insert(provider, objectFromAIProviderConfig(config));
    }

    QJsonObject root;
    const QString activeProvider = normalizedProviderName(store.activeProvider);
    root.insert(
        QStringLiteral("activeProvider"),
        isRemovedProviderName(activeProvider) ? QStringLiteral("custom") : activeProvider);
    root.insert(QStringLiteral("providers"), providers);
    return root;
}

AIConfig aiProviderConfigFromObject(const QString &provider, const QJsonObject &root)
{
    AIConfig config = defaultAIConfigForProvider(provider);
    config.provider = provider;
    config.protocol = root.value(QStringLiteral("protocol")).toString(config.protocol);
    config.baseUrl = root.value(QStringLiteral("baseUrl")).toString(config.baseUrl);
    config.model = root.value(QStringLiteral("model")).toString(config.model);
    config.path = root.value(QStringLiteral("path")).toString(config.path);
    config.apiKeyStorage = root.value(QStringLiteral("apiKeyStorage")).toString(config.apiKeyStorage);
    config.apiKeyEncrypted = root.value(QStringLiteral("apiKeyEncrypted")).toString(config.apiKeyEncrypted);
    config.allowPlainApiKey = root.value(QStringLiteral("allowPlainApiKey")).toBool(config.allowPlainApiKey);
    if (config.allowPlainApiKey && config.apiKeyStorage == QStringLiteral("plain-json"))
    {
        config.apiKey = root.value(QStringLiteral("apiKey")).toString(config.apiKey);
    }
    config.stream = root.value(QStringLiteral("stream")).toBool(config.stream);
    config.timeoutMs = root.value(QStringLiteral("timeoutMs")).toInt(config.timeoutMs);
    config.temperature = root.value(QStringLiteral("temperature")).toDouble(config.temperature);
    config.maxTokens = root.value(QStringLiteral("maxTokens")).toInt(config.maxTokens);
    return config;
}

AIConfig activeAIProviderConfig(const AIConfigStore &store)
{
    const QString activeProvider = normalizedProviderName(store.activeProvider);
    const auto iterator = store.providers.constFind(activeProvider);
    if (iterator != store.providers.constEnd())
    {
        return iterator.value();
    }

    return defaultAIConfigForProvider(activeProvider);
}
}

ConfigManager::ConfigManager() = default;

AppConfig ConfigManager::loadAppConfig() const
{
    AppConfig config;

    QFile file(appConfigPath());
    if (!file.exists())
    {
        return config;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::warning(QStringLiteral("Unable to read app config."));
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenConfig(appConfigPath(), QStringLiteral("app config"));
        Logger::warning(QStringLiteral("App config is broken; default config will be used."));
        return config;
    }

    const QJsonObject root = document.object();
    const QJsonObject window = root.value(QStringLiteral("window")).toObject();
    if (window.contains(QStringLiteral("x")) && window.contains(QStringLiteral("y")))
    {
        config.windowPosition = QPoint(
            window.value(QStringLiteral("x")).toInt(config.windowPosition.x()),
            window.value(QStringLiteral("y")).toInt(config.windowPosition.y()));
        config.hasWindowPosition = true;
    }

    if (window.contains(QStringLiteral("alwaysOnTop")))
    {
        config.alwaysOnTop = window.value(QStringLiteral("alwaysOnTop")).toBool(config.alwaysOnTop);
    }

    if (window.contains(QStringLiteral("scale")))
    {
        config.scale = window.value(QStringLiteral("scale")).toDouble(config.scale);
    }

    const QJsonObject system = root.value(QStringLiteral("system")).toObject();
    if (system.contains(QStringLiteral("launchAtStartup")))
    {
        config.launchAtStartup = system.value(QStringLiteral("launchAtStartup")).toBool(config.launchAtStartup);
    }

    const QJsonObject performance = root.value(QStringLiteral("performance")).toObject();
    if (performance.contains(QStringLiteral("mode")))
    {
        config.performanceMode = performance.value(QStringLiteral("mode")).toString(config.performanceMode);
    }

    if (performance.contains(QStringLiteral("pauseWhenHidden")))
    {
        config.pauseWhenHidden = performance.value(QStringLiteral("pauseWhenHidden")).toBool(config.pauseWhenHidden);
    }

    if (performance.contains(QStringLiteral("enableLazyLoad")))
    {
        config.enableLazyLoad = performance.value(QStringLiteral("enableLazyLoad")).toBool(config.enableLazyLoad);
    }

    if (performance.contains(QStringLiteral("enableAnimationPrewarm")))
    {
        config.enableAnimationPrewarm = performance.value(QStringLiteral("enableAnimationPrewarm")).toBool(config.enableAnimationPrewarm);
    }

    if (performance.contains(QStringLiteral("animationCacheLimitMb")))
    {
        config.animationCacheLimitMb = performance.value(QStringLiteral("animationCacheLimitMb")).toInt(config.animationCacheLimitMb);
    }

    if (performance.contains(QStringLiteral("unloadAnimationsWhenHidden")))
    {
        config.unloadAnimationsWhenHidden = performance.value(QStringLiteral("unloadAnimationsWhenHidden")).toBool(config.unloadAnimationsWhenHidden);
    }

    const QJsonObject chat = root.value(QStringLiteral("chat")).toObject();
    if (chat.contains(QStringLiteral("requestContextMessageLimit")))
    {
        config.requestContextMessageLimit = chat.value(QStringLiteral("requestContextMessageLimit")).toInt(config.requestContextMessageLimit);
    }

    if (chat.contains(QStringLiteral("memoryHistoryMessageLimit")))
    {
        config.memoryHistoryMessageLimit = chat.value(QStringLiteral("memoryHistoryMessageLimit")).toInt(config.memoryHistoryMessageLimit);
    }

    if (chat.contains(QStringLiteral("saveConversationHistory")))
    {
        config.saveConversationHistory = chat.value(QStringLiteral("saveConversationHistory")).toBool(config.saveConversationHistory);
    }

    if (chat.contains(QStringLiteral("savedHistoryMessageLimit")))
    {
        config.savedHistoryMessageLimit = chat.value(QStringLiteral("savedHistoryMessageLimit")).toInt(config.savedHistoryMessageLimit);
    }

    const QJsonObject character = root.value(QStringLiteral("character")).toObject();
    if (character.contains(QStringLiteral("id")))
    {
        config.characterId = character.value(QStringLiteral("id")).toString(config.characterId).trimmed();
    }

    const QJsonObject reminder = root.value(QStringLiteral("reminder")).toObject();
    if (reminder.contains(QStringLiteral("soundId")))
    {
        config.reminderSoundId = reminder.value(QStringLiteral("soundId")).toString(config.reminderSoundId).trimmed();
        if (config.reminderSoundId.isEmpty())
        {
            config.reminderSoundId = QStringLiteral("reminder_default");
        }
    }

    if (reminder.contains(QStringLiteral("soundEnabled")))
    {
        config.reminderSoundEnabled = reminder.value(QStringLiteral("soundEnabled")).toBool(config.reminderSoundEnabled);
    }

    if (reminder.contains(QStringLiteral("soundVolume")))
    {
        config.reminderSoundVolume = qBound(0.0, reminder.value(QStringLiteral("soundVolume")).toDouble(config.reminderSoundVolume), 1.0);
    }

    return config;
}

AIConfigStore ConfigManager::loadAIConfigStore() const
{
    AIConfigStore store;

    QFile file(aiConfigPath());
    if (!file.exists())
    {
        return store;
    }

    if (!file.open(QIODevice::ReadOnly))
    {
        Logger::warning(QStringLiteral("Unable to read AI config."));
        return store;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        file.close();
        backupBrokenConfig(aiConfigPath(), QStringLiteral("AI config"));
        Logger::warning(QStringLiteral("AI config is broken; default config will be used."));
        return store;
    }

    const QJsonObject root = document.object();
    store.activeProvider = normalizedProviderName(root.value(QStringLiteral("activeProvider")).toString(store.activeProvider));
    bool removedLegacyProviderConfig = false;
    if (isRemovedProviderName(store.activeProvider))
    {
        store.activeProvider = QStringLiteral("custom");
        removedLegacyProviderConfig = true;
    }

    const QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    for (auto iterator = providers.constBegin(); iterator != providers.constEnd(); ++iterator)
    {
        if (!iterator.value().isObject())
        {
            continue;
        }

        const QString provider = normalizedProviderName(iterator.key());
        const QString declaredProvider = normalizedProviderName(
            iterator.value().toObject().value(QStringLiteral("provider")).toString(provider));
        if (isRemovedProviderName(provider) || isRemovedProviderName(declaredProvider))
        {
            removedLegacyProviderConfig = true;
            continue;
        }

        store.providers.insert(provider, aiProviderConfigFromObject(provider, iterator.value().toObject()));
    }

    if (removedLegacyProviderConfig)
    {
        file.close();
        Logger::info(QStringLiteral("Removed legacy AI provider config."));
        saveAIConfigStore(store);
    }

    return store;
}

AIConfig ConfigManager::loadAIConfig() const
{
    return activeAIProviderConfig(loadAIConfigStore());
}

bool ConfigManager::saveAppConfig(const AppConfig &config) const
{
    const QString directoryPath = configDirectoryPath();
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        Logger::warning(QStringLiteral("Unable to create config directory."));
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("window"), windowObjectFromConfig(config));
    root.insert(QStringLiteral("system"), systemObjectFromConfig(config));
    root.insert(QStringLiteral("performance"), performanceObjectFromConfig(config));
    root.insert(QStringLiteral("chat"), chatObjectFromConfig(config));
    root.insert(QStringLiteral("character"), characterObjectFromConfig(config));
    root.insert(QStringLiteral("reminder"), reminderObjectFromConfig(config));

    QFile file(appConfigPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        Logger::warning(QStringLiteral("Unable to open app config for writing."));
        return false;
    }

    const QJsonDocument document(root);
    return file.write(document.toJson(QJsonDocument::Indented)) >= 0;
}

bool ConfigManager::saveAIConfigStore(const AIConfigStore &store) const
{
    const QString directoryPath = configDirectoryPath();
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        Logger::warning(QStringLiteral("Unable to create config directory."));
        return false;
    }

    QFile file(aiConfigPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        Logger::warning(QStringLiteral("Unable to open AI config for writing."));
        return false;
    }

    const QJsonDocument document(objectFromAIConfigStore(store));
    return file.write(document.toJson(QJsonDocument::Indented)) >= 0;
}

bool ConfigManager::saveAIConfig(const AIConfig &config) const
{
    AIConfig normalizedConfig = config;
    normalizedConfig.provider = normalizedProviderName(normalizedConfig.provider);
    if (isRemovedProviderName(normalizedConfig.provider))
    {
        normalizedConfig = defaultAIConfigForProvider(QStringLiteral("custom"));
    }

    AIConfigStore store = loadAIConfigStore();
    store.activeProvider = normalizedConfig.provider;
    store.providers.insert(normalizedConfig.provider, normalizedConfig);
    return saveAIConfigStore(store);
}

QString ConfigManager::appConfigPath() const
{
    return QDir(configDirectoryPath()).filePath(AppConfigFileName);
}

QString ConfigManager::aiConfigPath() const
{
    return QDir(configDirectoryPath()).filePath(AIConfigFileName);
}

QString ConfigManager::conversationHistoryPath() const
{
    return QDir(configDirectoryPath()).filePath(ConversationHistoryFileName);
}

QString ConfigManager::configDirectoryPath() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!path.isEmpty())
    {
        return path;
    }

    return QDir::currentPath();
}

void ConfigManager::backupBrokenConfig(const QString &filePath, const QString &configName) const
{
    QFile file(filePath);
    if (!file.exists())
    {
        return;
    }

    const QFileInfo fileInfo(filePath);
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString backupPath = fileInfo.dir().filePath(
        fileInfo.completeBaseName() + QStringLiteral(".broken.") + timestamp + QStringLiteral(".json"));
    int suffix = 1;
    while (QFile::exists(backupPath))
    {
        backupPath = fileInfo.dir().filePath(
            fileInfo.completeBaseName()
            + QStringLiteral(".broken.")
            + timestamp
            + QStringLiteral("-")
            + QString::number(suffix)
            + QStringLiteral(".json"));
        ++suffix;
    }

    if (file.rename(backupPath))
    {
        Logger::warning(QStringLiteral("Broken %1 was backed up: %2").arg(configName, backupPath));
        return;
    }

    Logger::warning(QStringLiteral("Failed to back up broken %1: %2").arg(configName, filePath));
}
