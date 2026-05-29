#include "PetWindow.h"

#include "../ai/ConversationManager.h"
#include "../ai/GoogleGeminiProvider.h"
#include "../ai/OpenAICompatibleProvider.h"
#include "../character/CharacterPackageLoader.h"
#include "../config/ConfigManager.h"
#include "../config/SecretStore.h"
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
#include <QPointer>
#include <QRandomGenerator>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>

#include <memory>

namespace
{
QString characterPackagePath()
{
    return QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/shiroko");
}

QString previewImagePath()
{
    return QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/shiroko/preview.png");
}

constexpr int MaxUserMessageLength = 4000;
constexpr int ChatInputLowerOffsetY = 48;
constexpr int StreamBubbleUpdateIntervalMs = 80;

bool populateRuntimeApiKey(AIConfig &config, QString *errorMessage)
{
    if (!config.apiKey.trimmed().isEmpty())
    {
        return true;
    }

    if (config.apiKeyStorage == QStringLiteral("windows-dpapi"))
    {
        if (config.apiKeyEncrypted.trimmed().isEmpty())
        {
            *errorMessage = QStringLiteral("请先在设置里配置 API Key。");
            return false;
        }

        const SecretStore::Result result = SecretStore::unprotectText(config.apiKeyEncrypted);
        if (!result.success)
        {
            *errorMessage = QStringLiteral("API Key 解密失败：") + result.errorMessage;
            return false;
        }

        config.apiKey = result.value;
    }

    if (config.apiKey.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 API Key。");
        return false;
    }

    return true;
}

bool prepareRuntimeAIConfig(AIConfig &config, QString *errorMessage)
{
    const bool supportedProtocol = config.protocol == QStringLiteral("openai-compatible")
        || config.protocol == QStringLiteral("google-generative-language");
    if (!supportedProtocol)
    {
        *errorMessage = QStringLiteral("当前 Provider 协议暂未接入。");
        return false;
    }

    if (!populateRuntimeApiKey(config, errorMessage))
    {
        return false;
    }

    if (config.baseUrl.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 Base URL。");
        return false;
    }

    if (config.model.trimmed().isEmpty())
    {
        *errorMessage = QStringLiteral("请先在设置里配置 Model。");
        return false;
    }

    return true;
}

std::unique_ptr<LLMProvider> createProvider(const AIConfig &config)
{
    if (config.protocol == QStringLiteral("google-generative-language"))
    {
        return std::make_unique<GoogleGeminiProvider>(config);
    }

    if (config.protocol == QStringLiteral("openai-compatible"))
    {
        return std::make_unique<OpenAICompatibleProvider>(config);
    }

    return nullptr;
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

PetWindow::~PetWindow() = default;

void PetWindow::applyAppConfig(const AppConfig &config)
{
    setAlwaysOnTop(config.alwaysOnTop);

    if (config.hasWindowPosition && isPointVisibleOnScreen(config.windowPosition))
    {
        move(config.windowPosition);
    }
}

AppConfig PetWindow::currentAppConfig() const
{
    AppConfig config;
    config.windowPosition = pos();
    config.hasWindowPosition = true;
    config.alwaysOnTop = m_alwaysOnTop;
    return config;
}

void PetWindow::pauseAnimation()
{
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
    QAction *settingsAction = menu.addAction(QStringLiteral("设置"));

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
    else if (selectedAction == settingsAction)
    {
        ConfigManager configManager;
        SettingsDialog dialog(configManager.loadAIConfigStore(), this);
        if (dialog.exec() == QDialog::Accepted && !configManager.saveAIConfigStore(dialog.aiConfigStore()))
        {
            Logger::warning(QStringLiteral("Failed to save AI config from settings dialog."));
        }
    }
    else if (selectedAction == exitAction)
    {
        close();
    }
    else if (selectedAction != nullptr && selectedAction->data().isValid())
    {
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
    if (!prepareRuntimeAIConfig(config, &errorMessage))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(errorMessage);
        return false;
    }

    std::unique_ptr<LLMProvider> provider = createProvider(config);
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
                window->finishStreamingChat();
                window->m_streamingAssistantText = response.content;
                window->flushStreamingBubble(true);
                window->refreshChatHistoryPanel();
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

void PetWindow::showConversationHistory()
{
    const QVector<ChatMessage> history = m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>();
    m_chatHistoryPanel->setMessages(history);
    m_chatHistoryPanel->showNear(frameGeometry());
}

void PetWindow::refreshChatHistoryPanel()
{
    if (!m_chatHistoryPanel || !m_chatHistoryPanel->isVisible())
    {
        return;
    }

    const QVector<ChatMessage> history = m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>();
    m_chatHistoryPanel->setMessages(history);
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
    m_characterPackage = CharacterPackageLoader::load(characterPackagePath(), &loadError);
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

    setDisplayImage(previewImagePath(), true);
}

void PetWindow::buildAnimationClips()
{
    m_clips.clear();

    QSet<QString> availableStates;
    const QSize targetSize(320, 320);
    for (auto iterator = m_characterPackage.states.constBegin(); iterator != m_characterPackage.states.constEnd(); ++iterator)
    {
        AnimationClip clip = AnimationClip::fromState(iterator.value(), targetSize);
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

    stateMenu->setEnabled(!stateMenu->actions().isEmpty());
}

void PetWindow::updateBubblePosition()
{
    m_chatBubble->updateAnchorPosition(bubbleAnchorPosition());
}

QPoint PetWindow::bubbleAnchorPosition() const
{
    return frameGeometry().topLeft() + QPoint(width() / 2, 0);
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
        const QString heldState = m_streamingTalkStarted
            ? QStringLiteral("talk")
            : QStringLiteral("think");

        if (stateName != heldState && m_clips.contains(heldState))
        {
            playResolvedState(heldState, centerWindow);
        }

        if (stateName != heldState)
        {
            return;
        }
    }

    if (m_frameAnimator.currentStateName() == stateName && m_frameAnimator.isPlaying())
    {
        return;
    }

    auto clipIterator = m_clips.constFind(stateName);
    if (clipIterator == m_clips.constEnd())
    {
        return;
    }

    const AnimationClip *clip = &clipIterator.value();
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

void PetWindow::scheduleIdleBehavior()
{
    if (!m_clips.contains(QStringLiteral("idle")))
    {
        return;
    }

    const int idleDelayMs = QRandomGenerator::global()->bounded(8000, 16001);
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

    const QSize targetSize(320, 320);
    const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    setDisplayPixmap(scaled, centerWindow);
}

void PetWindow::setDisplayPixmap(const QPixmap &pixmap, bool centerWindow)
{
    m_petView->setFrame(pixmap);
    resize(pixmap.size());

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
