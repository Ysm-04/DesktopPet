#include "PetWindow.h"

#include "../ai/AIProviderFactory.h"
#include "../ai/ConversationManager.h"
#include "../ai/ConversationStore.h"
#include "../assistant/CommandDispatcher.h"
#include "../character/CharacterPackageLoader.h"
#include "../character/CharacterPackageRepository.h"
#include "../config/ConfigManager.h"
#include "../fileops/FileOperationManager.h"
#include "../launcher/AppLaunchManager.h"
#include "../launcher/AppLaunchStore.h"
#include "../notification/NotificationDispatcher.h"
#include "../reminder/ReminderCommandHandler.h"
#include "../reminder/ReminderManager.h"
#include "../reminder/ReminderSoundPlayer.h"
#include "../reminder/ReminderSoundRepository.h"
#include "../system/StartupManager.h"
#include "../util/Logger.h"
#include "../web/WebCapabilityDetector.h"
#include "../web/WebChatManager.h"
#include "../web/WebStore.h"
#include "../weather/WeatherManager.h"
#include "../weather/WeatherStore.h"
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
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHideEvent>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QCheckBox>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QShowEvent>
#include <QStringList>
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
    config.requestContextMessageLimit = qBound(0, config.requestContextMessageLimit, 200);
    config.memoryHistoryMessageLimit = evenBoundedHistoryLimit(config.memoryHistoryMessageLimit, 2, 5000);
    config.savedHistoryMessageLimit = evenBoundedHistoryLimit(config.savedHistoryMessageLimit, 2, 10000);
    config.characterId = config.characterId.trimmed();
    if (!CharacterPackageRepository::hasPackage(config.characterId))
    {
        config.characterId = CharacterPackageRepository::defaultCharacterId();
    }
    config.reminderSoundId = ReminderSoundRepository::soundInfo(config.reminderSoundId).id;
    config.reminderSoundVolume = qBound(0.0, config.reminderSoundVolume, 1.0);
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
    const QSize dialogSize = dialog->size().isValid()
        ? dialog->size()
        : dialog->sizeHint();
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
        lines.append(QStringLiteral("[%1] %2\n%3")
            .arg(index + 1)
            .arg(citation.title.trimmed().isEmpty() ? QStringLiteral("来源") : citation.title.trimmed())
            .arg(citation.url));
    }
    return lines.join(QStringLiteral("\n"));
}

}

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
    , m_chatBubble(std::make_unique<ChatBubble>())
    , m_chatHistoryPanel(std::make_unique<ChatHistoryPanel>(this))
    , m_chatInputDialog(std::make_unique<ChatInputDialog>(MaxUserMessageLength, this))
    , m_conversationManager(std::make_unique<ConversationManager>())
    , m_conversationStore(std::make_unique<ConversationStore>(ConfigManager().conversationHistoryPath()))
    , m_fileOperationManager(std::make_unique<FileOperationManager>())
    , m_appLaunchManager(std::make_unique<AppLaunchManager>())
    , m_notificationDispatcher(std::make_unique<NotificationDispatcher>())
    , m_reminderManager(std::make_unique<ReminderManager>())
    , m_reminderSoundPlayer(std::make_unique<ReminderSoundPlayer>())
    , m_webChatManager(std::make_unique<WebChatManager>())
    , m_weatherManager(std::make_unique<WeatherManager>())
    , m_petView(new PetView(this))
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

    QPointer<PetWindow> window(this);
    refreshChatInputWebToggle();
    m_chatInputDialog->setSubmitCallback([window](const QString &message, bool webEnabled) {
        return !window.isNull() && window->submitChatMessage(message, webEnabled);
    });

    m_reminderManager->setTriggeredCallback([window](const ReminderItem &item) {
        if (!window.isNull())
        {
            window->handleTriggeredReminder(item);
        }
    });

    loadInitialImage();
}

PetWindow::~PetWindow()
{
    saveConversationHistoryIfNeeded();
}

void PetWindow::applyAppConfig(const AppConfig &config)
{
    const AppConfig normalizedConfig = normalizedAppConfig(config);
    const bool characterChanged = m_appConfig.characterId != normalizedConfig.characterId;
    const bool rebuildClips = !qFuzzyCompare(m_appConfig.scale, normalizedConfig.scale)
        || m_appConfig.performanceMode != normalizedConfig.performanceMode
        || m_appConfig.enableLazyLoad != normalizedConfig.enableLazyLoad;
    const bool animationCachePolicyChanged =
        m_appConfig.enableAnimationPrewarm != normalizedConfig.enableAnimationPrewarm
        || m_appConfig.animationCacheLimitMb != normalizedConfig.animationCacheLimitMb
        || m_appConfig.unloadAnimationsWhenHidden != normalizedConfig.unloadAnimationsWhenHidden;
    const bool loadPersistedHistory = !m_appConfig.saveConversationHistory
        && normalizedConfig.saveConversationHistory;

    m_appConfig = normalizedConfig;
    setAlwaysOnTop(m_appConfig.alwaysOnTop);
    configureConversation(loadPersistedHistory);

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

void PetWindow::showBubbleMessage(const QString &message)
{
    hideReminderActions();
    m_chatBubble->showMessage(message, bubbleAnchorPosition());
}

void PetWindow::openSettingsDialog()
{
    ConfigManager configManager;
    WeatherStore weatherStore;
    WebStore webStore;
    AppLaunchStore appLaunchStore;
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
    QString appLaunchConfigError;
    const AppLaunchConfig appLaunchConfig = appLaunchStore.load(&appLaunchConfigError);
    if (!appLaunchConfigError.isEmpty())
    {
        Logger::warning(QStringLiteral("Launcher config load warning: ") + appLaunchConfigError);
    }

    SettingsDialog dialog(
        configManager.loadAIConfigStore(),
        currentAppConfig(),
        weatherConfig,
        webConfig,
        appLaunchConfig,
        m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>(),
        m_reminderManager ? m_reminderManager->allReminders() : QVector<ReminderItem>(),
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

    QString saveLauncherConfigError;
    if (!appLaunchStore.save(dialog.appLaunchConfig(), &saveLauncherConfigError))
    {
        Logger::warning(QStringLiteral("Failed to save launcher config from settings dialog: ") + saveLauncherConfigError);
    }
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
        return handleWeatherChatMessage(result.message);
    }

    if (result.action == CommandDispatchAction::FileOperation)
    {
        return handleFileOperationChatMessage(result.message);
    }

    if (result.action == CommandDispatchAction::LaunchApp)
    {
        return handleLaunchAppChatMessage(result.message);
    }

    if (result.action == CommandDispatchAction::UnsupportedTool)
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(result.message);
        return true;
    }

    saveWebTogglePreference(webEnabled);
    return webEnabled ? submitWebChatMessage(result.message) : submitAiChatMessage(result.message);
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
        showBubbleMessage(QStringLiteral("当前请求正在进行，请稍后再查天气。"));
        return false;
    }

    if (m_weatherManager->isBusy())
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("天气查询正在进行，请稍后。"));
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
    showBubbleMessage(QStringLiteral("正在查询天气..."));

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

bool PetWindow::handleFileOperationChatMessage(const QString &message)
{
    if (!m_fileOperationManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("文件操作功能初始化失败。"));
        return false;
    }

    const QString text = message.trimmed();
    const auto contains = [&text](const QString &keyword) {
        return text.contains(keyword, Qt::CaseInsensitive);
    };
    if (contains(QStringLiteral("删除")) || contains(QStringLiteral("移动")) || contains(QStringLiteral("覆盖"))
        || contains(QStringLiteral("执行")) || contains(QStringLiteral("运行")) || contains(QStringLiteral("脚本")) || contains(QStringLiteral("命令")))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("文件操作 v1 不支持删除、覆盖、移动、执行脚本或运行命令。"));
        return false;
    }

    if (contains(QStringLiteral("截图")) || contains(QStringLiteral("保存到")))
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("文件操作 v1 暂不支持截图或把当前内容保存到指定位置。"));
        return false;
    }

    if (contains(QStringLiteral("打包")) || contains(QStringLiteral("压缩")) || contains(QStringLiteral("zip")))
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("zip 打包需要额外稳定的压缩实现，本版暂不启用；可以先使用复制或备份。"));
        return false;
    }

    const auto confirmPlan = [this](const FileOperationPlan &plan) {
        QString messageText = plan.description;
        if (!plan.warnings.isEmpty())
        {
            messageText += QStringLiteral("\n\n注意：\n") + plan.warnings.join(QLatin1Char('\n'));
        }
        messageText += QStringLiteral("\n\n请确认是否执行。");
        return QMessageBox::warning(
            this,
            plan.title,
            messageText,
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Yes;
    };

    FileOperationResult operationResult;
    if (contains(QStringLiteral("列出")) || contains(QStringLiteral("目录")) || contains(QStringLiteral("文件夹")))
    {
        const QString directoryPath = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择要列出的文件夹"),
            QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (directoryPath.isEmpty())
        {
            return false;
        }

        operationResult = m_fileOperationManager->executeListDirectory(m_fileOperationManager->listDirectoryPlan(directoryPath));
    }
    else if (contains(QStringLiteral("复制")))
    {
        const QString sourceFilePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择要复制的文件"));
        if (sourceFilePath.isEmpty())
        {
            return false;
        }
        const QString targetDirectoryPath = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("选择复制到的文件夹"),
            QString(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (targetDirectoryPath.isEmpty())
        {
            return false;
        }

        const FileOperationPlan plan = m_fileOperationManager->copyFilePlan(sourceFilePath, targetDirectoryPath);
        if (!confirmPlan(plan))
        {
            return false;
        }
        operationResult = m_fileOperationManager->executeCopyFile(plan);
    }
    else if (contains(QStringLiteral("备份")))
    {
        const QString sourceFilePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择要备份的文件"));
        if (sourceFilePath.isEmpty())
        {
            return false;
        }

        const FileOperationPlan plan = m_fileOperationManager->backupFilePlan(sourceFilePath);
        if (!confirmPlan(plan))
        {
            return false;
        }
        operationResult = m_fileOperationManager->executeBackupFile(plan);
    }
    else if (contains(QStringLiteral("重命名")))
    {
        const QString sourceFilePath = QFileDialog::getOpenFileName(this, QStringLiteral("选择要重命名的文件"));
        if (sourceFilePath.isEmpty())
        {
            return false;
        }

        bool ok = false;
        const QString newFileName = QInputDialog::getText(
            this,
            QStringLiteral("重命名文件"),
            QStringLiteral("新文件名"),
            QLineEdit::Normal,
            QFileInfo(sourceFilePath).fileName(),
            &ok).trimmed();
        if (!ok || newFileName.isEmpty())
        {
            return false;
        }

        const FileOperationPlan plan = m_fileOperationManager->renameFilePlan(sourceFilePath, newFileName);
        if (!confirmPlan(plan))
        {
            return false;
        }
        operationResult = m_fileOperationManager->executeRenameFile(plan);
    }
    else
    {
        const QString sourceFilePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择要读取的文本文件"),
            QString(),
            QStringLiteral("Text Files (*.txt *.md *.markdown *.log *.json *.csv *.ini *.xml *.yaml *.yml);;All Files (*)"));
        if (sourceFilePath.isEmpty())
        {
            return false;
        }

        operationResult = m_fileOperationManager->executeReadTextFile(m_fileOperationManager->readTextFilePlan(sourceFilePath));
    }

    if (!operationResult.success)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(operationResult.errorMessage.isEmpty() ? QStringLiteral("文件操作失败。") : operationResult.errorMessage);
        return false;
    }

    playState(QStringLiteral("talk"), false);
    const QString output = operationResult.outputText.trimmed();
    showBubbleMessage(output.isEmpty() ? operationResult.message : output);
    return true;
}

bool PetWindow::handleLaunchAppChatMessage(const QString &message)
{
    if (!m_appLaunchManager)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("应用启动功能初始化失败。"));
        return false;
    }

    AppLaunchStore store;
    QString loadError;
    AppLaunchConfig config = store.load(&loadError);
    if (!loadError.isEmpty())
    {
        Logger::warning(QStringLiteral("Launcher config load warning: ") + loadError);
    }

    AppLaunchPlan plan = m_appLaunchManager->resolveLaunchPlan(message, config);
    if (plan.needsManualSelection)
    {
        const QString executablePath = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择要启动的应用"),
            QString(),
            QStringLiteral("Applications (*.exe)"));
        if (executablePath.isEmpty())
        {
            return false;
        }

        plan = m_appLaunchManager->manualSelectionPlan(message, executablePath);
    }

    if (!plan.success)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(plan.errorMessage.isEmpty() ? QStringLiteral("没有找到可启动的应用。") : plan.errorMessage);
        return false;
    }

    QString confirmText = QStringLiteral("应用：%1\n来源：%2")
        .arg(plan.displayName.isEmpty() ? plan.requestedName : plan.displayName,
             plan.matchSource.isEmpty() ? QStringLiteral("未知") : plan.matchSource);
    if (!plan.executablePath.trimmed().isEmpty())
    {
        confirmText += QStringLiteral("\n路径：") + plan.executablePath;
    }
    if (!plan.shortcutPath.trimmed().isEmpty())
    {
        confirmText += QStringLiteral("\n快捷方式：") + plan.shortcutPath;
    }
    if (!plan.workingDirectory.trimmed().isEmpty())
    {
        confirmText += QStringLiteral("\n工作目录：") + plan.workingDirectory;
    }
    QDialog confirmDialog(this);
    confirmDialog.setWindowTitle(QStringLiteral("启动应用"));
    confirmDialog.setModal(true);

    auto *confirmLayout = new QVBoxLayout(&confirmDialog);
    confirmLayout->setContentsMargins(18, 18, 18, 14);
    confirmLayout->setSpacing(12);

    auto *questionLabel = new QLabel(QStringLiteral("确认启动该应用？"), &confirmDialog);
    QFont questionFont = questionLabel->font();
    questionFont.setBold(true);
    questionLabel->setFont(questionFont);
    confirmLayout->addWidget(questionLabel);

    auto *detailLabel = new QLabel(confirmText, &confirmDialog);
    detailLabel->setWordWrap(true);
    detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLabel->setMinimumWidth(520);
    confirmLayout->addWidget(detailLabel);

    auto *rememberCheckBox = new QCheckBox(QStringLiteral("记住为此名称，下次直接匹配"), &confirmDialog);
    rememberCheckBox->setVisible(plan.canRemember);
    confirmLayout->addWidget(rememberCheckBox);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &confirmDialog);
    if (QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok))
    {
        okButton->setText(QStringLiteral("启动"));
    }
    if (QPushButton *cancelButton = buttonBox->button(QDialogButtonBox::Cancel))
    {
        cancelButton->setText(QStringLiteral("取消"));
    }
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &confirmDialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &confirmDialog, &QDialog::reject);
    confirmLayout->addWidget(buttonBox);

    if (confirmDialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    const AppLaunchResult result = m_appLaunchManager->executeLaunchPlan(plan);
    if (!result.success)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(result.errorMessage.isEmpty() ? QStringLiteral("启动应用失败。") : result.errorMessage);
        return false;
    }

    QString bubbleText = result.message;
    if (plan.canRemember && rememberCheckBox->isChecked())
    {
        RegisteredApp app = m_appLaunchManager->registeredAppFromPlan(
            plan,
            {plan.requestedName, plan.displayName});

        bool replaced = false;
        for (RegisteredApp &existingApp : config.apps)
        {
            if (existingApp.id == app.id)
            {
                existingApp = app;
                replaced = true;
                break;
            }
        }
        if (!replaced)
        {
            config.apps.append(app);
        }

        QString saveError;
        if (!store.save(config, &saveError))
        {
            Logger::warning(QStringLiteral("Failed to save launcher config after manual app selection: ") + saveError);
            bubbleText += QStringLiteral("\n但保存应用别名失败。");
        }
    }

    playState(QStringLiteral("talk"), false);
    showBubbleMessage(bubbleText.isEmpty() ? QStringLiteral("应用已启动。") : bubbleText);
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
        showBubbleMessage(QStringLiteral("联网请求正在进行，请稍后。"));
        return false;
    }

    if ((m_conversationManager && m_conversationManager->isBusy()) || m_streamingChatActive)
    {
        playState(QStringLiteral("talk"), false);
        showBubbleMessage(QStringLiteral("AI 回复正在进行，请稍后再使用联网模式。"));
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
    showBubbleMessage(QStringLiteral("正在联网思考..."));

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
        if (window->m_conversationManager)
        {
            window->m_conversationManager->appendExternalExchange(chatMessage, displayText);
        }
        window->saveConversationHistoryIfNeeded();
        window->refreshChatHistoryPanel();
        window->showBubbleMessage(displayText);
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
    QString message = response.content.trimmed();
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
        message += QStringLiteral("\n\n来源：\n") + webCitationListText(response.citations);
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
    const QRect petGeometry = frameGeometry();
    constexpr int PanelSideSpacing = 8;

    QPoint position(
        petGeometry.right() + PanelSideSpacing,
        petGeometry.center().y() - panelSize.height() / 2);

    if (QScreen *screen = screenForPopup(this))
    {
        const QRect availableGeometry = screen->availableGeometry();
        const int maxX = qMax(availableGeometry.left(), availableGeometry.right() - panelSize.width());
        const int maxY = qMax(availableGeometry.top(), availableGeometry.bottom() - panelSize.height());
        if (position.x() > maxX)
        {
            position.setX(petGeometry.left() - panelSize.width() - PanelSideSpacing);
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
        showBubbleMessage(QStringLiteral("当前联网请求正在进行，请稍后。"));
        return false;
    }

    if (!m_conversationManager || m_conversationManager->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
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
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    stopAnimationPrewarm();
    playState(QStringLiteral("think"), false);
    hideReminderActions();
    m_streamingAssistantText.clear();
    m_streamBubbleUpdateTimer.stop();
    m_streamingChatActive = true;
    m_streamingTalkStarted = false;
    m_chatBubble->showMessage(QStringLiteral("正在思考..."), bubbleAnchorPosition(), 0);

    QPointer<PetWindow> window(this);
    m_conversationManager->sendUserMessageStreaming(
        chatMessage,
        [window](const QString &delta) {
            if (!window.isNull())
            {
                window->handleChatStreamDelta(delta);
            }
        },
        [window](const ChatResponse &response) {
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

    showBubbleMessage(QStringLiteral("没有正在进行的 AI 或联网请求。"));
}

bool PetWindow::hasActiveAIRequest() const
{
    return (m_conversationManager && m_conversationManager->isBusy()) || hasActiveWebRequest();
}

bool PetWindow::isManualStateSwitchLocked() const
{
    if (m_streamingChatActive || hasActiveAIRequest())
    {
        return true;
    }

    const QString currentState = m_stateMachine.currentState();
    return currentState == QStringLiteral("think") || currentState == QStringLiteral("talk");
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
    m_conversationManager->setMemoryHistoryMessageLimit(m_appConfig.memoryHistoryMessageLimit);

    if (loadPersistedHistory)
    {
        loadConversationHistoryIfNeeded();
    }

    saveConversationHistoryIfNeeded();
    refreshChatHistoryPanel();
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
    const QVector<ChatMessage> history = m_conversationStore->load(m_appConfig.savedHistoryMessageLimit, &loadError);
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
        if (!m_conversationStore->clear())
        {
            Logger::warning(QStringLiteral("Failed to clear empty conversation history."));
        }
        return;
    }

    if (!m_conversationStore->save(m_conversationManager->history(), m_appConfig.savedHistoryMessageLimit))
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
    if (m_streamingChatActive && !m_streamingTalkStarted)
    {
        m_streamingTalkStarted = true;
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
        true);
}

void PetWindow::finishStreamingChat()
{
    m_streamingChatActive = false;
    m_streamingTalkStarted = false;
    scheduleAnimationPrewarm();
}

void PetWindow::cancelStreamingChat()
{
    m_streamBubbleUpdateTimer.stop();
    m_streamingAssistantText.clear();
    m_streamingChatActive = false;
    m_streamingTalkStarted = false;
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
    if (m_chatBubble)
    {
        m_chatBubble->hideBubble();
    }
    hideReminderActions();
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

    QWidget::hideEvent(event);
}

void PetWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_reminderManager)
    {
        m_reminderManager->start();
        m_reminderManager->checkDueRemindersNow();
    }
    scheduleAnimationPrewarm();
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    resetBubbleAutoHideTimer();
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        playResolvedState(m_stateMachine.endDrag(), false);
        startChat();
        event->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        resetBubbleAutoHideTimer();
        move(event->globalPosition().toPoint() - m_dragOffset);
        updateBubblePosition();
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
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        stopAnimationPrewarm();
        playResolvedState(m_stateMachine.beginDrag(), false);
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
        m_dragging = false;
        playResolvedState(m_stateMachine.endDrag(), false);
        scheduleAnimationPrewarm();
        showNextTriggeredReminder();
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
    buildAnimationClips();

    if (m_clips.contains(QStringLiteral("idle")))
    {
        playResolvedState(m_stateMachine.start(), centerWindow);
        return true;
    }

    setDisplayImage(CharacterPackageRepository::previewPath(m_appConfig.characterId), centerWindow);
    return !m_characterPackage.states.isEmpty();
}

void PetWindow::buildAnimationClips()
{
    stopAnimationPrewarm();
    m_animationPrewarmQueue.clear();
    m_animationPrewarmAttemptedStates.clear();
    m_clipLastAccessSerial.clear();
    m_clipAccessSerial = 0;
    m_clips.clear();

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
}

QPoint PetWindow::bubbleAnchorPosition() const
{
    const CharacterBase &base = m_characterPackage.base;
    const CharacterBubble &bubble = m_characterPackage.bubble;
    const double baseWidth = base.width > 0 ? static_cast<double>(base.width) : static_cast<double>(width());
    const double baseHeight = base.height > 0 ? static_cast<double>(base.height) : static_cast<double>(height());
    const double scaleX = baseWidth > 0.0 ? static_cast<double>(width()) / baseWidth : 1.0;
    const double scaleY = baseHeight > 0.0 ? static_cast<double>(height()) / baseHeight : 1.0;
    const QPointF localAnchor(
        static_cast<double>(width()) * base.anchorX + bubble.offsetX * scaleX,
        static_cast<double>(height()) * base.anchorY + bubble.offsetY * scaleY);

    return frameGeometry().topLeft() + QPoint(qRound(localAnchor.x()), qRound(localAnchor.y()));
}

void PetWindow::playState(const QString &stateName, bool centerWindow)
{
    playResolvedState(m_stateMachine.requestState(stateName), centerWindow);
}

void PetWindow::playResolvedState(const QString &stateName, bool centerWindow)
{
    if (stateName.isEmpty())
    {
        return;
    }

    if (m_streamingChatActive
        && stateName != QStringLiteral("error")
        && stateName != QStringLiteral("drag"))
    {
        const QString preferredHeldState = m_streamingTalkStarted
            ? QStringLiteral("talk")
            : QStringLiteral("think");
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
        Logger::warning(QStringLiteral("Animation state failed to load: state=%1").arg(stateName));
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
    m_frameAnimator.play(clip);

    if (stateName == QStringLiteral("idle"))
    {
        scheduleIdleBehavior();
    }
    else if (clip->loop)
    {
        m_behaviorReturnTimer.start(4000);
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
        ? QRandomGenerator::global()->bounded(16000, 30001)
        : QRandomGenerator::global()->bounded(8000, 16001);
    m_idleBehaviorTimer.start(idleDelayMs);
}

void PetWindow::playIdleBehavior()
{
    if (m_dragging || m_stateMachine.currentState() != QStringLiteral("idle"))
    {
        scheduleIdleBehavior();
        return;
    }

    QStringList candidateStates;
    const QStringList preferredStates = {
        QStringLiteral("think"),
        QStringLiteral("sleep"),
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
    if (m_streamingChatActive)
    {
        const QString heldState = m_streamingTalkStarted
            ? QStringLiteral("talk")
            : QStringLiteral("think");

        if (m_clips.contains(heldState))
        {
            playResolvedState(heldState, false);
        }
        return;
    }

    if (!m_dragging)
    {
        playResolvedState(m_stateMachine.finishState(QStringLiteral("idle")), false);
    }
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
    resize(pixmap.size());
    if (size() != previousSize)
    {
        updateBubblePosition();
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
