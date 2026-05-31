#include "PetWindow.h"

#include "../ai/AIProviderFactory.h"
#include "../ai/ConversationManager.h"
#include "../ai/ConversationStore.h"
#include "../character/CharacterPackageLoader.h"
#include "../character/CharacterPackageRepository.h"
#include "../config/ConfigManager.h"
#include "../util/Logger.h"
#include "ChatBubble.h"
#include "ChatHistoryPanel.h"
#include "ChatInputDialog.h"
#include "PetView.h"
#include "SettingsDialog.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDialog>
#include <QGuiApplication>
#include <QHideEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>
#include <QtGlobal>

#include <memory>

namespace
{
constexpr int MaxUserMessageLength = 4000;
constexpr int ChatInputLowerOffsetY = 48;
constexpr int StreamBubbleUpdateIntervalMs = 80;
constexpr int MinAnimationTargetSide = 32;
constexpr int MaxAnimationTargetSide = 2048;
constexpr int LowPowerFpsCap = 6;
constexpr int ChatFinishedReturnDelayMs = 1500;

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

AppConfig normalizedAppConfig(AppConfig config)
{
    config.scale = qBound(0.5, config.scale, 2.0);
    if (config.performanceMode != QStringLiteral("standard")
        && config.performanceMode != QStringLiteral("low-power"))
    {
        config.performanceMode = QStringLiteral("standard");
    }
    config.requestContextMessageLimit = qBound(0, config.requestContextMessageLimit, 200);
    config.memoryHistoryMessageLimit = evenBoundedHistoryLimit(config.memoryHistoryMessageLimit, 2, 5000);
    config.savedHistoryMessageLimit = evenBoundedHistoryLimit(config.savedHistoryMessageLimit, 2, 10000);
    return config;
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

}

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
    , m_chatBubble(std::make_unique<ChatBubble>())
    , m_chatHistoryPanel(std::make_unique<ChatHistoryPanel>(this))
    , m_chatInputDialog(std::make_unique<ChatInputDialog>(MaxUserMessageLength, this))
    , m_conversationManager(std::make_unique<ConversationManager>())
    , m_conversationStore(std::make_unique<ConversationStore>(ConfigManager().conversationHistoryPath()))
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

    QPointer<PetWindow> window(this);
    m_chatInputDialog->setSubmitCallback([window](const QString &message) {
        return !window.isNull() && window->submitChatMessage(message);
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
    const bool rebuildClips = !qFuzzyCompare(m_appConfig.scale, normalizedConfig.scale)
        || m_appConfig.performanceMode != normalizedConfig.performanceMode
        || m_appConfig.enableLazyLoad != normalizedConfig.enableLazyLoad;
    const bool loadPersistedHistory = !m_appConfig.saveConversationHistory
        && normalizedConfig.saveConversationHistory;

    m_appConfig = normalizedConfig;
    setAlwaysOnTop(m_appConfig.alwaysOnTop);
    configureConversation(loadPersistedHistory);

    if (m_appConfig.hasWindowPosition && isPointVisibleOnScreen(m_appConfig.windowPosition))
    {
        move(m_appConfig.windowPosition);
    }

    if (rebuildClips && !m_characterPackage.states.isEmpty())
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
}

AppConfig PetWindow::currentAppConfig() const
{
    AppConfig config = m_appConfig;
    config.windowPosition = pos();
    config.hasWindowPosition = true;
    config.alwaysOnTop = m_alwaysOnTop;
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
}

void PetWindow::showBubbleMessage(const QString &message)
{
    m_chatBubble->showMessage(message, bubbleAnchorPosition());
}

void PetWindow::openSettingsDialog()
{
    ConfigManager configManager;
    SettingsDialog dialog(configManager.loadAIConfigStore(), currentAppConfig(), [this]() {
        return isManualStateSwitchLocked();
    }, [this]() {
        clearConversation();
    }, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    if (!configManager.saveAIConfigStore(dialog.aiConfigStore()))
    {
        Logger::warning(QStringLiteral("Failed to save AI config from settings dialog."));
    }

    applyAppConfig(dialog.appConfig());
    if (!configManager.saveAppConfig(currentAppConfig()))
    {
        Logger::warning(QStringLiteral("Failed to save app config from settings dialog."));
    }
}

void PetWindow::setSettingsFallbackInContextMenuEnabled(bool enabled)
{
    m_settingsFallbackInContextMenuEnabled = enabled;
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
    QAction *cancelAIAction = menu.addAction(QStringLiteral("取消 AI 请求"));
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

    m_chatInputDialog->showAt(chatInputAnchorPosition());
}

bool PetWindow::submitChatMessage(const QString &message)
{
    if (!m_conversationManager || m_conversationManager->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    const QString trimmedMessage = message.trimmed();
    if (trimmedMessage.isEmpty())
    {
        return false;
    }

    if (trimmedMessage.size() > MaxUserMessageLength)
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(QStringLiteral("消息太长，请控制在 4000 字以内。"));
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

    if (!m_conversationManager->setProvider(std::move(provider)))
    {
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    playState(QStringLiteral("think"), false);
    m_streamingAssistantText.clear();
    m_streamBubbleUpdateTimer.stop();
    m_streamingChatActive = true;
    m_streamingTalkStarted = false;
    m_chatBubble->showMessage(QStringLiteral("正在思考..."), bubbleAnchorPosition(), 0);

    QPointer<PetWindow> window(this);
    m_conversationManager->sendUserMessageStreaming(
        trimmedMessage,
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
    refreshChatHistoryPanel();
    showBubbleMessage(hadActiveRequest
        ? QStringLiteral("已取消 AI 请求，并清空对话。")
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

    showBubbleMessage(QStringLiteral("没有正在进行的 AI 请求。"));
}

bool PetWindow::hasActiveAIRequest() const
{
    return m_conversationManager && m_conversationManager->isBusy();
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
}

void PetWindow::cancelStreamingChat()
{
    m_streamBubbleUpdateTimer.stop();
    m_streamingAssistantText.clear();
    m_streamingChatActive = false;
    m_streamingTalkStarted = false;
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
    if (m_chatBubble)
    {
        m_chatBubble->hideBubble();
    }
    if (m_chatInputDialog)
    {
        m_chatInputDialog->hide();
    }
    if (m_chatHistoryPanel)
    {
        m_chatHistoryPanel->hide();
    }

    QWidget::hideEvent(event);
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
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PetWindow::loadInitialImage()
{
    QString loadError;
    m_characterPackage = CharacterPackageLoader::load(CharacterPackageRepository::defaultPackagePath(), &loadError);
    if (!loadError.isEmpty())
    {
        Logger::warning(QStringLiteral("Character package load failed: ") + loadError);
    }

    buildAnimationClips();

    if (m_clips.contains(QStringLiteral("idle")))
    {
        playResolvedState(m_stateMachine.start(), true);
        return;
    }

    setDisplayImage(CharacterPackageRepository::defaultPreviewPath(), true);
}

void PetWindow::buildAnimationClips()
{
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
        return;
    }

    auto clipIterator = m_clips.find(stateName);
    if (clipIterator == m_clips.end())
    {
        return;
    }

    AnimationClip *clip = &clipIterator.value();
    if (!clip->ensureLoaded())
    {
        return;
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
