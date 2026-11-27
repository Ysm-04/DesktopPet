#include "PetWindow.h"

#include "../agent_bridge/AgentStatusCodec.h"
#include "../agent_bridge/AgentStatusServer.h"
#include "../ai/AIProviderFactory.h"
#include "../ai/ConversationManager.h"
#include "../ai/ConversationStore.h"
#include "../ai/LongTermMemoryCommandParser.h"
#include "../ai/LongTermMemoryExtractor.h"
#include "../ai/LongTermMemoryStore.h"
#include "../assistant/CommandDispatcher.h"
#include "../character/CharacterPackageLoader.h"
#include "../character/CharacterPackageRepository.h"
#include "../character/CharacterPersonaStore.h"
#include "../config/ConfigManager.h"
#include "../notification/NotificationDispatcher.h"
#include "../reminder/ReminderCommandHandler.h"
#include "../reminder/ReminderManager.h"
#include "../reminder/ReminderSoundPlayer.h"
#include "../reminder/ReminderSoundRepository.h"
#include "../system/StartupManager.h"
#include "../util/Logger.h"
#include "../util/MarkdownFormatter.h"
#include "../web/WebCapabilityDetector.h"
#include "../web/WebChatManager.h"
#include "../web/WebStore.h"
#include "../weather/WeatherManager.h"
#include "../weather/WeatherParser.h"
#include "../weather/WeatherStore.h"
#include "AgentStatusBar.h"
#include "ChatBubble.h"
#include "ChatHistoryPanel.h"
#include "ChatInputDialog.h"
#include "PetView.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHideEvent>
#include <QImage>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QStringList>
#include <QTime>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <memory>
#include <utility>

namespace
{
constexpr int MaxUserMessageLength = 4000;
constexpr int ChatInputLowerOffsetY = 48;
constexpr int StreamBubbleUpdateIntervalMs = 80;
constexpr int MinAnimationTargetSide = 32;
constexpr int MaxAnimationTargetSide = 2048;
constexpr int LowPowerFpsCap = 6;
constexpr int ChatFinishedReturnDelayMs = 1500;
constexpr int StandardPrewarmIntervalMs = 800;
constexpr int LowPowerPrewarmIntervalMs = 1500;
constexpr qint64 BytesPerMegabyte = 1024 * 1024;
constexpr int ReminderVisibleAlphaThreshold = 24;

QRect visiblePixmapRect(const QPixmap &pixmap)
{
    if (pixmap.isNull())
    {
        return {};
    }

    QImage image = pixmap.toImage();
    if (!image.hasAlphaChannel())
    {
        return image.rect();
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (qAlpha(line[x]) < ReminderVisibleAlphaThreshold)
            {
                continue;
            }

            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
        }
    }

    if (right < left || bottom < top)
    {
        return {};
    }

    return QRect(QPoint(left, top), QPoint(right, bottom));
}

int boundedAnimationTargetSide(double sideLength)
{
    const double boundedSideLength = qBound(
        static_cast<double>(MinAnimationTargetSide),
        sideLength,
        static_cast<double>(MaxAnimationTargetSide));
    return qRound(boundedSideLength);
}

int evenBoundedHistoryLimit(int value, int minimum, int maximum)
{
    const int boundedValue = qBound(minimum, value, maximum);
    return boundedValue - (boundedValue % 2);
}

QString megabytesText(qint64 bytes)
{
    return QString::number(static_cast<double>(bytes) / static_cast<double>(BytesPerMegabyte), 'f', 1);
}

AppConfig normalizedAppConfig(AppConfig config)
{
    config.launchAtStartup = StartupManager::isLaunchAtStartupEnabled();
    config.scale = qBound(0.5, config.scale, 2.0);
    if (config.performanceMode != QStringLiteral("standard")
        && config.performanceMode != QStringLiteral("low-power"))
    {
        config.performanceMode = QStringLiteral("standard");
    }
    config.animationCacheLimitMb = qBound(64, config.animationCacheLimitMb, 1024);
    config.requestContextMessageLimit = qBound(0, config.requestContextMessageLimit, 500);
    config.requestContextMaxChars = qBound(4000, config.requestContextMaxChars, 120000);
    config.memoryHistoryMessageLimit = evenBoundedHistoryLimit(config.memoryHistoryMessageLimit, 2, 5000);
    config.savedHistoryMessageLimit = evenBoundedHistoryLimit(config.savedHistoryMessageLimit, 2, 10000);
    config.characterId = config.characterId.trimmed();
    if (!CharacterPackageRepository::hasPackage(config.characterId))
    {
        config.characterId = CharacterPackageRepository::defaultCharacterId();
    }
    config.reminderSoundId = ReminderSoundRepository::soundInfo(config.reminderSoundId).id;
    config.reminderSoundVolume = qBound(0.0, config.reminderSoundVolume, 1.0);
    config.sedentaryReminderMinutes = qBound(30, config.sedentaryReminderMinutes, 180);
    const QString notifyLevel = config.agentBubbleNotifyLevel.trimmed().toLower();
    config.agentBubbleNotifyLevel = (notifyLevel == QStringLiteral("all") || notifyLevel == QStringLiteral("silent"))
        ? notifyLevel
        : QStringLiteral("attention");
    return config;
}

QScreen *screenForPopup(const QWidget *reference)
{
    if (reference != nullptr)
    {
        if (QScreen *screen = QGuiApplication::screenAt(reference->frameGeometry().center()))
        {
            return screen;
        }
    }

    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos()))
    {
        return screen;
    }

    return QGuiApplication::primaryScreen();
}

void centerDialogOnScreen(QDialog *dialog, const QWidget *reference)
{
    if (dialog == nullptr)
    {
        return;
    }

    QScreen *screen = screenForPopup(reference);
    if (screen == nullptr)
    {
        return;
    }

    const QRect availableGeometry = screen->availableGeometry();
    const int maxWidth = qMax(400, qRound(availableGeometry.width() * 0.92));
    const int maxHeight = qMax(360, qRound(availableGeometry.height() * 0.88));
    if (dialog->minimumWidth() > maxWidth)
    {
        dialog->setMinimumWidth(maxWidth);
    }
    if (dialog->minimumHeight() > maxHeight)
    {
        dialog->setMinimumHeight(maxHeight);
    }
    QSize dialogSize = dialog->size().isValid()
        ? dialog->size()
        : dialog->sizeHint();
    dialogSize = QSize(
        qBound(dialog->minimumWidth(), dialogSize.width(), maxWidth),
        qBound(dialog->minimumHeight(), dialogSize.height(), maxHeight));
    dialog->resize(dialogSize);

    const int x = availableGeometry.left() + qMax(0, (availableGeometry.width() - dialogSize.width()) / 2);
    const int y = availableGeometry.top() + qMax(0, (availableGeometry.height() - dialogSize.height()) / 2);
    dialog->move(QPoint(x, y));
}

QString userVisibleErrorMessage(const ChatResponse &response)
{
    QString message = response.errorMessage.trimmed();
    if (message.isEmpty())
    {
        message = QStringLiteral("未知错误。");
    }

    if (response.httpStatus > 0)
    {
        message = QStringLiteral("HTTP ") + QString::number(response.httpStatus) + QStringLiteral("：") + message;
    }

    return message;
}


QString webCitationListText(const QVector<WebCitation> &citations)
{
    QStringList lines;
    const int count = qMin(citations.size(), 10);
    for (int index = 0; index < count; ++index)
    {
        const WebCitation &citation = citations.at(index);
        const QString formatted = MarkdownFormatter::formatCitationMarkdown(
            index + 1,
            citation.title,
            citation.url);
        if (!formatted.isEmpty())
        {
            lines.append(formatted);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString stripWebStatusNoticeFooter(QString text)
{
    static const QStringList notices = {
        QStringLiteral("（模型未使用联网来源）"),
        QStringLiteral("（模型使用了联网能力，但未返回可展示来源）"),
    };
    bool stripped = true;
    while (stripped)
    {
        stripped = false;
        text = text.trimmed();
        for (const QString &notice : notices)
        {
            if (text.endsWith(notice))
            {
                text.chop(notice.size());
                stripped = true;
            }
        }
    }
    return text.trimmed();
}

}

PetWindow::PetWindow(QWidget *parent)
    : PetWindow(parent, AppConfig())
{
}

PetWindow::PetWindow(QWidget *parent, const AppConfig &initialConfig)
    : QWidget(parent)
    , m_agentStatusBar(std::make_unique<AgentStatusBar>())
    , m_agentStatusServer(std::make_unique<AgentStatusServer>())
    , m_chatBubble(std::make_unique<ChatBubble>())
    , m_chatHistoryPanel(std::make_unique<ChatHistoryPanel>(this))
    , m_chatInputDialog(std::make_unique<ChatInputDialog>(MaxUserMessageLength, this))
    , m_conversationManager(std::make_unique<ConversationManager>())
    , m_conversationStore(std::make_unique<ConversationStore>(ConfigManager().conversationHistoryPath()))
    , m_longTermMemoryStore(std::make_unique<LongTermMemoryStore>(ConfigManager().longTermMemoryPath()))
    , m_longTermMemoryExtractor(std::make_unique<LongTermMemoryExtractor>())
    , m_notificationDispatcher(std::make_unique<NotificationDispatcher>())
    , m_reminderManager(std::make_unique<ReminderManager>())
    , m_reminderSoundPlayer(std::make_unique<ReminderSoundPlayer>())
    , m_webChatManager(std::make_unique<WebChatManager>())
    , m_weatherManager(std::make_unique<WeatherManager>())
    , m_petView(new PetView(this))
    , m_appConfig(initialConfig)
    , m_dragging(false)
    , m_alwaysOnTop(true)
    , m_centerNextFrame(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setMouseTracking(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_petView);

    m_frameAnimator.setFrameChangedCallback([this](const QPixmap &pixmap) {
        setDisplayPixmap(pixmap, m_centerNextFrame);
        m_centerNextFrame = false;
    });

    m_frameAnimator.setClipFinishedCallback([this](const QString &nextState) {
        playResolvedState(m_stateMachine.finishState(nextState), false);
    });
    m_frameAnimator.setPlaybackErrorCallback([this](const QString &state, const QString &error) {
        m_petView->effects().setState(QStringLiteral("error"));
        Logger::warning(QStringLiteral("Animation playback failed: state=%1 reason=%2").arg(state, error));
        showBubbleMessage(QStringLiteral("动画播放失败：") + error);
        if (state != QStringLiteral("idle"))
        {
            const QString fallbackState = m_stateMachine.requestState(QStringLiteral("idle"), StateRequestSource::System);
            if (!fallbackState.isEmpty() && fallbackState != state)
            {
                playResolvedState(fallbackState, false);
            }
        }
    });

    m_idleBehaviorTimer.setSingleShot(true);
    connect(&m_idleBehaviorTimer, &QTimer::timeout, this, [this]() {
        playIdleBehavior();
    });

    m_behaviorReturnTimer.setSingleShot(true);
    connect(&m_behaviorReturnTimer, &QTimer::timeout, this, [this]() {
        returnToIdleFromBehavior();
    });

    m_streamBubbleUpdateTimer.setSingleShot(true);
    connect(&m_streamBubbleUpdateTimer, &QTimer::timeout, this, [this]() {
        flushStreamingBubble(false);
    });

    m_animationPrewarmTimer.setSingleShot(true);
    connect(&m_animationPrewarmTimer, &QTimer::timeout, this, [this]() {
        processAnimationPrewarm();
    });

    m_interactionClock.start();
    m_lastCareResetMs = m_interactionClock.elapsed();

    m_pendingTapTimer.setSingleShot(true);
    connect(&m_pendingTapTimer, &QTimer::timeout, this, [this]() {
        triggerPendingTapInteraction();
    });

    m_startupGreetingTimer.setSingleShot(true);
    connect(&m_startupGreetingTimer, &QTimer::timeout, this, [this]() {
        tryTriggerStartupGreeting();
    });

    m_proactiveCareTimer.setInterval(60000);
    connect(&m_proactiveCareTimer, &QTimer::timeout, this, [this]() {
        checkProactiveCare();
    });

    m_agentPruneTimer.setInterval(1000);
    connect(&m_agentPruneTimer, &QTimer::timeout, this, [this]() {
        pruneExpiredExternalAgentSessions();
    });

    m_badgeFallbackTimer.setSingleShot(true);
    connect(&m_badgeFallbackTimer, &QTimer::timeout, this, [this]() {
        m_fallbackBadgeState.clear();
        syncBadgeLogicalState();
    });

    QPointer<PetWindow> window(this);
    refreshChatInputWebToggle();
    m_chatInputDialog->setSubmitCallback([window](const QString &message, bool webEnabled) {
        return !window.isNull() && window->submitChatMessage(message, webEnabled);
    });
    m_chatInputDialog->setWebToggleChangedCallback([window](bool webEnabled) {
        if (!window.isNull())
        {
            window->saveWebTogglePreference(webEnabled);
        }
    });
    m_reminderManager->setTriggeredCallback([window](const ReminderItem &item) {
        if (!window.isNull())
        {
            window->handleTriggeredReminder(item);
        }
    });
    m_agentStatusServer->setEventCallback([window](const AgentStatusEvent &event) {
        if (!window.isNull())
        {
            window->handleExternalAgentEvent(event);
        }
    });
    m_agentStatusBar->setDismissSessionCallback([window](const QString &sessionKey) {
        if (!window.isNull())
        {
            window->dismissExternalAgentSession(sessionKey);
        }
    });
    m_agentStatusBar->setClearAllCallback([window]() {
        if (!window.isNull())
        {
            window->clearExternalAgentSessions();
        }
    });
    m_agentStatusBar->setClearCompletedCallback([window]() {
        if (!window.isNull())
        {
            const qint64 nowMs = window->m_interactionClock.elapsed();
            window->m_agentSessionRegistry.clearCompleted(nowMs);
            window->syncExternalAgentPresentation(window->m_agentSessionRegistry.arbitrate(nowMs), nullptr, false);
        }
    });

    loadInitialImage();
    configureAgentBridge();
}

PetWindow::~PetWindow()
{
    m_agentPruneTimer.stop();
    if (m_agentStatusServer)
    {
        m_agentStatusServer->stop();
    }
    if (m_agentStatusBar)
    {
        m_agentStatusBar->hideAll();
    }
    if (m_longTermMemoryExtractor)
    {
        m_longTermMemoryExtractor->cancel();
    }
    saveConversationHistoryIfNeeded();
    if (m_conversationManager)
    {
        m_conversationManager->setArchiveHistoryLoader({});
        m_conversationManager->setLongTermMemoryLoader({});
    }
}

void PetWindow::applyAppConfig(const AppConfig &config)
{
    const AppConfig normalizedConfig = normalizedAppConfig(config);
    const bool characterChanged = m_appConfig.characterId != normalizedConfig.characterId;
    const bool rebuildClips = !qFuzzyCompare(m_appConfig.scale, normalizedConfig.scale)
        || m_appConfig.performanceMode != normalizedConfig.performanceMode
        || m_appConfig.enableLazyLoad != normalizedConfig.enableLazyLoad;
    const bool effectsChanged = m_appConfig.enableCharacterEffects != normalizedConfig.enableCharacterEffects;
    const bool animationCachePolicyChanged =
        m_appConfig.enableAnimationPrewarm != normalizedConfig.enableAnimationPrewarm
        || m_appConfig.animationCacheLimitMb != normalizedConfig.animationCacheLimitMb
        || m_appConfig.unloadAnimationsWhenHidden != normalizedConfig.unloadAnimationsWhenHidden;
    const bool loadPersistedHistory = !m_appConfig.saveConversationHistory
        && normalizedConfig.saveConversationHistory;
    const bool proactiveCareTurnedOn = !m_appConfig.enableProactiveCare
        && normalizedConfig.enableProactiveCare;

    m_appConfig = normalizedConfig;
    setAlwaysOnTop(m_appConfig.alwaysOnTop);
    configureConversation(loadPersistedHistory);
    configureAgentBridge();

    if (!m_appConfig.enableTouchInteraction)
    {
        m_pendingTapTimer.stop();
    }

    if (!m_appConfig.enableStartupGreeting)
    {
        m_startupGreetingTimer.stop();
    }

    if (!m_appConfig.enableProactiveCare)
    {
        m_proactiveCareTimer.stop();
    }
    else if (isVisible())
    {
        if (proactiveCareTurnedOn)
        {
            m_lastCareResetMs = m_interactionClock.elapsed();
        }
        if (!m_proactiveCareTimer.isActive())
        {
            m_proactiveCareTimer.start(60000);
        }
    }

    if (m_appConfig.hasWindowPosition && isPointVisibleOnScreen(m_appConfig.windowPosition))
    {
        move(m_appConfig.windowPosition);
    }

    if (characterChanged)
    {
        m_frameAnimator.stop();
        loadCharacterPackage(m_appConfig.characterId, false);
    }
    else if (rebuildClips && !m_characterPackage.states.isEmpty())
    {
        const QString previousState = m_stateMachine.currentState().isEmpty()
            ? QStringLiteral("idle")
            : m_stateMachine.currentState();

        m_frameAnimator.stop();
        buildAnimationClips();

        const QString nextState = m_clips.contains(previousState)
            ? previousState
            : QStringLiteral("idle");
        if (m_clips.contains(nextState))
        {
            playResolvedState(m_stateMachine.requestState(nextState), false);
        }
    }

    if (effectsChanged && !characterChanged && !rebuildClips)
    {
        m_petView->configureEffects(m_characterPackage.effects, m_appConfig.enableCharacterEffects, isLowPowerMode());
        m_petView->effects().setState(m_stateMachine.currentState());
        m_petView->effects().setPresented(isVisible());
        syncBadgeLogicalState();
        resize(m_petView->sizeHint());
        updateBubblePosition();
    }

    if (isAnimationCacheManagementEnabled())
    {
        if (animationCachePolicyChanged && !rebuildClips)
        {
            m_animationPrewarmAttemptedStates.clear();
            m_animationPrewarmQueue.clear();
            trimAnimationCache(QStringLiteral("config updated"));
        }
        scheduleAnimationPrewarm();
    }
    else
    {
        stopAnimationPrewarm();
    }
}

AppConfig PetWindow::currentAppConfig() const
{
    AppConfig config = m_appConfig;
    config.windowPosition = pos();
    config.hasWindowPosition = true;
    config.alwaysOnTop = m_alwaysOnTop;
    config.launchAtStartup = StartupManager::isLaunchAtStartupEnabled();
    return config;
}

void PetWindow::pauseAnimation()
{
    if (!m_appConfig.pauseWhenHidden)
    {
        return;
    }

    m_returnToIdleAfterResume = m_behaviorReturnTimer.isActive();
    m_idleBehaviorTimer.stop();
    m_behaviorReturnTimer.stop();
    stopAnimationPrewarm();
    m_frameAnimator.pause();
}

void PetWindow::resumeAnimation()
{
    m_frameAnimator.resume();

    if (m_stateMachine.currentState() == QStringLiteral("idle"))
    {
        scheduleIdleBehavior();
    }
    else if (m_returnToIdleAfterResume)
    {
        m_behaviorReturnTimer.start(4000);
    }

    m_returnToIdleAfterResume = false;
    scheduleAnimationPrewarm();
}

void PetWindow::showBubbleMessage(const QString &message, bool renderMarkdown)
{
    hideReminderActions();
    m_chatBubble->showMessage(message, bubbleAnchorPosition(), 10000, false, renderMarkdown);
}

void PetWindow::showPendingRequestBubble(const QString &message)
{
    hideReminderActions();
    // The request callback replaces this message on completion, failure or cancellation.
    m_chatBubble->showMessage(message, bubbleAnchorPosition(), 0);
}

void PetWindow::openSettingsDialog()
{
    ConfigManager configManager;
    WeatherStore weatherStore;
    WebStore webStore;
    QString weatherConfigError;
    const WeatherConfig weatherConfig = weatherStore.load(&weatherConfigError);
    if (!weatherConfigError.isEmpty())
    {
        Logger::warning(QStringLiteral("Weather config load warning: ") + weatherConfigError);
    }
    QString webConfigError;
    const WebConfig webConfig = webStore.load(&webConfigError);
    if (!webConfigError.isEmpty())
    {
        Logger::warning(QStringLiteral("Web config load warning: ") + webConfigError);
    }
    SettingsDialog::LongTermMemoryCallbacks longTermMemoryCallbacks;
    longTermMemoryCallbacks.addItem = [this](const QString &content, LongTermMemoryCategory category, LongTermMemoryItem *savedItem, QString *errorMessage) {
        if (!m_longTermMemoryStore)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("长期记忆存储未初始化。");
            }
            return false;
        }
        return m_longTermMemoryStore->upsert(content, category, LongTermMemorySource::Manual, savedItem, nullptr, errorMessage);
    };
    longTermMemoryCallbacks.updateItem = [this](const QString &id, const QString &content, LongTermMemoryCategory category, LongTermMemoryItem *updatedItem, QString *errorMessage) {
        if (!m_longTermMemoryStore)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("长期记忆存储未初始化。");
            }
            return false;
        }
        return m_longTermMemoryStore->updateItem(id, content, category, updatedItem, errorMessage);
    };
    longTermMemoryCallbacks.removeItem = [this](const QString &id, QString *errorMessage) {
        if (!m_longTermMemoryStore)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("长期记忆存储未初始化。");
            }
            return false;
        }
        return m_longTermMemoryStore->removeById(id, errorMessage);
    };
    longTermMemoryCallbacks.clearAll = [this](QString *errorMessage) {
        if (!m_longTermMemoryStore)
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("长期记忆存储未初始化。");
            }
            return false;
        }
        return m_longTermMemoryStore->clear(errorMessage);
    };

    SettingsDialog dialog(
        configManager.loadAIConfigStore(),
        currentAppConfig(),
        weatherConfig,
        webConfig,
        m_conversationManager ? m_conversationManager->fullHistory() : QVector<ChatMessage>(),
        m_reminderManager ? m_reminderManager->allReminders() : QVector<ReminderItem>(),
        m_longTermMemoryStore ? m_longTermMemoryStore->load() : QVector<LongTermMemoryItem>(),
        [this]() {
            return isManualStateSwitchLocked();
        },
        [this]() {
            clearConversation();
        },
        [this](const QString &reminderId, QString *errorMessage) {
            return m_reminderManager && m_reminderManager->cancelReminder(reminderId, errorMessage);
        },
        [this](const QString &reminderId, const QString &title, const QDateTime &remindAt, const ReminderRecurrence &recurrence, ReminderItem *updatedItem, QString *errorMessage) {
            if (!m_reminderManager)
            {
                if (errorMessage != nullptr)
                {
                    *errorMessage = QStringLiteral("提醒功能初始化失败。");
                }
                return false;
            }

            if (!m_reminderManager->updateReminder(reminderId, title, remindAt, recurrence, errorMessage))
            {
                return false;
            }

            if (updatedItem != nullptr)
            {
                const QVector<ReminderItem> reminders = m_reminderManager->allReminders();
                bool found = false;
                for (const ReminderItem &item : reminders)
                {
                    if (item.id == reminderId)
                    {
                        *updatedItem = item;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    if (errorMessage != nullptr)
                    {
                        *errorMessage = QStringLiteral("提醒已更新，但没有找到更新后的记录。");
                    }
                    return false;
                }
            }
            return true;
        },
        [this](QString *errorMessage) {
            return m_reminderManager && m_reminderManager->pruneFinishedReminders(20, errorMessage);
        },
        [this](const QString &soundId, double volume) {
            if (m_reminderSoundPlayer)
            {
                m_reminderSoundPlayer->play(soundId, volume);
            }
        },
        std::move(longTermMemoryCallbacks),
        [this](const QString &changedCharacterId) {
            const QString trimmedId = changedCharacterId.trimmed();
            const QString activeId = m_characterPackage.id.trimmed().isEmpty()
                ? m_appConfig.characterId.trimmed()
                : m_characterPackage.id.trimmed();
            if (trimmedId != activeId)
            {
                return;
            }

            if (CharacterPackageRepository::hasUserPackage(activeId))
            {
                QString loadError;
                const CharacterPackage reloadedPackage = CharacterPackageLoader::load(
                    CharacterPackageRepository::packagePath(activeId),
                    &loadError);
                if (loadError.isEmpty())
                {
                    m_characterPackage.persona = reloadedPackage.persona;
                    m_characterPackage.dialogues = reloadedPackage.dialogues;
                }
            }

            refreshActiveCharacterPersona();
        },
        this);
    centerDialogOnScreen(&dialog, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    if (!configManager.saveAIConfigStore(dialog.aiConfigStore()))
    {
        Logger::warning(QStringLiteral("Failed to save AI config from settings dialog."));
    }

    AppConfig acceptedAppConfig = dialog.appConfig();
    QString startupError;
    if (!StartupManager::setLaunchAtStartupEnabled(acceptedAppConfig.launchAtStartup, &startupError))
    {
        Logger::warning(QStringLiteral("Failed to update startup setting: ") + startupError);
        acceptedAppConfig.launchAtStartup = StartupManager::isLaunchAtStartupEnabled();
        QMessageBox::warning(
            this,
            QStringLiteral("开机自启动"),
            startupError.isEmpty() ? QStringLiteral("开机自启动设置保存失败。") : startupError);
    }

    applyAppConfig(acceptedAppConfig);
    if (!configManager.saveAppConfig(currentAppConfig()))
    {
        Logger::warning(QStringLiteral("Failed to save app config from settings dialog."));
    }

    QString saveWeatherConfigError;
    if (!weatherStore.save(dialog.weatherConfig(), &saveWeatherConfigError))
    {
        Logger::warning(QStringLiteral("Failed to save weather config from settings dialog: ") + saveWeatherConfigError);
    }

    QString saveWebConfigError;
    if (!webStore.save(dialog.webConfig(), &saveWebConfigError))
    {
        Logger::warning(QStringLiteral("Failed to save web config from settings dialog: ") + saveWebConfigError);
    }
    refreshChatInputWebToggle();

}

void PetWindow::activateFromExternalInstance()
{
    if (!isVisible())
    {
        show();
        resumeAnimation();
    }

    if (isMinimized())
    {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }

    if (QWidget *modalWidget = QApplication::activeModalWidget())
    {
        modalWidget->raise();
        modalWidget->activateWindow();
        return;
    }

    raise();
    activateWindow();
    if (m_reminderManager)
    {
        m_reminderManager->checkDueRemindersNow();
    }
    updateBubblePosition();
}

void PetWindow::setSettingsFallbackInContextMenuEnabled(bool enabled)
{
    m_settingsFallbackInContextMenuEnabled = enabled;
}

void PetWindow::setTrayNotificationCallback(std::function<bool(const QString &, const QString &)> callback)
{
    if (m_notificationDispatcher)
    {
        m_notificationDispatcher->setShowCallback(std::move(callback));
    }
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    resetBubbleAutoHideTimer();

    QMenu menu(this);

    QAction *topAction = menu.addAction(QStringLiteral("取消置顶"));
    topAction->setCheckable(true);
    topAction->setChecked(m_alwaysOnTop);

    const bool aiRequestRunning = hasActiveAIRequest();
    QAction *chatAction = menu.addAction(QStringLiteral("聊天"));
    chatAction->setEnabled(!aiRequestRunning);
    QAction *showConversationAction = menu.addAction(QStringLiteral("显示对话"));
    QAction *cancelAIAction = menu.addAction(QStringLiteral("取消当前请求"));
    cancelAIAction->setEnabled(aiRequestRunning);
    QAction *clearConversationAction = menu.addAction(QStringLiteral("清空对话"));
    clearConversationAction->setEnabled(m_conversationManager && m_conversationManager->hasHistory());
    QAction *settingsAction = nullptr;
    if (m_settingsFallbackInContextMenuEnabled)
    {
        settingsAction = menu.addAction(QStringLiteral("设置"));
    }

    addStateTestActions(&menu);

    menu.addSeparator();
    QAction *exitAction = menu.addAction(QStringLiteral("退出"));

    QAction *selectedAction = menu.exec(event->globalPos());
    if (selectedAction == topAction)
    {
        setAlwaysOnTop(!m_alwaysOnTop);
    }
    else if (selectedAction == chatAction)
    {
        startChat();
    }
    else if (selectedAction == showConversationAction)
    {
        showConversationHistory();
    }
    else if (selectedAction == cancelAIAction)
    {
        cancelActiveAIRequest();
    }
    else if (selectedAction == clearConversationAction)
    {
        clearConversation();
    }
    else if (settingsAction != nullptr && selectedAction == settingsAction)
    {
        openSettingsDialog();
    }
    else if (selectedAction == exitAction)
    {
        close();
    }
    else if (selectedAction != nullptr && selectedAction->data().isValid())
    {
        if (isManualStateSwitchLocked())
        {
            return;
        }

        playState(selectedAction->data().toString(), false);
    }
}

void PetWindow::startChat()
{
    if (!m_chatInputDialog)
    {
        return;
    }

    refreshChatInputWebToggle();
    m_chatInputDialog->showAt(chatInputAnchorPosition());
}

bool PetWindow::submitChatMessage(const QString &message, bool webEnabled)
{
    const QString normalizedMessage = message.trimmed();
    if (normalizedMessage.isEmpty())
    {
        return false;
    }

    if (normalizedMessage.size() > MaxUserMessageLength)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("消息太长，请控制在 4000 字以内。"));
        return false;
    }

    CommandDispatcher dispatcher;
    const CommandDispatchResult result = dispatcher.dispatch(normalizedMessage);
    if (result.action == CommandDispatchAction::Reminder)
    {
        return handleReminderChatMessage(result.message);
    }

    if (result.action == CommandDispatchAction::Weather)
    {
        if (webEnabled)
        {
            const WeatherQuery parsedQuery = WeatherParser().parse(result.message);
            if (!parsedQuery.unsupportedReason.isEmpty())
            {
                saveWebTogglePreference(webEnabled);
                return submitWebChatMessage(result.message);
            }
        }
        return handleWeatherChatMessage(result.message);
    }

    if (handleLongTermMemoryChatMessage(result.message))
    {
        return true;
    }

    saveWebTogglePreference(webEnabled);
    return webEnabled ? submitWebChatMessage(result.message) : submitAiChatMessage(result.message);
}

bool PetWindow::handleLongTermMemoryChatMessage(const QString &message)
{
    const LongTermMemoryCommand command = LongTermMemoryCommandParser::parse(message);
    if (command.type == LongTermMemoryCommandType::None)
    {
        return false;
    }

    if (!m_longTermMemoryStore)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("长期记忆功能初始化失败。"));
        m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
        return true;
    }

    if (command.type == LongTermMemoryCommandType::ClearRejected)
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("为防止误删全部长期记忆，请打开 设置 → 长期记忆 点击“清空全部”并二次确认。"));
        m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
        return true;
    }

    if (command.type == LongTermMemoryCommandType::List)
    {
        const QVector<LongTermMemoryItem> items = m_longTermMemoryStore->load();
        if (items.isEmpty())
        {
            playState(QStringLiteral("talk"), false);
            showBubbleMessage(QStringLiteral("当前还没有保存任何长期记忆。你可以对我说“记住我喜欢简洁回答”，或在 设置 → 长期记忆 中添加。"));
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        QStringList lines;
        lines.append(QStringLiteral("**当前长期记忆（共 %1 条）**\n").arg(items.size()));
        const int maxPreviewCount = qMin(static_cast<int>(items.size()), 8);
        for (int index = 0; index < maxPreviewCount; ++index)
        {
            const LongTermMemoryItem &item = items.at(index);
            lines.append(QStringLiteral("- **[%1]** %2")
                             .arg(longTermMemoryCategoryDisplayName(item.category), item.content));
        }
        if (items.size() > maxPreviewCount)
        {
            lines.append(QStringLiteral("\n……完整列表请在 设置 → 长期记忆 中查看或编辑。"));
        }

        playState(QStringLiteral("talk"), false);
        showBubbleMessage(lines.join(QStringLiteral("\n")), true);
        m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
        return true;
    }

    if (command.type == LongTermMemoryCommandType::Forget)
    {
        QString errorMessage;
        const QVector<LongTermMemoryItem> removed = m_longTermMemoryStore->removeMatchingKeyword(command.payload, &errorMessage);
        if (!errorMessage.isEmpty())
        {
            playState(QStringLiteral("error"), false);
            showBubbleMessage(errorMessage);
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        if (removed.isEmpty())
        {
            playState(QStringLiteral("talk"), false);
            showBubbleMessage(QStringLiteral("长期记忆中没有找到包含“%1”的条目。").arg(command.payload));
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        QStringList previews;
        const int previewLimit = qMin(static_cast<int>(removed.size()), 3);
        for (int index = 0; index < previewLimit; ++index)
        {
            previews.append(removed.at(index).content);
        }
        const QString suffix = removed.size() > previewLimit
            ? QStringLiteral(" 等共 %1 条").arg(removed.size())
            : QString();

        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("已从长期记忆中忘掉：%1%2。").arg(previews.join(QStringLiteral("；")), suffix));
        m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
        return true;
    }

    if (command.type == LongTermMemoryCommandType::RememberFromContext)
    {
        if (hasActiveAIRequest() || m_streamingChatActive)
        {
            playState(QStringLiteral("talk"), false);
            showPendingRequestBubble(QStringLiteral("当前有对话请求正在进行，请稍后再提炼长期记忆。"));
            return true;
        }

        const QVector<ChatMessage> recentHistory = m_conversationManager
            ? m_conversationManager->history()
            : QVector<ChatMessage>();
        if (recentHistory.isEmpty())
        {
            playState(QStringLiteral("talk"), false);
            showBubbleMessage(QStringLiteral("当前还没有可参考的近期对话记录。你可以直接对我说“记住我正在开发 QtDesktopPet 桌面宠物项目”。"));
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        if (!m_longTermMemoryExtractor)
        {
            playState(QStringLiteral("error"), false);
            showBubbleMessage(QStringLiteral("长期记忆提炼器未初始化。"));
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        AIConfig config = ConfigManager().loadAIConfig();
        QString runtimeError;
        if (!AIProviderFactory::prepareRuntimeConfig(config, &runtimeError))
        {
            playState(QStringLiteral("error"), false);
            showBubbleMessage(runtimeError);
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        config.stream = false;
        config.timeoutMs = qBound(10000, config.timeoutMs, 30000);
        config.temperature = 0.1;
        config.maxTokens = 1024;

        std::unique_ptr<LLMProvider> provider = AIProviderFactory::createProvider(config);
        if (!provider)
        {
            playState(QStringLiteral("error"), false);
            showBubbleMessage(QStringLiteral("当前 Provider 协议暂未接入。"));
            m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return true;
        }

        stopAnimationPrewarm();
        playState(QStringLiteral("think"), false);
        hideReminderActions();
        m_explicitMemoryExtractionActive = true;
        showPendingRequestBubble(QStringLiteral("正在结合最近对话提炼长期记忆..."));

        QPointer<PetWindow> window(this);
        m_longTermMemoryExtractor->extractFromContextAsync(
            std::move(provider),
            message,
            recentHistory,
            *m_longTermMemoryStore,
            [window](const ExplicitExtractionResult &result) {
                if (window.isNull())
                {
                    return;
                }

                window->m_explicitMemoryExtractionActive = false;
                if (!result.success || result.savedItems.isEmpty())
                {
                    window->playState(QStringLiteral("error"), false);
                    window->showBubbleMessage(result.errorMessage.isEmpty()
                        ? QStringLiteral("未能从最近对话中提炼出明确的长期记忆。")
                        : result.errorMessage);
                    window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
                    return;
                }

                QStringList lines;
                lines.append(QStringLiteral("**已结合最近对话提炼并写入长期记忆（共 %1 条）**\n")
                                 .arg(result.savedItems.size()));
                for (const LongTermMemoryItem &item : result.savedItems)
                {
                    lines.append(QStringLiteral("- **[%1]** %2")
                                     .arg(longTermMemoryCategoryDisplayName(item.category), item.content));
                }
                if (!window->m_appConfig.enableLongTermMemory)
                {
                    lines.append(QStringLiteral("\n（提示：长期记忆开关当前未启用，可在 设置 → 长期记忆 中开启）"));
                }

                window->playState(QStringLiteral("talk"), false);
                window->showBubbleMessage(lines.join(QStringLiteral("\n")), true);
                window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            });
        return true;
    }

    LongTermMemoryItem savedItem;
    bool updatedExisting = false;
    QString errorMessage;
    if (!m_longTermMemoryStore->upsert(
            command.payload,
            command.suggestedCategory,
            LongTermMemorySource::Explicit,
            &savedItem,
            &updatedExisting,
            &errorMessage))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(errorMessage.isEmpty() ? QStringLiteral("写入长期记忆失败。") : errorMessage);
        m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
        return true;
    }

    QString notice = updatedExisting
        ? QStringLiteral("已更新长期记忆（%1）：%2")
              .arg(longTermMemoryCategoryDisplayName(savedItem.category), savedItem.content)
        : QStringLiteral("已记住（%1）：%2")
              .arg(longTermMemoryCategoryDisplayName(savedItem.category), savedItem.content);
    if (!m_appConfig.enableLongTermMemory)
    {
        notice += QStringLiteral("\n（提示：长期记忆开关当前未启用，可在 设置 → 长期记忆 中开启）");
    }

    playState(QStringLiteral("talk"), false);
    showBubbleMessage(notice);
    m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
    return true;
}

void PetWindow::triggerLongTermMemoryExtractionIfNeeded(const QString &userMessage, const QString &assistantMessage)
{
    if (!m_appConfig.enableLongTermMemory
        || !m_appConfig.autoExtractLongTermMemory
        || !m_longTermMemoryStore
        || !m_longTermMemoryExtractor
        || m_longTermMemoryExtractor->isBusy()
        || !LongTermMemoryExtractor::shouldConsiderExchange(userMessage, assistantMessage))
    {
        return;
    }

    AIConfig config = ConfigManager().loadAIConfig();
    QString runtimeError;
    if (!AIProviderFactory::prepareRuntimeConfig(config, &runtimeError))
    {
        return;
    }

    config.stream = false;
    config.timeoutMs = qBound(10000, config.timeoutMs, 30000);
    config.temperature = 0.1;
    config.maxTokens = 1024;

    std::unique_ptr<LLMProvider> provider = AIProviderFactory::createProvider(config);
    if (!provider)
    {
        return;
    }

    QVector<ChatMessage> recentContext;
    if (m_conversationManager)
    {
        const QVector<ChatMessage> fullRecent = m_conversationManager->history();
        const int contextEnd = qMax(0, static_cast<int>(fullRecent.size()) - 2);
        const int contextStart = qMax(0, contextEnd - 4);
        if (contextEnd > contextStart)
        {
            recentContext = fullRecent.mid(contextStart, contextEnd - contextStart);
        }
    }

    m_longTermMemoryExtractor->extractAsync(
        std::move(provider),
        userMessage,
        assistantMessage,
        *m_longTermMemoryStore,
        {},
        recentContext);
}

bool PetWindow::handleReminderChatMessage(const QString &message)
{
    if (!m_reminderManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("提醒功能初始化失败。"));
        return false;
    }

    const ReminderCommandResult result = ReminderCommandHandler::handle(
        message,
        *m_reminderManager);
    playState(result.success ? QStringLiteral("talk") : QStringLiteral("error"), false);
    showBubbleMessage(result.message);
    return result.success;
}


bool PetWindow::handleWeatherChatMessage(const QString &message)
{
    if (!m_weatherManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("天气功能初始化失败。"));
        return false;
    }

    if (hasActiveAIRequest() || m_streamingChatActive)
    {
        playState(QStringLiteral("talk"), false);
        showPendingRequestBubble(QStringLiteral("当前请求正在进行，请稍后再查天气。"));
        return false;
    }

    if (m_weatherManager->isBusy())
    {
        playState(QStringLiteral("talk"), false);
        showPendingRequestBubble(QStringLiteral("天气查询正在进行，请稍后。"));
        return false;
    }

    WeatherStore weatherStore;
    QString weatherConfigError;
    const WeatherConfig weatherConfig = weatherStore.load(&weatherConfigError);
    if (!weatherConfigError.isEmpty())
    {
        Logger::warning(QStringLiteral("Weather config load warning: ") + weatherConfigError);
    }

    stopAnimationPrewarm();
    playState(QStringLiteral("think"), false);
    hideReminderActions();
    showPendingRequestBubble(QStringLiteral("正在查询天气..."));

    QPointer<PetWindow> window(this);
    m_weatherManager->queryWeather(message, weatherConfig, [window](const WeatherQueryResult &result) {
        if (window.isNull())
        {
            return;
        }

        if (result.success)
        {
            window->playState(QStringLiteral("talk"), false);
            window->showBubbleMessage(result.message);
            window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return;
        }

        window->playState(QStringLiteral("error"), false);
        window->showBubbleMessage(result.errorMessage.isEmpty() ? QStringLiteral("天气查询失败。") : result.errorMessage);
        window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
    });
    return true;
}



bool PetWindow::submitWebChatMessage(const QString &message)
{
    if (!m_webChatManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("联网模式初始化失败。"));
        return false;
    }

    if (hasActiveWebRequest())
    {
        playState(QStringLiteral("talk"), false);
        showPendingRequestBubble(QStringLiteral("联网请求正在进行，请稍后。"));
        return false;
    }

    if ((m_conversationManager && m_conversationManager->isBusy()) || m_streamingChatActive)
    {
        playState(QStringLiteral("talk"), false);
        showPendingRequestBubble(QStringLiteral("AI 回复正在进行，请稍后再使用联网模式。"));
        return false;
    }

    ConfigManager configManager;
    AIConfig aiConfig = configManager.loadAIConfig();

    WebStore webStore;
    QString webConfigError;
    WebConfig webConfig = webStore.load(&webConfigError);
    if (!webConfigError.isEmpty())
    {
        Logger::warning(QStringLiteral("Web config load warning: ") + webConfigError);
    }

    const WebCapability capability = WebCapabilityDetector::detect(aiConfig, webConfig);
    if (!capability.supported)
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(capability.userMessage);
        refreshChatInputWebToggle();
        return false;
    }

    QString runtimeError;
    AIConfig runtimeConfig = aiConfig;
    if (!AIProviderFactory::prepareRuntimeConfig(runtimeConfig, &runtimeError))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(runtimeError);
        return false;
    }

    const QString chatMessage = message.trimmed();
    if (chatMessage.isEmpty())
    {
        return false;
    }

    if (!m_conversationManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("AI 对话功能初始化失败。"));
        return false;
    }

    m_conversationManager->setConversationMetadata(runtimeConfig.provider, runtimeConfig.model);
    ChatRequest request = m_conversationManager->buildRequestForUserMessage(chatMessage);
    if (request.messages.isEmpty())
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("联网请求内容为空。"));
        return false;
    }

    stopAnimationPrewarm();
    playState(QStringLiteral("think"), false);
    hideReminderActions();
    showPendingRequestBubble(QStringLiteral("正在联网思考..."));

    QPointer<PetWindow> window(this);
    WebChatRequest webRequest;
    webRequest.chatRequest = request;
    webRequest.aiConfig = runtimeConfig;
    webRequest.webConfig = webConfig;
    m_webChatManager->sendWebChat(webRequest, [window, chatMessage](const WebChatResponse &response) {
        if (window.isNull())
        {
            return;
        }

        if (!response.success)
        {
            window->playState(QStringLiteral("error"), false);
            window->showBubbleMessage(response.errorMessage.isEmpty()
                ? QStringLiteral("联网请求失败。")
                : response.errorMessage);
            window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
            return;
        }

        window->playState(QStringLiteral("talk"), false);
        const QString displayText = window->formatWebChatResponseForDisplay(response);
        const QString historyText = window->formatWebChatResponseForHistory(response);
        if (window->m_conversationManager)
        {
            window->m_conversationManager->appendExternalExchange(chatMessage, historyText);
        }
        window->saveConversationHistoryIfNeeded();
        window->refreshChatHistoryPanel();
        window->showBubbleMessage(displayText, true);
        window->triggerLongTermMemoryExtractionIfNeeded(chatMessage, response.content);
        window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
    });
    return true;
}

bool PetWindow::hasActiveWebRequest() const
{
    return m_webChatManager && m_webChatManager->isBusy();
}

QString PetWindow::formatWebChatResponseForDisplay(const WebChatResponse &response) const
{
    QString message = stripWebStatusNoticeFooter(response.content);
    if (message.isEmpty())
    {
        message = QStringLiteral("联网请求已完成，但没有返回内容。");
    }

    WebStore webStore;
    const WebConfig webConfig = webStore.load();
    if (!webConfig.showCitations)
    {
        return message;
    }

    if (!response.citations.isEmpty())
    {
        message += QStringLiteral("\n\n**来源：**\n") + webCitationListText(response.citations);
    }
    else if (!response.usedWeb)
    {
        message += QStringLiteral("\n\n（模型未使用联网来源）");
    }
    else
    {
        message += QStringLiteral("\n\n（模型使用了联网能力，但未返回可展示来源）");
    }

    return message;
}

QString PetWindow::formatWebChatResponseForHistory(const WebChatResponse &response) const
{
    QString message = stripWebStatusNoticeFooter(response.content);
    if (message.isEmpty())
    {
        message = QStringLiteral("联网请求已完成，但没有返回内容。");
    }

    WebStore webStore;
    const WebConfig webConfig = webStore.load();
    if (webConfig.showCitations && !response.citations.isEmpty())
    {
        message += QStringLiteral("\n\n**来源：**\n") + webCitationListText(response.citations);
    }

    return message;
}

void PetWindow::refreshChatInputWebToggle()
{
    if (!m_chatInputDialog)
    {
        return;
    }

    WebStore webStore;
    QString errorMessage;
    const WebConfig webConfig = webStore.load(&errorMessage);
    if (!errorMessage.isEmpty())
    {
        Logger::warning(QStringLiteral("Web config load warning: ") + errorMessage);
    }

    ConfigManager configManager;
    const AIConfig aiConfig = configManager.loadAIConfig();
    const WebCapability capability = WebCapabilityDetector::detect(aiConfig, webConfig);
    const bool available = webConfig.enabled;
    m_chatInputDialog->setWebToggleAvailable(available, capability.userMessage);
    const bool checked = webConfig.rememberLastToggle ? webConfig.lastToggleOn : webConfig.defaultToggleOn;
    m_chatInputDialog->setWebEnabled(available && checked);
}

void PetWindow::saveWebTogglePreference(bool webEnabled)
{
    WebStore webStore;
    QString loadError;
    WebConfig webConfig = webStore.load(&loadError);
    if (!loadError.isEmpty())
    {
        Logger::warning(QStringLiteral("Web config load warning: ") + loadError);
    }

    if (!webConfig.rememberLastToggle)
    {
        return;
    }

    webConfig.lastToggleOn = webEnabled;
    QString saveError;
    if (!webStore.save(webConfig, &saveError))
    {
        Logger::warning(QStringLiteral("Failed to save web toggle preference: ") + saveError);
    }
}

void PetWindow::handleTriggeredReminder(const ReminderItem &item)
{
    playReminderSound();

    if (shouldNotifyOnlyForReminder())
    {
        showReminderNotification(item);
        return;
    }

    enqueueVisibleTriggeredReminder(item);
}

void PetWindow::playReminderSound()
{
    if (m_appConfig.reminderSoundEnabled && m_reminderSoundPlayer)
    {
        m_reminderSoundPlayer->play(m_appConfig.reminderSoundId, m_appConfig.reminderSoundVolume);
    }
}

void PetWindow::showReminderNotification(const ReminderItem &item)
{
    if (m_notificationDispatcher)
    {
        const bool shown = m_notificationDispatcher->showReminder(QStringLiteral("定时提醒"), QStringLiteral("到时间啦：%1").arg(item.title));
        if (!shown)
        {
            Logger::warning(QStringLiteral("Reminder notification backend unavailable: id=%1").arg(item.id));
        }
        return;
    }

    Logger::warning(QStringLiteral("Reminder notification dispatcher is unavailable: id=%1").arg(item.id));
}

bool PetWindow::shouldNotifyOnlyForReminder() const
{
    return !isVisible() || hasActiveAIRequest() || m_streamingChatActive;
}

void PetWindow::enqueueVisibleTriggeredReminder(const ReminderItem &item)
{
    m_pendingVisibleTriggeredReminders.append(item);
    showNextTriggeredReminder();
}

void PetWindow::showNextTriggeredReminder()
{
    if (m_hasActiveTriggeredReminder || m_dragging || m_pendingVisibleTriggeredReminders.isEmpty())
    {
        return;
    }

    const ReminderItem item = m_pendingVisibleTriggeredReminders.takeFirst();
    showTriggeredReminder(item);
}

void PetWindow::finishActiveTriggeredReminder(bool hideBubble)
{
    hideReminderActions();
    m_hasActiveTriggeredReminder = false;
    if (hideBubble && m_chatBubble)
    {
        m_chatBubble->hideBubble();
    }

    showNextTriggeredReminder();
}

void PetWindow::showTriggeredReminder(const ReminderItem &item)
{
    const QString reminderState = m_clips.contains(QStringLiteral("happy"))
        ? QStringLiteral("happy")
        : QStringLiteral("talk");
    playState(reminderState, false);
    hideReminderActions();
    m_chatBubble->showMessage(QStringLiteral("到时间啦：%1").arg(item.title), bubbleAnchorPosition(), 0);
    showReminderActions(item);
}

void PetWindow::ensureReminderActionPanel()
{
    if (m_reminderActionPanel)
    {
        return;
    }

    auto *panel = new QFrame();
    panel->setObjectName(QStringLiteral("ReminderActionPanel"));
    panel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    panel->setAttribute(Qt::WA_TranslucentBackground);
    panel->setAttribute(Qt::WA_ShowWithoutActivating);
    panel->setStyleSheet(QStringLiteral(
        "QFrame#ReminderActionPanel {"
        "background: #ffffff;"
        "border: 1px solid #c7cdd4;"
        "border-radius: 8px;"
        "}"
        "QPushButton {"
        "background: #f1f4f7;"
        "border: 1px solid #aeb6bf;"
        "border-radius: 6px;"
        "padding: 7px 12px;"
        "color: #202124;"
        "}"
        "QPushButton:hover {"
        "background: #e4e9ee;"
        "}"
        "QPushButton:pressed {"
        "background: #d5dce3;"
        "}"));

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *dismissButton = new QPushButton(QStringLiteral("知道了"), panel);
    auto *snoozeButton = new QPushButton(QStringLiteral("5分钟后再提醒"), panel);
    layout->addWidget(dismissButton);
    layout->addWidget(snoozeButton);

    connect(dismissButton, &QPushButton::clicked, this, [this]() {
        finishActiveTriggeredReminder(true);
    });

    connect(snoozeButton, &QPushButton::clicked, this, [this]() {
        if (!m_hasActiveTriggeredReminder)
        {
            finishActiveTriggeredReminder(true);
            return;
        }

        const ReminderItem item = m_activeTriggeredReminder;
        hideReminderActions();
        snoozeTriggeredReminder(item);
    });

    m_reminderActionPanel.reset(panel);
}

void PetWindow::showReminderActions(const ReminderItem &item)
{
    m_activeTriggeredReminder = item;
    m_hasActiveTriggeredReminder = true;
    ensureReminderActionPanel();
    if (!m_reminderActionPanel)
    {
        return;
    }

    m_reminderActionPanel->adjustSize();
    updateReminderActionPosition();
    m_reminderActionPanel->show();
    m_reminderActionPanel->raise();
}

void PetWindow::hideReminderActions()
{
    m_hasActiveTriggeredReminder = false;
    if (m_reminderActionPanel)
    {
        m_reminderActionPanel->hide();
    }
}

void PetWindow::updateReminderActionPosition()
{
    if (!m_reminderActionPanel)
    {
        return;
    }

    m_reminderActionPanel->adjustSize();
    const QSize panelSize = m_reminderActionPanel->sizeHint();
    const QRect petGeometry = visiblePetGeometry();
    const int petReferenceSide = qMin(petGeometry.width(), petGeometry.height());
    const int panelSideSpacing = qBound(4, qRound(static_cast<double>(petReferenceSide) * 0.015), 10);

    QPoint position(
        petGeometry.right() + 1 + panelSideSpacing,
        petGeometry.center().y() - panelSize.height() / 2);

    if (QScreen *screen = screenForPopup(this))
    {
        const QRect availableGeometry = screen->availableGeometry();
        const int maxX = qMax(availableGeometry.left(), availableGeometry.right() - panelSize.width() + 1);
        const int maxY = qMax(availableGeometry.top(), availableGeometry.bottom() - panelSize.height() + 1);
        if (position.x() > maxX)
        {
            position.setX(petGeometry.left() - panelSize.width() - panelSideSpacing);
        }

        position.setX(qBound(
            availableGeometry.left(),
            position.x(),
            maxX));
        position.setY(qBound(
            availableGeometry.top(),
            position.y(),
            maxY));
    }

    m_reminderActionPanel->move(position);
}

void PetWindow::snoozeTriggeredReminder(const ReminderItem &item)
{
    m_hasActiveTriggeredReminder = false;
    if (!m_reminderManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("提醒功能初始化失败。"));
        QTimer::singleShot(1200, this, [this]() {
            showNextTriggeredReminder();
        });
        return;
    }

    ReminderItem snoozedItem;
    QString errorMessage;
    if (!m_reminderManager->snoozeReminder(item, 5, &snoozedItem, &errorMessage))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(errorMessage.isEmpty() ? QStringLiteral("创建稍后提醒失败。") : errorMessage);
        QTimer::singleShot(1200, this, [this]() {
            showNextTriggeredReminder();
        });
        return;
    }

    playState(QStringLiteral("talk"), false);
    showBubbleMessage(QStringLiteral("已延后提醒：%1，时间：%2").arg(snoozedItem.title, reminderDisplayTime(snoozedItem.remindAt)));
    QTimer::singleShot(1200, this, [this]() {
        showNextTriggeredReminder();
    });
}

bool PetWindow::submitAiChatMessage(const QString &message)
{
    if (hasActiveWebRequest())
    {
        showPendingRequestBubble(QStringLiteral("当前联网请求正在进行，请稍后。"));
        return false;
    }

    if (!m_conversationManager)
    {
        showBubbleMessage(QStringLiteral("AI 对话功能初始化失败。"));
        return false;
    }

    if (m_conversationManager->isBusy())
    {
        showPendingRequestBubble(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    const QString chatMessage = message.trimmed();
    if (chatMessage.isEmpty())
    {
        return false;
    }

    ConfigManager configManager;
    AIConfig config = configManager.loadAIConfig();
    QString errorMessage;
    if (!AIProviderFactory::prepareRuntimeConfig(config, &errorMessage))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(errorMessage);
        return false;
    }

    std::unique_ptr<LLMProvider> provider = AIProviderFactory::createProvider(config);
    if (!provider)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("当前 Provider 协议暂未接入。"));
        return false;
    }

    m_conversationManager->setConversationMetadata(config.provider, config.model);
    if (!m_conversationManager->setProvider(std::move(provider)))
    {
        showPendingRequestBubble(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    stopAnimationPrewarm();
    m_streamingAssistantText.clear();
    m_streamBubbleUpdateTimer.stop();
    m_streamingChatActive = true;
    m_streamingTalkStarted = false;
    m_behaviorReturnTimer.stop();
    playState(QStringLiteral("think"), false);
    hideReminderActions();
    showPendingRequestBubble(QStringLiteral("正在思考..."));

    QPointer<PetWindow> window(this);
    m_conversationManager->sendUserMessageStreaming(
        chatMessage,
        [window](const QString &delta) {
            if (!window.isNull())
            {
                window->handleChatStreamDelta(delta);
            }
        },
        [window, chatMessage](const ChatResponse &response) {
            if (window.isNull())
            {
                return;
            }

            window->m_streamBubbleUpdateTimer.stop();
            if (response.success)
            {
                const bool shouldReturnToIdleAfterChat =
                    window->m_streamingTalkStarted
                    || window->m_stateMachine.currentState() == QStringLiteral("think")
                    || window->m_stateMachine.currentState() == QStringLiteral("talk")
                    || window->m_frameAnimator.currentStateName() == QStringLiteral("think")
                    || window->m_frameAnimator.currentStateName() == QStringLiteral("talk");
                window->finishStreamingChat();
                window->m_streamingAssistantText = response.content;
                window->flushStreamingBubble(true);
                window->saveConversationHistoryIfNeeded();
                window->refreshChatHistoryPanel();
                window->triggerLongTermMemoryExtractionIfNeeded(chatMessage, response.content);
                if (shouldReturnToIdleAfterChat)
                {
                    window->m_behaviorReturnTimer.start(ChatFinishedReturnDelayMs);
                }
                return;
            }

            window->cancelStreamingChat();
            window->m_streamingAssistantText.clear();
            window->playState(QStringLiteral("error"), false);
            window->showBubbleMessage(QStringLiteral("AI 回复失败：") + userVisibleErrorMessage(response));
        });

    return true;
}

void PetWindow::clearConversation()
{
    if (!m_conversationManager)
    {
        return;
    }

    const bool hadActiveRequest = hasActiveAIRequest();
    m_conversationManager->clear();
    if (m_conversationStore && !m_conversationStore->clear())
    {
        Logger::warning(QStringLiteral("Failed to clear persisted conversation history."));
    }
    cancelStreamingChat();
    if (m_webChatManager && m_webChatManager->isBusy())
    {
        m_webChatManager->cancel();
    }
    if (m_longTermMemoryExtractor && m_longTermMemoryExtractor->isBusy())
    {
        m_longTermMemoryExtractor->cancel();
    }
    m_explicitMemoryExtractionActive = false;
    refreshChatHistoryPanel();
    showBubbleMessage(hadActiveRequest
        ? QStringLiteral("已取消当前请求，并清空对话。")
        : QStringLiteral("对话已清空。"));
    playState(QStringLiteral("idle"), false);
}

void PetWindow::cancelActiveAIRequest()
{
    if (m_conversationManager && m_conversationManager->isBusy())
    {
        m_conversationManager->cancel();
        cancelStreamingChat();
        showBubbleMessage(QStringLiteral("AI 请求已取消。"));
        playState(QStringLiteral("idle"), false);
        return;
    }

    if (m_webChatManager && m_webChatManager->isBusy())
    {
        m_webChatManager->cancel();
        showBubbleMessage(QStringLiteral("联网请求已取消。"));
        playState(QStringLiteral("idle"), false);
        return;
    }

    if (m_explicitMemoryExtractionActive && m_longTermMemoryExtractor && m_longTermMemoryExtractor->isBusy())
    {
        m_longTermMemoryExtractor->cancel();
        m_explicitMemoryExtractionActive = false;
        showBubbleMessage(QStringLiteral("长期记忆提炼已取消。"));
        playState(QStringLiteral("idle"), false);
        return;
    }

    showBubbleMessage(QStringLiteral("没有正在进行的 AI 或联网请求。"));
}

bool PetWindow::hasActiveAIRequest() const
{
    return (m_conversationManager && m_conversationManager->isBusy())
        || hasActiveWebRequest()
        || (m_explicitMemoryExtractionActive && m_longTermMemoryExtractor && m_longTermMemoryExtractor->isBusy());
}

bool PetWindow::isManualStateSwitchLocked() const
{
    return !pendingLocalAnimationState().isEmpty()
        || (m_appConfig.enableAgentBridge && m_hasWorkingExternalSession);
}

QString PetWindow::pendingLocalAnimationState() const
{
    if (m_streamingChatActive)
    {
        return m_streamingTalkStarted ? QStringLiteral("talk") : QStringLiteral("think");
    }
    if (hasActiveAIRequest() || (m_weatherManager && m_weatherManager->isBusy()))
    {
        return QStringLiteral("think");
    }
    return {};
}

void PetWindow::showConversationHistory()
{
    const QVector<ChatMessage> history = m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>();
    const int prunedCount = m_conversationManager ? m_conversationManager->prunedHistoryMessageCount() : 0;
    m_chatHistoryPanel->setMessages(history, prunedCount);
    m_chatHistoryPanel->showNear(frameGeometry());
}

void PetWindow::refreshChatHistoryPanel()
{
    if (!m_chatHistoryPanel || !m_chatHistoryPanel->isVisible())
    {
        return;
    }

    const QVector<ChatMessage> history = m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>();
    const int prunedCount = m_conversationManager ? m_conversationManager->prunedHistoryMessageCount() : 0;
    m_chatHistoryPanel->setMessages(history, prunedCount);
}

void PetWindow::configureConversation(bool loadPersistedHistory)
{
    if (!m_conversationManager)
    {
        return;
    }

    m_conversationManager->setRequestContextMessageLimit(m_appConfig.requestContextMessageLimit);
    m_conversationManager->setRequestContextUseMax(m_appConfig.requestContextUseMax);
    m_conversationManager->setRequestContextMaxChars(m_appConfig.requestContextMaxChars);
    m_conversationManager->setHistoryRecallEnabled(m_appConfig.enableHistoryRecall);
    m_conversationManager->setMemoryHistoryMessageLimit(m_appConfig.memoryHistoryMessageLimit);
    m_conversationManager->setLongTermMemoryEnabled(m_appConfig.enableLongTermMemory);

    if (m_longTermMemoryStore)
    {
        LongTermMemoryStore *memoryStore = m_longTermMemoryStore.get();
        m_conversationManager->setLongTermMemoryLoader([memoryStore]() {
            return memoryStore->load();
        });
    }
    else
    {
        m_conversationManager->setLongTermMemoryLoader({});
    }

    if (m_appConfig.saveConversationHistory && m_conversationStore)
    {
        ConversationStore *store = m_conversationStore.get();
        m_conversationManager->setArchiveHistoryLoader([store]() {
            return store->loadAll();
        });
    }
    else
    {
        m_conversationManager->setArchiveHistoryLoader({});
    }

    if (loadPersistedHistory)
    {
        loadConversationHistoryIfNeeded();
    }

    saveConversationHistoryIfNeeded();
    refreshChatHistoryPanel();
    refreshActiveCharacterPersona();
}

void PetWindow::loadConversationHistoryIfNeeded()
{
    if (!m_appConfig.saveConversationHistory || !m_conversationManager || !m_conversationStore)
    {
        return;
    }

    if (m_conversationManager->hasHistory())
    {
        return;
    }

    QString loadError;
    const int loadLimit = m_appConfig.saveAllConversationHistory
        ? qMax(2, m_appConfig.memoryHistoryMessageLimit)
        : qMin(qMax(2, m_appConfig.memoryHistoryMessageLimit), qMax(2, m_appConfig.savedHistoryMessageLimit));
    const QVector<ChatMessage> history = m_conversationStore->load(loadLimit, &loadError);
    if (!loadError.isEmpty())
    {
        Logger::warning(QStringLiteral("Failed to load conversation history: ") + loadError);
    }

    if (!history.isEmpty())
    {
        m_conversationManager->setHistory(history);
    }
}

void PetWindow::saveConversationHistoryIfNeeded()
{
    if (!m_appConfig.saveConversationHistory || !m_conversationManager || !m_conversationStore)
    {
        return;
    }

    if (!m_conversationManager->hasHistory())
    {
        return;
    }

    const QVector<ChatMessage> history = m_conversationManager->history();
    const bool saved = m_appConfig.saveAllConversationHistory
        ? m_conversationStore->saveAll(history)
        : m_conversationStore->save(history, m_appConfig.savedHistoryMessageLimit);
    if (!saved)
    {
        Logger::warning(QStringLiteral("Failed to save conversation history."));
    }
}

void PetWindow::handleChatStreamDelta(const QString &delta)
{
    if (delta.isEmpty())
    {
        return;
    }

    m_streamingAssistantText += delta;
    if (m_streamingChatActive && !m_streamingTalkStarted && !m_streamingAssistantText.trimmed().isEmpty())
    {
        m_streamingTalkStarted = true;
        m_behaviorReturnTimer.stop();
        playState(QStringLiteral("talk"), false);
        if (m_stateMachine.currentState() == QStringLiteral("talk"))
        {
            m_returnToIdleAfterResume = false;
        }
    }

    if (!isVisible())
    {
        return;
    }

    if (!m_streamBubbleUpdateTimer.isActive())
    {
        m_streamBubbleUpdateTimer.start(StreamBubbleUpdateIntervalMs);
    }
}

void PetWindow::flushStreamingBubble(bool finalUpdate)
{
    if (!isVisible() || m_streamingAssistantText.trimmed().isEmpty())
    {
        return;
    }

    hideReminderActions();
    m_chatBubble->showMessage(
        m_streamingAssistantText,
        bubbleAnchorPosition(),
        finalUpdate ? 10000 : 0,
        true,
        true);
}

void PetWindow::finishStreamingChat()
{
    m_behaviorReturnTimer.stop();
    // Some providers return the entire answer only in the completion callback.
    // Keep the lease until talk has replaced think, without an idle frame.
    if (m_streamingChatActive)
    {
        m_streamingTalkStarted = true;
        playState(QStringLiteral("talk"), false);
    }
    m_streamingChatActive = false;
    m_streamingTalkStarted = false;
    syncBadgeLogicalState();
    scheduleAnimationPrewarm();
}

void PetWindow::cancelStreamingChat()
{
    m_behaviorReturnTimer.stop();
    m_streamBubbleUpdateTimer.stop();
    m_streamingAssistantText.clear();
    m_streamingChatActive = false;
    m_streamingTalkStarted = false;
    syncBadgeLogicalState();
    scheduleAnimationPrewarm();
}

void PetWindow::resetBubbleAutoHideTimer()
{
    if (m_chatBubble)
    {
        m_chatBubble->resetAutoHideTimer();
    }
}

QPoint PetWindow::chatInputAnchorPosition() const
{
    return frameGeometry().topLeft() + QPoint(width() / 2, height() / 2 + ChatInputLowerOffsetY);
}

void PetWindow::hideEvent(QHideEvent *event)
{
    m_streamBubbleUpdateTimer.stop();
    stopAnimationPrewarm();
    m_pendingTapTimer.stop();
    m_startupGreetingTimer.stop();
    m_proactiveCareTimer.stop();
    m_leftMousePressed = false;
    m_dragMovedEnough = false;
    m_ignoreNextLeftRelease = false;
    if (m_chatBubble)
    {
        m_chatBubble->hideBubble();
    }
    hideReminderActions();
    if (m_agentStatusBar)
    {
        m_agentStatusBar->hideAll();
    }
    if (m_chatInputDialog)
    {
        m_chatInputDialog->hide();
    }
    if (m_chatHistoryPanel)
    {
        m_chatHistoryPanel->hide();
    }

    if (isAnimationCacheManagementEnabled() && m_appConfig.unloadAnimationsWhenHidden)
    {
        unloadNonProtectedAnimationCache(QStringLiteral("window hidden"));
    }

    m_petView->effects().setPresented(false);
    QWidget::hideEvent(event);
}

void PetWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_petView->effects().setPresented(true);
    if (m_reminderManager)
    {
        m_reminderManager->start();
        m_reminderManager->checkDueRemindersNow();
    }
    scheduleAnimationPrewarm();
    scheduleStartupGreetingIfNeeded();
    if (m_appConfig.enableProactiveCare)
    {
        if (!m_proactiveCareTimer.isActive())
        {
            m_lastCareResetMs = m_interactionClock.elapsed();
            m_proactiveCareTimer.start(60000);
        }
    }
    else
    {
        m_proactiveCareTimer.stop();
    }

    // Also settle a lease released while hidden, including clear/dismiss paths
    // that have no remaining sessions to keep the periodic prune timer alive.
    syncExternalAgentPresentation(m_agentSessionRegistry.arbitrate(m_interactionClock.elapsed()), nullptr, false);
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    resetBubbleAutoHideTimer();
    if (event->button() == Qt::LeftButton)
    {
        m_pendingTapTimer.stop();
        m_leftMousePressed = false;
        m_dragMovedEnough = false;
        m_ignoreNextLeftRelease = true;
        if (m_dragging)
        {
            m_dragging = false;
            m_petView->effects().endDrag();
            playResolvedState(m_stateMachine.endDrag(), false);
        }
        startChat();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_leftMousePressed && (event->buttons() & Qt::LeftButton))
    {
        const QPoint currentGlobalPos = event->globalPosition().toPoint();
        const int distance = (currentGlobalPos - m_leftPressGlobalPos).manhattanLength();
        const int dragThreshold = qMax(6, QApplication::startDragDistance());
        if (!m_dragging && distance >= dragThreshold)
        {
            m_dragging = true;
            m_pendingTapTimer.stop();
            stopAnimationPrewarm();
            playResolvedState(m_stateMachine.beginDrag(), false);
        }

        if (m_dragging)
        {
            if (distance >= PetDragFeedbackMinDistancePx)
            {
                m_dragMovedEnough = true;
            }
            resetBubbleAutoHideTimer();
            m_petView->effects().dragTo(currentGlobalPos.x());
            move(currentGlobalPos - m_dragOffset);
            updateBubblePosition();
        }

        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void PetWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    updateBubblePosition();
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    resetBubbleAutoHideTimer();

    if (event->button() == Qt::LeftButton)
    {
        m_pendingTapTimer.stop();
        m_leftMousePressed = true;
        m_dragging = false;
        m_dragMovedEnough = false;
        m_leftPressGlobalPos = event->globalPosition().toPoint();
        m_dragOffset = m_leftPressGlobalPos - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    resetBubbleAutoHideTimer();

    if (event->button() == Qt::LeftButton)
    {
        if (m_ignoreNextLeftRelease)
        {
            m_ignoreNextLeftRelease = false;
            m_leftMousePressed = false;
            m_dragging = false;
            m_dragMovedEnough = false;
            event->accept();
            return;
        }

        const bool wasDragging = m_dragging;
        const bool shouldGiveDragFeedback = wasDragging && m_dragMovedEnough;
        const bool wasLeftPressed = m_leftMousePressed;

        m_dragging = false;
        m_leftMousePressed = false;
        m_dragMovedEnough = false;

        if (wasDragging)
        {
            m_petView->effects().endDrag();
            playResolvedState(m_stateMachine.endDrag(), false);
            if (shouldHoldExternalWorkingState() && m_clips.contains(QStringLiteral("think")))
            {
                playResolvedState(m_stateMachine.requestState(QStringLiteral("think"), StateRequestSource::Automatic), false);
            }
            scheduleAnimationPrewarm();
            showNextTriggeredReminder();
            if (shouldGiveDragFeedback)
            {
                triggerDragDropInteraction();
            }
            event->accept();
            return;
        }

        if (wasLeftPressed && m_appConfig.enableTouchInteraction && !isMicroInteractionBusy())
        {
            m_pendingTapGlobalPos = event->globalPosition().toPoint();
            const int tapDelayMs = qBound(180, QApplication::doubleClickInterval() / 2, 260);
            m_pendingTapTimer.start(tapDelayMs);
        }

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PetWindow::loadInitialImage()
{
    loadCharacterPackage(m_appConfig.characterId, true);
}

bool PetWindow::loadCharacterPackage(const QString &characterId, bool centerWindow)
{
    m_badgeFallbackTimer.stop();
    m_fallbackBadgeState.clear();
    const QString requestedCharacterId = CharacterPackageRepository::hasPackage(characterId)
        ? characterId
        : CharacterPackageRepository::defaultCharacterId();
    QString loadError;
    CharacterPackage package = CharacterPackageLoader::load(CharacterPackageRepository::packagePath(requestedCharacterId), &loadError);
    if (!loadError.isEmpty() && requestedCharacterId != CharacterPackageRepository::defaultCharacterId())
    {
        Logger::warning(QStringLiteral("Character package load failed: id=%1 error=%2")
            .arg(requestedCharacterId, loadError));
        loadError.clear();
        package = CharacterPackageLoader::load(CharacterPackageRepository::defaultPackagePath(), &loadError);
        m_appConfig.characterId = CharacterPackageRepository::defaultCharacterId();
    }

    if (!loadError.isEmpty())
    {
        Logger::warning(QStringLiteral("Character package load failed: ") + loadError);
    }

    m_characterPackage = package;
    m_dialogueProvider.resetHistory();
    refreshActiveCharacterPersona();
    buildAnimationClips();

    if (m_clips.contains(QStringLiteral("idle")))
    {
        playResolvedState(m_stateMachine.start(), centerWindow);
        syncBadgeLogicalState();
        return true;
    }

    setDisplayImage(CharacterPackageRepository::previewPath(m_appConfig.characterId), centerWindow);
    syncBadgeLogicalState();
    return !m_characterPackage.states.isEmpty();
}

void PetWindow::refreshActiveCharacterPersona()
{
    if (!m_conversationManager)
    {
        return;
    }

    QString activeId = m_characterPackage.id.trimmed();
    if (activeId.isEmpty())
    {
        activeId = m_appConfig.characterId.trimmed();
    }

    m_conversationManager->setCurrentCharacterId(activeId);
    const CharacterPersonaStore personaStore(ConfigManager().characterPersonasPath());
    const ResolvedCharacterPersona resolved = personaStore.resolvePersona(activeId, m_characterPackage.persona);
    m_conversationManager->setCharacterPersona(resolved.persona);
}

void PetWindow::buildAnimationClips()
{
    m_frameAnimator.stop(); // Release async players before replacing their clips.
    stopAnimationPrewarm();
    m_animationPrewarmQueue.clear();
    m_animationPrewarmAttemptedStates.clear();
    m_clipLastAccessSerial.clear();
    m_clipAccessSerial = 0;
    m_clips.clear();
    m_petView->configureEffects(m_characterPackage.effects, m_appConfig.enableCharacterEffects, isLowPowerMode());
    m_petView->effects().setPresented(isVisible());

    QSet<QString> availableStates;
    const QSize targetSize = animationTargetSize();
    const bool loadFramesImmediately = !m_appConfig.enableLazyLoad;
    for (auto iterator = m_characterPackage.states.constBegin(); iterator != m_characterPackage.states.constEnd(); ++iterator)
    {
        AnimationClip clip = AnimationClip::fromState(iterator.value(), targetSize, loadFramesImmediately);
        clip.fps = effectiveAnimationFps(clip.fps);
        if (!clip.isValid())
        {
            continue;
        }

        availableStates.insert(iterator.key());
        m_clips.insert(iterator.key(), clip);
    }

    m_stateMachine.setAvailableStates(availableStates);
}

void PetWindow::addStateTestActions(QMenu *menu)
{
    QMenu *stateMenu = menu->addMenu(QStringLiteral("State Test"));
    const bool stateSwitchLocked = isManualStateSwitchLocked();
    const QStringList stateNames = {
        QStringLiteral("idle"),
        QStringLiteral("talk"),
        QStringLiteral("think"),
        QStringLiteral("sleep"),
        QStringLiteral("happy"),
        QStringLiteral("error"),
        QStringLiteral("drag"),
    };

    for (const QString &stateName : stateNames)
    {
        if (!m_clips.contains(stateName))
        {
            continue;
        }

        QAction *stateAction = stateMenu->addAction(stateName);
        stateAction->setData(stateName);
    }

    stateMenu->setEnabled(!stateSwitchLocked && !stateMenu->actions().isEmpty());
}

void PetWindow::updateBubblePosition()
{
    m_chatBubble->updateAnchorPosition(bubbleAnchorPosition());
    updateReminderActionPosition();
    updateAgentStatusBarPosition();
}

QPoint PetWindow::bubbleAnchorPosition() const
{
    const CharacterBase &base = m_characterPackage.base;
    const CharacterBubble &bubble = m_characterPackage.bubble;
    const QRectF content = m_petView->contentRect();
    const double baseWidth = base.width > 0 ? static_cast<double>(base.width) : static_cast<double>(width());
    const double baseHeight = base.height > 0 ? static_cast<double>(base.height) : static_cast<double>(height());
    const double scaleX = baseWidth > 0.0 ? content.width() / baseWidth : 1.0;
    const double scaleY = baseHeight > 0.0 ? content.height() / baseHeight : 1.0;
    const QPointF localAnchor(
        content.left() + content.width() * base.anchorX + bubble.offsetX * scaleX,
        content.top() + content.height() * base.anchorY + bubble.offsetY * scaleY);

    return frameGeometry().topLeft() + QPoint(qRound(localAnchor.x()), qRound(localAnchor.y()));
}

QRect PetWindow::visiblePetGeometry() const
{
    if (m_petView == nullptr)
    {
        return frameGeometry();
    }

    const QPixmap pixmap = m_petView->pixmap();
    if (pixmap.isNull())
    {
        return frameGeometry();
    }

    if (m_visiblePetRectPixmapKey != pixmap.cacheKey())
    {
        m_visiblePetRectPixmapKey = pixmap.cacheKey();
        m_visiblePetRectCache = visiblePixmapRect(pixmap);
    }

    if (!m_visiblePetRectCache.isValid())
    {
        return frameGeometry();
    }

    const double dpr = pixmap.devicePixelRatio();
    const QRectF logical(m_visiblePetRectCache.x() / dpr, m_visiblePetRectCache.y() / dpr,
                         m_visiblePetRectCache.width() / dpr, m_visiblePetRectCache.height() / dpr);
    return logical.translated(m_petView->contentRect().topLeft() + frameGeometry().topLeft()).toAlignedRect();
}

void PetWindow::playState(const QString &stateName, bool centerWindow)
{
    updateFallbackBadgeForRequest(stateName);
    playResolvedState(m_stateMachine.requestState(stateName), centerWindow);
    syncBadgeLogicalState();
}

void PetWindow::updateFallbackBadgeForRequest(const QString &requestedState)
{
    const bool isBadgeState = requestedState == QStringLiteral("think")
        || requestedState == QStringLiteral("sleep")
        || requestedState == QStringLiteral("error");
    if (!isBadgeState || m_clips.contains(requestedState))
    {
        m_badgeFallbackTimer.stop();
        m_fallbackBadgeState.clear();
        return;
    }

    m_fallbackBadgeState = requestedState;
    if (requestedState == QStringLiteral("think") && pendingLocalAnimationState() == QStringLiteral("think"))
    {
        m_badgeFallbackTimer.stop();
        return;
    }

    m_badgeFallbackTimer.start(requestedState == QStringLiteral("error") ? 3000 : 4000);
}

void PetWindow::syncBadgeLogicalState()
{
    if (!m_petView)
    {
        return;
    }

    const QString animatorState = m_frameAnimator.currentStateName();
    if (animatorState == QStringLiteral("think")
        || animatorState == QStringLiteral("sleep")
        || animatorState == QStringLiteral("error"))
    {
        m_petView->effects().setBadgeState(QString());
        return;
    }

    if ((pendingLocalAnimationState() == QStringLiteral("think")
         || (!hasBlockingLocalActivityForAgent() && isVisible() && m_hasWorkingExternalSession))
        && !m_clips.contains(QStringLiteral("think")))
    {
        m_petView->effects().setBadgeState(QStringLiteral("think"));
        return;
    }

    if (!m_fallbackBadgeState.isEmpty() && !m_clips.contains(m_fallbackBadgeState))
    {
        m_petView->effects().setBadgeState(m_fallbackBadgeState);
        return;
    }

    m_petView->effects().setBadgeState(QString());
}

void PetWindow::playResolvedState(const QString &stateName, bool centerWindow)
{
    if (stateName.isEmpty())
    {
        return;
    }

    const QString preferredHeldState = pendingLocalAnimationState();
    if (!preferredHeldState.isEmpty()
        && stateName != QStringLiteral("error")
        && stateName != QStringLiteral("drag"))
    {
        m_behaviorReturnTimer.stop();
        const QString heldState = m_stateMachine.requestState(preferredHeldState, StateRequestSource::System);

        if (stateName != heldState)
        {
            if (!heldState.isEmpty() && m_clips.contains(heldState))
            {
                playResolvedState(heldState, centerWindow);
            }
            return;
        }
    }

    if (stateName == QStringLiteral("think") && shouldHoldExternalWorkingState())
    {
        m_externalThinkOwned = true;
        m_behaviorReturnTimer.stop();
    }

    if (m_frameAnimator.currentStateName() == stateName && m_frameAnimator.isPlaying())
    {
        noteAnimationClipAccess(stateName);
        return;
    }

    auto clipIterator = m_clips.find(stateName);
    if (clipIterator == m_clips.end())
    {
        return;
    }

    AnimationClip *clip = &clipIterator.value();
    const bool wasLoaded = clip->isLoaded();
    if (!clip->ensureLoaded())
    {
        const QString errorReason = clip->errorString();
        m_petView->effects().setState(QStringLiteral("error"));
        Logger::warning(QStringLiteral("Animation state failed to load: state=%1 reason=%2").arg(stateName, errorReason));
        m_frameAnimator.stop();
        if (stateName == QStringLiteral("idle"))
        {
            const QString previewPath = !m_characterPackage.previewPath.isEmpty()
                ? m_characterPackage.previewPath
                : CharacterPackageRepository::previewPath(m_appConfig.characterId);
            setDisplayImage(previewPath, centerWindow);
        }
        if (!errorReason.isEmpty())
        {
            showBubbleMessage(QStringLiteral("动画加载失败：") + errorReason);
        }
        if (stateName != QStringLiteral("idle"))
        {
            const QString fallbackState = m_stateMachine.requestState(QStringLiteral("idle"), StateRequestSource::System);
            if (!fallbackState.isEmpty() && fallbackState != stateName)
            {
                playResolvedState(fallbackState, false);
            }
        }
        return;
    }

    noteAnimationClipAccess(stateName);
    if (!wasLoaded)
    {
        Logger::info(QStringLiteral("Animation state loaded: state=%1 frames=%2 cacheMb=%3")
            .arg(stateName)
            .arg(QString::number(clip->loadedFrameCount()))
            .arg(megabytesText(clip->estimatedMemoryBytes())));
    }

    m_idleBehaviorTimer.stop();
    m_behaviorReturnTimer.stop();
    m_centerNextFrame = centerWindow;
    m_petView->effects().setState(stateName);
    m_frameAnimator.play(clip);
    if (m_frameAnimator.currentStateName() != stateName)
        return; // A decoder error callback may already have selected a fallback.

    if (stateName == QStringLiteral("idle"))
    {
        scheduleIdleBehavior();
    }
    else if (clip->loop)
    {
        if (!pendingLocalAnimationState().isEmpty()
            || (stateName == QStringLiteral("think") && shouldHoldExternalWorkingState()))
        {
            // Pending local requests and external work own their animation
            // until a lifecycle event releases it, rather than a fixed delay.
        }
        else
        {
            m_behaviorReturnTimer.start(4000);
        }
    }

    trimAnimationCache(QStringLiteral("state played"));
    scheduleAnimationPrewarm();
}

QSize PetWindow::animationTargetSize() const
{
    const CharacterBase &base = m_characterPackage.base;
    const double totalScale = base.scale * m_appConfig.scale;
    return QSize(
        boundedAnimationTargetSide(static_cast<double>(base.width) * totalScale),
        boundedAnimationTargetSide(static_cast<double>(base.height) * totalScale));
}

int PetWindow::effectiveAnimationFps(int fps) const
{
    if (isLowPowerMode())
    {
        return qMax(1, qMin(fps, LowPowerFpsCap));
    }

    return qMax(1, fps);
}

bool PetWindow::isLowPowerMode() const
{
    return m_appConfig.performanceMode == QStringLiteral("low-power");
}

bool PetWindow::isAnimationCacheManagementEnabled() const
{
    return m_appConfig.enableLazyLoad;
}

void PetWindow::rebuildAnimationPrewarmQueue()
{
    m_animationPrewarmQueue.clear();
    if (!isAnimationCacheManagementEnabled() || !m_appConfig.enableAnimationPrewarm)
    {
        return;
    }

    const auto appendStateIfNeeded = [this](const QString &stateName) {
        if (stateName == QStringLiteral("idle")
            || m_animationPrewarmQueue.contains(stateName)
            || m_animationPrewarmAttemptedStates.contains(stateName))
        {
            return;
        }

        const auto iterator = m_clips.constFind(stateName);
        if (iterator != m_clips.constEnd() && !iterator.value().isLoaded())
        {
            m_animationPrewarmQueue.append(stateName);
        }
    };

    appendStateIfNeeded(QStringLiteral("drag"));
    appendStateIfNeeded(QStringLiteral("think"));
    appendStateIfNeeded(QStringLiteral("talk"));

    QStringList stateNames = m_clips.keys();
    stateNames.sort();
    for (const QString &stateName : stateNames)
    {
        appendStateIfNeeded(stateName);
    }
}

void PetWindow::scheduleAnimationPrewarm()
{
    if (!isAnimationCacheManagementEnabled() || !m_appConfig.enableAnimationPrewarm)
    {
        stopAnimationPrewarm();
        return;
    }

    if (!isVisible() || m_dragging || m_streamingChatActive || hasActiveAIRequest())
    {
        stopAnimationPrewarm();
        return;
    }

    if (m_animationPrewarmQueue.isEmpty())
    {
        rebuildAnimationPrewarmQueue();
    }

    if (m_animationPrewarmQueue.isEmpty() || m_animationPrewarmTimer.isActive())
    {
        return;
    }

    m_animationPrewarmTimer.start(isLowPowerMode() ? LowPowerPrewarmIntervalMs : StandardPrewarmIntervalMs);
}

void PetWindow::stopAnimationPrewarm()
{
    m_animationPrewarmTimer.stop();
}

void PetWindow::processAnimationPrewarm()
{
    m_animationPrewarmTimer.stop();
    if (!isAnimationCacheManagementEnabled() || !m_appConfig.enableAnimationPrewarm)
    {
        return;
    }

    if (!isVisible() || m_dragging || m_streamingChatActive || hasActiveAIRequest())
    {
        return;
    }

    while (!m_animationPrewarmQueue.isEmpty())
    {
        const QString stateName = m_animationPrewarmQueue.takeFirst();
        m_animationPrewarmAttemptedStates.insert(stateName);
        auto clipIterator = m_clips.find(stateName);
        if (clipIterator == m_clips.end() || clipIterator.value().isLoaded())
        {
            continue;
        }

        AnimationClip &clip = clipIterator.value();
        if (!clip.ensureLoaded())
        {
            Logger::warning(QStringLiteral("Animation state prewarm failed: state=%1").arg(stateName));
            continue;
        }

        noteAnimationClipAccess(stateName);
        Logger::info(QStringLiteral("Animation state prewarmed: state=%1 frames=%2 cacheMb=%3")
            .arg(stateName)
            .arg(QString::number(clip.loadedFrameCount()))
            .arg(megabytesText(clip.estimatedMemoryBytes())));
        trimAnimationCache(QStringLiteral("prewarm"));
        break;
    }

    scheduleAnimationPrewarm();
}

void PetWindow::noteAnimationClipAccess(const QString &stateName)
{
    if (!m_clips.contains(stateName))
    {
        return;
    }

    ++m_clipAccessSerial;
    m_clipLastAccessSerial.insert(stateName, m_clipAccessSerial);
}

qint64 PetWindow::animationCacheLimitBytes() const
{
    return static_cast<qint64>(m_appConfig.animationCacheLimitMb) * BytesPerMegabyte;
}

qint64 PetWindow::loadedAnimationCacheBytes() const
{
    qint64 totalBytes = 0;
    for (auto iterator = m_clips.constBegin(); iterator != m_clips.constEnd(); ++iterator)
    {
        totalBytes += iterator.value().estimatedMemoryBytes();
    }

    return totalBytes;
}

int PetWindow::loadedAnimationClipCount() const
{
    int count = 0;
    for (auto iterator = m_clips.constBegin(); iterator != m_clips.constEnd(); ++iterator)
    {
        if (iterator.value().isLoaded())
        {
            ++count;
        }
    }

    return count;
}

QSet<QString> PetWindow::protectedAnimationStates() const
{
    QSet<QString> states;
    states.insert(QStringLiteral("idle"));

    const QString animatorState = m_frameAnimator.currentStateName();
    if (!animatorState.isEmpty())
    {
        states.insert(animatorState);
    }

    const QString stateMachineState = m_stateMachine.currentState();
    if (!stateMachineState.isEmpty())
    {
        states.insert(stateMachineState);
    }

    if (m_streamingChatActive || hasActiveAIRequest())
    {
        states.insert(QStringLiteral("think"));
        states.insert(QStringLiteral("talk"));
    }

    if (m_dragging)
    {
        states.insert(QStringLiteral("drag"));
    }

    return states;
}

void PetWindow::trimAnimationCache(const QString &reason)
{
    if (!isAnimationCacheManagementEnabled())
    {
        return;
    }

    qint64 totalBytes = loadedAnimationCacheBytes();
    const qint64 limitBytes = animationCacheLimitBytes();
    if (totalBytes <= limitBytes)
    {
        return;
    }

    struct UnloadCandidate
    {
        QString stateName;
        qint64 lastAccessSerial = 0;
    };

    const QSet<QString> protectedStates = protectedAnimationStates();
    QList<UnloadCandidate> candidates;
    QStringList protectedLoadedStates;
    for (auto iterator = m_clips.constBegin(); iterator != m_clips.constEnd(); ++iterator)
    {
        if (!iterator.value().isLoaded())
        {
            continue;
        }

        if (protectedStates.contains(iterator.key()))
        {
            protectedLoadedStates.append(iterator.key());
            continue;
        }

        candidates.append({iterator.key(), m_clipLastAccessSerial.value(iterator.key(), 0)});
    }

    std::sort(candidates.begin(), candidates.end(), [](const UnloadCandidate &left, const UnloadCandidate &right) {
        if (left.lastAccessSerial == right.lastAccessSerial)
        {
            return left.stateName < right.stateName;
        }

        return left.lastAccessSerial < right.lastAccessSerial;
    });

    for (const UnloadCandidate &candidate : candidates)
    {
        if (totalBytes <= limitBytes)
        {
            break;
        }

        auto clipIterator = m_clips.find(candidate.stateName);
        if (clipIterator == m_clips.end() || !clipIterator.value().isLoaded())
        {
            continue;
        }

        const qint64 freedBytes = clipIterator.value().estimatedMemoryBytes();
        clipIterator.value().unloadFrames();
        m_clipLastAccessSerial.remove(candidate.stateName);
        totalBytes -= freedBytes;
        Logger::info(QStringLiteral("Animation state unloaded: state=%1 reason=%2 freedMb=%3 cacheMb=%4 loadedStates=%5")
            .arg(candidate.stateName)
            .arg(reason)
            .arg(megabytesText(freedBytes))
            .arg(megabytesText(totalBytes))
            .arg(QString::number(loadedAnimationClipCount())));
    }

    if (totalBytes > limitBytes)
    {
        protectedLoadedStates.sort();
        Logger::warning(QStringLiteral("Animation cache remains over limit: reason=%1 cacheMb=%2 limitMb=%3 protectedStates=%4")
            .arg(reason)
            .arg(megabytesText(totalBytes))
            .arg(QString::number(m_appConfig.animationCacheLimitMb))
            .arg(protectedLoadedStates.join(QStringLiteral(","))));
    }
}

void PetWindow::unloadNonProtectedAnimationCache(const QString &reason)
{
    if (!isAnimationCacheManagementEnabled())
    {
        return;
    }

    const QSet<QString> protectedStates = protectedAnimationStates();
    for (auto iterator = m_clips.begin(); iterator != m_clips.end(); ++iterator)
    {
        if (!iterator.value().isLoaded())
        {
            continue;
        }

        if (protectedStates.contains(iterator.key()))
        {
            Logger::info(QStringLiteral("Animation state kept loaded: state=%1 reason=%2")
                .arg(iterator.key())
                .arg(reason));
            continue;
        }

        const qint64 freedBytes = iterator.value().estimatedMemoryBytes();
        iterator.value().unloadFrames();
        m_clipLastAccessSerial.remove(iterator.key());
        Logger::info(QStringLiteral("Animation state unloaded: state=%1 reason=%2 freedMb=%3 cacheMb=%4 loadedStates=%5")
            .arg(iterator.key())
            .arg(reason)
            .arg(megabytesText(freedBytes))
            .arg(megabytesText(loadedAnimationCacheBytes()))
            .arg(QString::number(loadedAnimationClipCount())));
    }
}

void PetWindow::scheduleIdleBehavior()
{
    if (!m_clips.contains(QStringLiteral("idle")))
    {
        return;
    }

    const int idleDelayMs = isLowPowerMode()
        ? QRandomGenerator::global()->bounded(180000, 300001)
        : QRandomGenerator::global()->bounded(90000, 180001);
    m_idleBehaviorTimer.start(idleDelayMs);
}

void PetWindow::playIdleBehavior()
{
    if (m_dragging || m_stateMachine.currentState() != QStringLiteral("idle"))
    {
        scheduleIdleBehavior();
        return;
    }

    if (shouldHoldExternalWorkingState() && m_clips.contains(QStringLiteral("think")))
    {
        playResolvedState(m_stateMachine.requestState(QStringLiteral("think"), StateRequestSource::Automatic), false);
        return;
    }

    QStringList candidateStates;
    const QStringList preferredStates = {
        QStringLiteral("happy"),
    };

    for (const QString &stateName : preferredStates)
    {
        if (m_clips.contains(stateName))
        {
            candidateStates.append(stateName);
        }
    }

    if (candidateStates.isEmpty())
    {
        scheduleIdleBehavior();
        return;
    }

    const int stateIndex = QRandomGenerator::global()->bounded(candidateStates.size());
    playResolvedState(m_stateMachine.requestState(candidateStates.at(stateIndex), StateRequestSource::Automatic), false);
}

void PetWindow::returnToIdleFromBehavior()
{
    const QString heldState = pendingLocalAnimationState();
    if (!heldState.isEmpty())
    {
        if (m_clips.contains(heldState))
        {
            playResolvedState(m_stateMachine.requestState(heldState, StateRequestSource::System), false);
        }
        syncBadgeLogicalState();
        return;
    }

    if (!m_dragging)
    {
        if (shouldHoldExternalWorkingState() && m_clips.contains(QStringLiteral("think")))
        {
            playResolvedState(m_stateMachine.requestState(QStringLiteral("think"), StateRequestSource::Automatic), false);
            syncBadgeLogicalState();
            return;
        }
        m_badgeFallbackTimer.stop();
        m_fallbackBadgeState.clear();
        playResolvedState(m_stateMachine.finishState(QStringLiteral("idle")), false);
        syncBadgeLogicalState();
    }
}

bool PetWindow::isMicroInteractionBusy() const
{
    if (!isVisible() || m_dragging || m_streamingChatActive || hasActiveAIRequest() || m_hasActiveTriggeredReminder)
    {
        return true;
    }

    if (m_chatInputDialog && m_chatInputDialog->isVisible())
    {
        return true;
    }

    const QString state = m_stateMachine.currentState();
    if (state == QStringLiteral("think") && shouldHoldExternalWorkingState())
    {
        return false;
    }

    return state == QStringLiteral("think")
        || state == QStringLiteral("talk")
        || state == QStringLiteral("error");
}

void PetWindow::showMicroInteractionMessage(const QString &message, int durationMs)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty() || !m_chatBubble)
    {
        return;
    }

    hideReminderActions();
    m_chatBubble->showMessage(trimmed, bubbleAnchorPosition(), durationMs, false, false);
}

void PetWindow::triggerPendingTapInteraction()
{
    triggerTouchInteraction(m_pendingTapGlobalPos);
}

void PetWindow::triggerTouchInteraction(const QPoint &globalPos)
{
    if (!m_appConfig.enableTouchInteraction || isMicroInteractionBusy())
    {
        return;
    }

    const qint64 nowMs = m_interactionClock.elapsed();
    if (!CharacterDialogueProvider::isCooldownElapsed(m_lastTouchInteractionMs, nowMs, PetTouchCooldownMs))
    {
        return;
    }

    const QPointF local = m_petView->sourcePoint(mapFromGlobal(globalPos));
    const QPointF stablePoint = local + m_petView->contentRect().topLeft() + frameGeometry().topLeft();
    const PetTouchZone zone = CharacterDialogueProvider::classifyTouchZone(visiblePetGeometry(), stablePoint.toPoint());
    if (zone == PetTouchZone::None)
    {
        return;
    }

    const CharacterDialogueScene scene = zone == PetTouchZone::Head
        ? CharacterDialogueScene::Headpat
        : CharacterDialogueScene::Tap;
    const QString line = m_dialogueProvider.pickLine(m_characterPackage.dialogues, scene);
    if (line.isEmpty())
    {
        return;
    }

    m_lastTouchInteractionMs = nowMs;
    if (m_clips.contains(QStringLiteral("happy")))
    {
        playResolvedState(m_stateMachine.requestState(QStringLiteral("happy"), StateRequestSource::Automatic), false);
    }
    showMicroInteractionMessage(line, 4500);
}

void PetWindow::triggerDragDropInteraction()
{
    if (!m_appConfig.enableTouchInteraction || isMicroInteractionBusy())
    {
        return;
    }

    const qint64 nowMs = m_interactionClock.elapsed();
    if (!CharacterDialogueProvider::isCooldownElapsed(m_lastDragDropInteractionMs, nowMs, PetDragDropCooldownMs))
    {
        return;
    }

    const QString line = m_dialogueProvider.pickLine(
        m_characterPackage.dialogues,
        CharacterDialogueScene::DragDrop);
    if (line.isEmpty())
    {
        return;
    }

    m_lastDragDropInteractionMs = nowMs;
    showMicroInteractionMessage(line, 4500);
}

void PetWindow::scheduleStartupGreetingIfNeeded()
{
    if (m_startupGreetingDone || !m_appConfig.enableStartupGreeting || !isVisible())
    {
        return;
    }

    if (!m_startupGreetingTimer.isActive())
    {
        m_startupGreetingTimer.start(1500);
    }
}

void PetWindow::tryTriggerStartupGreeting()
{
    if (m_startupGreetingDone || !m_appConfig.enableStartupGreeting || !isVisible())
    {
        return;
    }

    if (isMicroInteractionBusy())
    {
        m_startupGreetingTimer.start(10000);
        return;
    }

    const CharacterDialogueScene scene =
        CharacterDialogueProvider::greetingSceneForHour(QTime::currentTime().hour());
    const QString line = m_dialogueProvider.pickLine(m_characterPackage.dialogues, scene);
    m_startupGreetingDone = true;
    if (!line.isEmpty())
    {
        showMicroInteractionMessage(line, 5000);
    }
}

void PetWindow::checkProactiveCare()
{
    if (!m_appConfig.enableProactiveCare || !isVisible())
    {
        return;
    }

    if (isMicroInteractionBusy())
    {
        return;
    }

    const qint64 nowMs = m_interactionClock.elapsed();
    const qint64 sedentaryIntervalMs =
        static_cast<qint64>(qBound(30, m_appConfig.sedentaryReminderMinutes, 180)) * 60 * 1000;
    if (nowMs - m_lastCareResetMs >= sedentaryIntervalMs)
    {
        const QString line = m_dialogueProvider.pickLine(
            m_characterPackage.dialogues,
            CharacterDialogueScene::Sedentary);
        m_lastCareResetMs = nowMs;
        if (!line.isEmpty())
        {
            showMicroInteractionMessage(line, 5500);
            return;
        }
    }

    const int currentHour = QTime::currentTime().hour();
    if (!CharacterDialogueProvider::isLateNightHour(currentHour))
    {
        return;
    }

    const QDate today = QDate::currentDate();
    if (m_lateNightCareDate != today)
    {
        m_lateNightCareDate = today;
        m_lateNightCareCount = 0;
    }

    constexpr qint64 MinUptimeBeforeLateNightCareMs = 20 * 60 * 1000;
    constexpr qint64 LateNightCareCooldownMs = 90 * 60 * 1000;
    constexpr int MaxLateNightCarePerNight = 2;
    if (nowMs < MinUptimeBeforeLateNightCareMs
        || m_lateNightCareCount >= MaxLateNightCarePerNight
        || !CharacterDialogueProvider::isCooldownElapsed(m_lastLateNightCareMs, nowMs, LateNightCareCooldownMs))
    {
        return;
    }

    const QString line = m_dialogueProvider.pickLine(
        m_characterPackage.dialogues,
        CharacterDialogueScene::LateNightCare);
    if (!line.isEmpty())
    {
        m_lastLateNightCareMs = nowMs;
        ++m_lateNightCareCount;
        showMicroInteractionMessage(line, 5500);
    }
}

void PetWindow::configureAgentBridge()
{
    if (!m_appConfig.enableAgentBridge)
    {
        m_agentPruneTimer.stop();
        if (m_agentStatusServer)
        {
            m_agentStatusServer->stop();
        }
        clearExternalAgentSessions();
        return;
    }

    if (m_agentStatusServer && !m_agentStatusServer->isListening())
    {
        QString startError;
        if (!m_agentStatusServer->start(&startError))
        {
            Logger::warning(QStringLiteral("Agent status server failed to start: ") + startError);
        }
    }

    const qint64 nowMs = m_interactionClock.elapsed();
    const AgentArbitrationResult arbitration = m_agentSessionRegistry.arbitrate(nowMs);
    syncExternalAgentPresentation(arbitration, nullptr, false);
}

void PetWindow::handleExternalAgentEvent(const AgentStatusEvent &event)
{
    if (!m_appConfig.enableAgentBridge)
    {
        return;
    }

    const qint64 nowMs = m_interactionClock.elapsed();
    const AgentRegistryUpdateResult update = m_agentSessionRegistry.ingest(event, nowMs);
    if (!update.accepted)
    {
        return;
    }

    const bool attention = update.session.state == AgentState::NeedsInput || update.session.state == AgentState::Error;
    const bool shouldNotifyTransition = update.stateChanged && !update.debounced
        && ((update.overallStateChanged && update.session.state == update.arbitration.overallState) || attention);
    syncExternalAgentPresentation(update.arbitration, &update.session, shouldNotifyTransition);
}

void PetWindow::pruneExpiredExternalAgentSessions()
{
    const qint64 nowMs = m_interactionClock.elapsed();
    m_agentSessionRegistry.pruneExpired(nowMs);
    const AgentArbitrationResult arbitration = m_agentSessionRegistry.arbitrate(nowMs);
    syncExternalAgentPresentation(arbitration, nullptr, false);
}

void PetWindow::dismissExternalAgentSession(const QString &sessionKey)
{
    const qint64 nowMs = m_interactionClock.elapsed();
    if (!m_agentSessionRegistry.dismissSession(sessionKey, nowMs))
    {
        return;
    }

    const AgentArbitrationResult arbitration = m_agentSessionRegistry.arbitrate(nowMs);
    syncExternalAgentPresentation(arbitration, nullptr, false);
}

void PetWindow::clearExternalAgentSessions()
{
    m_agentSessionRegistry.clear();
    m_agentPruneTimer.stop();
    const AgentArbitrationResult emptyArbitration;
    syncExternalAgentPresentation(emptyArbitration, nullptr, false);
}

void PetWindow::syncExternalAgentPresentation(
    const AgentArbitrationResult &arbitration,
    const AgentSessionSnapshot *transitionSession,
    bool stateTransitioned)
{
    m_activeExternalAgentState = arbitration.hasPrimary
        ? arbitration.overallState
        : AgentState::Idle;
    m_hasWorkingExternalSession = std::any_of(
        arbitration.activeSessions.cbegin(), arbitration.activeSessions.cend(),
        [](const AgentSessionSnapshot &session) { return session.state == AgentState::Working; });

    const qint64 nowMs = m_interactionClock.elapsed();
    if (arbitration.visibleSessions.isEmpty())
    {
        m_agentPruneTimer.stop();
    }
    else if (!m_agentPruneTimer.isActive())
    {
        m_agentPruneTimer.start(1000);
    }

    if (m_agentStatusBar)
    {
        if (m_appConfig.enableAgentBridge && m_appConfig.showAgentStatusBar && isVisible() && !arbitration.visibleSessions.isEmpty())
        {
            updateAgentStatusBarPosition();
            m_agentStatusBar->setSessions(arbitration.visibleSessions, nowMs);
        }
        else
        {
            m_agentStatusBar->hideAll();
        }
    }

    m_petView->effects().setAttention(m_activeExternalAgentState == AgentState::NeedsInput
        || m_activeExternalAgentState == AgentState::Error);
    const bool localBlocked = hasBlockingLocalActivityForAgent();
    if (!localBlocked && isVisible())
    {
        if (m_hasWorkingExternalSession && m_clips.contains(QStringLiteral("think")))
        {
            playResolvedState(
                m_stateMachine.requestState(QStringLiteral("think"), StateRequestSource::Automatic),
                false);
        }
        else if (!m_hasWorkingExternalSession && m_externalThinkOwned)
        {
            // Completion releases the external lease, even after a hidden/local
            // activity deferred it. A lower-priority automatic request cannot
            // release think on its own.
            m_externalThinkOwned = false;
            if (m_stateMachine.currentState() == QStringLiteral("think"))
            {
                const QString nextState = m_activeExternalAgentState == AgentState::Done
                    ? QStringLiteral("happy")
                    : m_activeExternalAgentState == AgentState::Error
                        ? QStringLiteral("error") : QStringLiteral("idle");
                playResolvedState(m_stateMachine.requestState(nextState, StateRequestSource::System), false);
            }
        }
    }
    syncBadgeLogicalState();

    if (!stateTransitioned || transitionSession == nullptr || !m_appConfig.enableAgentBridge)
    {
        return;
    }

    const AgentState transitionedState = transitionSession->state;
    const bool isAttentionState = transitionedState == AgentState::NeedsInput
        || transitionedState == AgentState::Error;
    const bool isDoneState = transitionedState == AgentState::Done;
    const bool isWorkingState = transitionedState == AgentState::Working;
    if (!isAttentionState && !isDoneState && !isWorkingState)
    {
        return;
    }

    if (!isVisible() || localBlocked)
    {
        return;
    }

    if (isAttentionState && m_appConfig.agentAlertSoundEnabled)
    {
        playReminderSound();
    }

    if (transitionedState == AgentState::Error && m_clips.contains(QStringLiteral("error")))
    {
        playResolvedState(m_stateMachine.requestState(QStringLiteral("error"), StateRequestSource::Automatic), false);
    }
    else if (transitionedState == AgentState::Done && !m_hasWorkingExternalSession
             && m_activeExternalAgentState == AgentState::Done
             && m_clips.contains(QStringLiteral("happy")))
    {
        playResolvedState(m_stateMachine.requestState(QStringLiteral("happy"), StateRequestSource::Automatic), false);
    }

    const QString level = m_appConfig.agentBubbleNotifyLevel;
    const bool allowBubble = (level == QStringLiteral("all"))
        || (level == QStringLiteral("attention") && isAttentionState);
    if (!allowBubble)
    {
        return;
    }

    CharacterDialogueScene dialogueScene = CharacterDialogueScene::AgentWorking;
    if (transitionedState == AgentState::NeedsInput)
    {
        dialogueScene = CharacterDialogueScene::AgentNeedsInput;
    }
    else if (transitionedState == AgentState::Error)
    {
        dialogueScene = CharacterDialogueScene::AgentError;
    }
    else if (transitionedState == AgentState::Done)
    {
        dialogueScene = CharacterDialogueScene::AgentDone;
    }

    QString bubbleText = m_dialogueProvider.pickLine(m_characterPackage.dialogues, dialogueScene);
    QString detailTag = AgentStatusCodec::sourceDisplayLabel(transitionSession->source);
    if (!transitionSession->workspace.isEmpty())
    {
        detailTag += QStringLiteral(" · ") + transitionSession->workspace;
    }
    detailTag += QStringLiteral(" · ") + AgentStatusCodec::stateDisplayText(transitionedState);
    if (!transitionSession->summary.isEmpty())
    {
        detailTag += QStringLiteral("：") + transitionSession->summary;
    }

    if (!bubbleText.isEmpty())
    {
        bubbleText += QStringLiteral("\n[") + detailTag + QStringLiteral("]");
    }
    else
    {
        bubbleText = detailTag;
    }

    showMicroInteractionMessage(bubbleText, isAttentionState ? 6500 : 5000);
}

void PetWindow::updateAgentStatusBarPosition()
{
    if (m_agentStatusBar)
    {
        m_agentStatusBar->updateAnchorRect(visiblePetGeometry());
    }
}

bool PetWindow::hasBlockingLocalActivityForAgent() const
{
    return m_dragging
        || m_streamingChatActive
        || hasActiveAIRequest()
        || (m_weatherManager && m_weatherManager->isBusy())
        || m_hasActiveTriggeredReminder;
}

bool PetWindow::shouldHoldExternalWorkingState() const
{
    return m_appConfig.enableAgentBridge
        && m_hasWorkingExternalSession
        && !hasBlockingLocalActivityForAgent();
}

void PetWindow::setDisplayImage(const QString &imagePath, bool centerWindow)
{
    QPixmap pixmap(imagePath);
    if (pixmap.isNull())
    {
        m_petView->showFallbackText(QStringLiteral("QtDesktopPet"));
        resize(240, 160);
        return;
    }

    const QSize targetSize = animationTargetSize();
    const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setDisplayPixmap(scaled, centerWindow);
}

void PetWindow::setDisplayPixmap(const QPixmap &pixmap, bool centerWindow)
{
    const QSize previousSize = size();
    m_petView->setFrame(pixmap);
    if (m_petView->sizeHint() != previousSize)
    {
        resize(m_petView->sizeHint());
    }
    if (size() != previousSize)
    {
        updateBubblePosition();
    }
    else
    {
        updateAgentStatusBarPosition();
    }

    if (centerWindow)
    {
        if (const QScreen *screen = QGuiApplication::primaryScreen())
        {
            const QRect available = screen->availableGeometry();
            move(available.center() - rect().center());
        }
    }
}

bool PetWindow::isPointVisibleOnScreen(const QPoint &point) const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens)
    {
        if (screen != nullptr && screen->availableGeometry().contains(point))
        {
            return true;
        }
    }

    return false;
}

void PetWindow::setAlwaysOnTop(bool enabled)
{
    const bool currentlyOnTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    if (m_alwaysOnTop == enabled && currentlyOnTop == enabled)
    {
        return;
    }

    m_alwaysOnTop = enabled;
    const bool wasVisible = isVisible();

    Qt::WindowFlags flags = windowFlags();
    if (enabled)
    {
        flags |= Qt::WindowStaysOnTopHint;
    }
    else
    {
        flags &= ~Qt::WindowStaysOnTopHint;
    }

    setWindowFlags(flags);
    if (wasVisible)
    {
        show();
    }
}
