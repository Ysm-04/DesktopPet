#include "PetWindow.h"

#include "../ai/ConversationManager.h"
#include "../ai/OpenAICompatibleProvider.h"
#include "../character/CharacterPackageLoader.h"
#include "../config/ConfigManager.h"
#include "../config/SecretStore.h"
#include "../util/Logger.h"
#include "ChatBubble.h"
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
    if (config.protocol != QStringLiteral("openai-compatible"))
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

QString roleLabel(const QString &role)
{
    if (role == QStringLiteral("user"))
    {
        return QStringLiteral("你");
    }

    if (role == QStringLiteral("assistant"))
    {
        return QStringLiteral("桌宠");
    }

    return role;
}

QString formattedConversationHistory(const QVector<ChatMessage> &history)
{
    if (history.isEmpty())
    {
        return QStringLiteral("暂无对话记录。");
    }

    QStringList lines;
    for (const ChatMessage &message : history)
    {
        const QString content = message.content.trimmed();
        if (content.isEmpty())
        {
            continue;
        }

        lines.append(roleLabel(message.role) + QStringLiteral("：") + content);
    }

    return lines.isEmpty() ? QStringLiteral("暂无对话记录。") : lines.join(QStringLiteral("\n\n"));
}
}

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
    , m_chatBubble(std::make_unique<ChatBubble>())
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

    QMenu *bubbleTestMenu = menu.addMenu(QStringLiteral("气泡测试"));
    QAction *shortBubbleAction = bubbleTestMenu->addAction(QStringLiteral("短文本"));
    QAction *maxBubbleAction = bubbleTestMenu->addAction(QStringLiteral("接近最大尺寸"));
    QAction *scrollBubbleAction = bubbleTestMenu->addAction(QStringLiteral("超长滚动文本"));

    const bool aiRequestRunning = hasActiveAIRequest();
    QAction *aiTestAction = menu.addAction(QStringLiteral("AI 测试"));
    aiTestAction->setEnabled(!aiRequestRunning);
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
    else if (selectedAction == shortBubbleAction)
    {
        showBubbleMessage(QStringLiteral("收到，马上处理。"));
    }
    else if (selectedAction == maxBubbleAction)
    {
        showBubbleMessage(QStringLiteral("这是一段用于测试气泡最大显示区域附近表现的文本。它应该自动换行，并在不出现滚动条的情况下尽量接近最大宽度和高度，方便观察边距、圆角、阴影和整体位置是否自然。"));
    }
    else if (selectedAction == scrollBubbleAction)
    {
        showBubbleMessage(QStringLiteral("这是一段用于测试超长 AI 回复的文本。第一段内容会让气泡快速达到最大高度，并触发垂直滚动条。用户应该可以通过滚动条查看后续内容，同时气泡本身不能继续无限变高。第二段内容继续补充更多文字，用来确认滚动区域、文本换行、右侧滚动条样式和顶部位置是否正常。第三段内容模拟模型输出较长解释时的情况：文本仍然需要保持清晰可读，不能遮挡整个桌面，也不能因为内容太多导致窗口尺寸失控。第四段内容用于确认自动隐藏计时仍然生效，并且再次打开气泡时滚动条会回到顶部。"));
    }
    else if (selectedAction == aiTestAction)
    {
        startAITest();
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

void PetWindow::startAITest()
{
    if (m_aiTestProvider && m_aiTestProvider->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 测试请求正在进行。"));
        return;
    }

    if (m_conversationManager && m_conversationManager->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
        return;
    }

    ConfigManager configManager;
    AIConfig config = configManager.loadAIConfig();
    QString errorMessage;
    if (!prepareRuntimeAIConfig(config, &errorMessage))
    {
        playState(QStringLiteral("error"), false);
        showBubbleMessage(errorMessage);
        return;
    }

    ChatRequest request;
    request.messages.append({QStringLiteral("system"), QStringLiteral("你是桌宠的最小连通性测试助手。")});
    request.messages.append({QStringLiteral("user"), QStringLiteral("请用一句话回复测试成功。")});

    m_aiTestProvider = std::make_unique<OpenAICompatibleProvider>(config);
    playState(QStringLiteral("think"), false);
    showBubbleMessage(QStringLiteral("正在测试 AI 连接..."));

    QPointer<PetWindow> window(this);
    m_aiTestProvider->sendChatRequest(request, [window](const ChatResponse &response) {
        if (window.isNull())
        {
            return;
        }

        if (response.success)
        {
            window->playState(QStringLiteral("talk"), false);
            window->showBubbleMessage(response.content);
            return;
        }

        window->playState(QStringLiteral("error"), false);
        window->showBubbleMessage(QStringLiteral("AI 测试失败：") + userVisibleErrorMessage(response));
    });
}

void PetWindow::startChat()
{
    if (m_aiTestProvider && m_aiTestProvider->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 测试请求正在进行。"));
        return;
    }

    if (!m_chatInputDialog)
    {
        return;
    }

    m_chatInputDialog->showAt(chatInputAnchorPosition());
}

bool PetWindow::submitChatMessage(const QString &message)
{
    if (m_aiTestProvider && m_aiTestProvider->isBusy())
    {
        showBubbleMessage(QStringLiteral("AI 测试请求正在进行。"));
        return false;
    }

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

    if (!m_conversationManager->setProvider(std::make_unique<OpenAICompatibleProvider>(config)))
    {
        showBubbleMessage(QStringLiteral("AI 回复正在进行。"));
        return false;
    }

    playState(QStringLiteral("think"), false);
    showBubbleMessage(QStringLiteral("正在思考..."));

    QPointer<PetWindow> window(this);
    m_conversationManager->sendUserMessage(message, [window](const ChatResponse &response) {
        if (window.isNull())
        {
            return;
        }

        if (response.success)
        {
            window->playState(QStringLiteral("talk"), false);
            window->showBubbleMessage(response.content);
            return;
        }

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
    if (m_aiTestProvider && m_aiTestProvider->isBusy())
    {
        m_aiTestProvider->cancel();
    }

    m_conversationManager->clear();
    showBubbleMessage(hadActiveRequest
        ? QStringLiteral("已取消 AI 请求，并清空对话。")
        : QStringLiteral("对话已清空。"));
    playState(QStringLiteral("idle"), false);
}

void PetWindow::cancelActiveAIRequest()
{
    bool canceled = false;
    if (m_aiTestProvider && m_aiTestProvider->isBusy())
    {
        m_aiTestProvider->cancel();
        canceled = true;
    }

    if (m_conversationManager && m_conversationManager->isBusy())
    {
        m_conversationManager->cancel();
        canceled = true;
    }

    if (!canceled)
    {
        showBubbleMessage(QStringLiteral("没有正在进行的 AI 请求。"));
        return;
    }

    showBubbleMessage(QStringLiteral("AI 请求已取消。"));
    playState(QStringLiteral("idle"), false);
}

bool PetWindow::hasActiveAIRequest() const
{
    return (m_aiTestProvider && m_aiTestProvider->isBusy())
        || (m_conversationManager && m_conversationManager->isBusy());
}

void PetWindow::showConversationHistory()
{
    const QVector<ChatMessage> history = m_conversationManager ? m_conversationManager->history() : QVector<ChatMessage>();
    m_chatBubble->showMessage(formattedConversationHistory(history), bubbleAnchorPosition(), 30000);
    m_chatBubble->setDismissOnExternalInteraction(true);
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
    if (m_chatBubble)
    {
        m_chatBubble->hideBubble();
    }
    if (m_chatInputDialog)
    {
        m_chatInputDialog->hide();
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
