#include "SettingsDialog.h"

#include "../ai/AIProviderFactory.h"
#include "../ai/LLMTypes.h"
#include "../config/SecretStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <utility>

namespace
{
QString normalizedProviderName(const QString &provider)
{
    const QString normalized = provider.trimmed().toLower();
    if (normalized == QStringLiteral("claude") || normalized == QStringLiteral("calude"))
    {
        return QStringLiteral("custom");
    }

    return normalized.isEmpty() ? QStringLiteral("custom") : normalized;
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

SettingsDialog::SettingsDialog(
    const AIConfigStore &configStore,
    const AppConfig &appConfig,
    std::function<bool()> aiTestBlocked,
    QWidget *parent)
    : QDialog(parent)
    , m_providerComboBox(new QComboBox(this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_apiKeyEdit(new QLineEdit(this))
    , m_modelEdit(new QLineEdit(this))
    , m_pathEdit(new QLineEdit(this))
    , m_timeoutSpinBox(new QSpinBox(this))
    , m_temperatureSpinBox(new QDoubleSpinBox(this))
    , m_maxTokensSpinBox(new QSpinBox(this))
    , m_testConnectionButton(new QPushButton(QStringLiteral("测试连接"), this))
    , m_allowPlainApiKeyCheckBox(new QCheckBox(QStringLiteral("允许在非 Windows 环境明文保存 API Key"), this))
    , m_scaleSpinBox(new QSpinBox(this))
    , m_performanceModeComboBox(new QComboBox(this))
    , m_pauseWhenHiddenCheckBox(new QCheckBox(QStringLiteral("隐藏到托盘时暂停动画"), this))
    , m_enableLazyLoadCheckBox(new QCheckBox(QStringLiteral("启用动画懒加载"), this))
    , m_characterComboBox(new QComboBox(this))
    , m_configStore(configStore)
    , m_appConfig(appConfig)
    , m_aiTestBlocked(std::move(aiTestBlocked))
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    resize(760, 520);

    const QList<QPair<QString, QString>> providers = {
        {QStringLiteral("openai"), QStringLiteral("OpenAI")},
        {QStringLiteral("google"), QStringLiteral("Google")},
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek")},
        {QStringLiteral("custom"), QStringLiteral("Custom")},
    };

    for (const auto &provider : providers)
    {
        m_providerComboBox->addItem(provider.second, provider.first);
    }

    const QString activeProvider = normalizedProviderName(m_configStore.activeProvider);
    const int providerIndex = m_providerComboBox->findData(activeProvider);
    m_providerComboBox->setCurrentIndex(providerIndex >= 0 ? providerIndex : m_providerComboBox->findData(QStringLiteral("custom")));

    m_apiKeyEdit->setEchoMode(QLineEdit::Password);

    m_timeoutSpinBox->setRange(1000, 300000);
    m_timeoutSpinBox->setSingleStep(1000);

    m_temperatureSpinBox->setRange(0.0, 2.0);
    m_temperatureSpinBox->setSingleStep(0.1);
    m_temperatureSpinBox->setDecimals(2);

    m_maxTokensSpinBox->setRange(1, 200000);

    m_scaleSpinBox->setRange(50, 200);
    m_scaleSpinBox->setSingleStep(10);
    m_scaleSpinBox->setSuffix(QStringLiteral("%"));
    m_scaleSpinBox->setValue(qRound(qBound(0.5, m_appConfig.scale, 2.0) * 100.0));

    m_performanceModeComboBox->addItem(QStringLiteral("标准"), QStringLiteral("standard"));
    m_performanceModeComboBox->addItem(QStringLiteral("低功耗"), QStringLiteral("low-power"));
    const int performanceModeIndex = m_performanceModeComboBox->findData(m_appConfig.performanceMode);
    m_performanceModeComboBox->setCurrentIndex(performanceModeIndex >= 0 ? performanceModeIndex : 0);

    m_pauseWhenHiddenCheckBox->setChecked(m_appConfig.pauseWhenHidden);
    m_enableLazyLoadCheckBox->setChecked(m_appConfig.enableLazyLoad);

    m_allowPlainApiKeyCheckBox->setVisible(!SecretStore::isEncryptionAvailable());
    m_currentProvider = normalizedProviderName(m_providerComboBox->currentData().toString());
    loadProviderConfig(m_currentProvider);

    m_testStatusLabel = new QLabel(QStringLiteral("未测试"), this);
    m_testStatusLabel->setObjectName(QStringLiteral("TestStatusLabel"));
    m_testStatusLabel->setWordWrap(true);

    auto *aiTitleLabel = new QLabel(QStringLiteral("AI 配置"), this);
    aiTitleLabel->setObjectName(QStringLiteral("PageTitle"));

    auto *aiFormLayout = new QFormLayout();
    aiFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    aiFormLayout->setLabelAlignment(Qt::AlignRight);
    aiFormLayout->setFormAlignment(Qt::AlignTop);
    aiFormLayout->setHorizontalSpacing(18);
    aiFormLayout->setVerticalSpacing(12);
    aiFormLayout->addRow(QStringLiteral("服务商"), m_providerComboBox);
    aiFormLayout->addRow(QStringLiteral("Base URL"), m_baseUrlEdit);
    aiFormLayout->addRow(QStringLiteral("API Key"), m_apiKeyEdit);
    aiFormLayout->addRow(QStringLiteral("Model"), m_modelEdit);
    aiFormLayout->addRow(QStringLiteral("Path"), m_pathEdit);
    aiFormLayout->addRow(QStringLiteral("Timeout"), m_timeoutSpinBox);
    aiFormLayout->addRow(QStringLiteral("Temperature"), m_temperatureSpinBox);
    aiFormLayout->addRow(QStringLiteral("Max Tokens"), m_maxTokensSpinBox);
    aiFormLayout->addRow(QString(), m_allowPlainApiKeyCheckBox);

    auto *storageHintLabel = new QLabel(this);
    storageHintLabel->setObjectName(QStringLiteral("HintLabel"));
    storageHintLabel->setWordWrap(true);
    storageHintLabel->setText(SecretStore::isEncryptionAvailable()
        ? QStringLiteral("API Key 将使用 Windows DPAPI 加密后保存。")
        : QStringLiteral("当前平台不支持内置加密。只有勾选确认后才会明文保存 API Key。"));

    auto *aiActionLayout = new QHBoxLayout();
    aiActionLayout->addWidget(m_testConnectionButton);
    aiActionLayout->addWidget(m_testStatusLabel, 1);

    auto *aiPageLayout = new QVBoxLayout();
    aiPageLayout->setContentsMargins(24, 24, 24, 24);
    aiPageLayout->setSpacing(16);
    aiPageLayout->addWidget(aiTitleLabel);
    aiPageLayout->addLayout(aiFormLayout);
    aiPageLayout->addWidget(storageHintLabel);
    aiPageLayout->addLayout(aiActionLayout);
    aiPageLayout->addStretch();

    auto *aiPage = new QWidget(this);
    aiPage->setLayout(aiPageLayout);

    auto *appTitleLabel = new QLabel(QStringLiteral("应用设置"), this);
    appTitleLabel->setObjectName(QStringLiteral("PageTitle"));

    auto *appFormLayout = new QFormLayout();
    appFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    appFormLayout->setLabelAlignment(Qt::AlignRight);
    appFormLayout->setFormAlignment(Qt::AlignTop);
    appFormLayout->setHorizontalSpacing(18);
    appFormLayout->setVerticalSpacing(12);
    appFormLayout->addRow(QStringLiteral("缩放"), m_scaleSpinBox);
    appFormLayout->addRow(QStringLiteral("性能模式"), m_performanceModeComboBox);
    appFormLayout->addRow(QString(), m_pauseWhenHiddenCheckBox);
    appFormLayout->addRow(QString(), m_enableLazyLoadCheckBox);

    auto *appPageLayout = new QVBoxLayout();
    appPageLayout->setContentsMargins(24, 24, 24, 24);
    appPageLayout->setSpacing(16);
    appPageLayout->addWidget(appTitleLabel);
    appPageLayout->addLayout(appFormLayout);
    appPageLayout->addStretch();

    auto *appPage = new QWidget(this);
    appPage->setLayout(appPageLayout);

    m_characterComboBox->addItem(QStringLiteral("shiroko"), QStringLiteral("shiroko"));
    m_characterComboBox->setEnabled(false);
    m_characterComboBox->setToolTip(QStringLiteral("角色选择将在多角色资源配置接入后启用。"));

    auto *characterTitleLabel = new QLabel(QStringLiteral("角色"), this);
    characterTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *characterHintLabel = new QLabel(QStringLiteral("当前版本使用内置角色资源。"), this);
    characterHintLabel->setObjectName(QStringLiteral("HintLabel"));
    characterHintLabel->setWordWrap(true);

    auto *characterFormLayout = new QFormLayout();
    characterFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    characterFormLayout->setLabelAlignment(Qt::AlignRight);
    characterFormLayout->setHorizontalSpacing(18);
    characterFormLayout->setVerticalSpacing(12);
    characterFormLayout->addRow(QStringLiteral("当前角色"), m_characterComboBox);

    auto *characterPageLayout = new QVBoxLayout();
    characterPageLayout->setContentsMargins(24, 24, 24, 24);
    characterPageLayout->setSpacing(16);
    characterPageLayout->addWidget(characterTitleLabel);
    characterPageLayout->addLayout(characterFormLayout);
    characterPageLayout->addWidget(characterHintLabel);
    characterPageLayout->addStretch();

    auto *characterPage = new QWidget(this);
    characterPage->setLayout(characterPageLayout);

    auto *navigationList = new QListWidget(this);
    navigationList->setObjectName(QStringLiteral("SettingsNavigation"));
    navigationList->setFixedWidth(150);
    navigationList->setFrameShape(QFrame::NoFrame);
    navigationList->setSpacing(4);
    navigationList->addItem(QStringLiteral("AI 配置"));
    navigationList->addItem(QStringLiteral("应用"));
    navigationList->addItem(QStringLiteral("角色"));

    auto *pageStack = new QStackedWidget(this);
    pageStack->setObjectName(QStringLiteral("SettingsPages"));
    pageStack->addWidget(aiPage);
    pageStack->addWidget(appPage);
    pageStack->addWidget(characterPage);

    connect(navigationList, &QListWidget::currentRowChanged, pageStack, &QStackedWidget::setCurrentIndex);
    navigationList->setCurrentRow(0);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(navigationList);
    contentLayout->addWidget(pageStack, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);
    layout->addLayout(contentLayout, 1);
    layout->addWidget(buttonBox);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #f8fafc; }"
        "QLabel#PageTitle { color: #111827; font-size: 18px; font-weight: 600; }"
        "QLabel#HintLabel { color: #667085; }"
        "QLabel#TestStatusLabel { color: #667085; padding-left: 8px; }"
        "QLabel#TestStatusLabel[status=\"error\"] { color: #b42318; }"
        "QLabel#TestStatusLabel[status=\"success\"] { color: #067647; }"
        "QLabel#TestStatusLabel[status=\"info\"] { color: #175cd3; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {"
        "  min-height: 30px; border: 1px solid #d0d5dd; border-radius: 6px;"
        "  padding: 4px 8px; background: #ffffff;"
        "}"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {"
        "  border: 1px solid #2e90fa;"
        "}"
        "QListWidget#SettingsNavigation {"
        "  background: transparent; border: none; padding: 4px;"
        "}"
        "QListWidget#SettingsNavigation::item {"
        "  min-height: 34px; padding: 6px 10px; border-radius: 6px; color: #475467;"
        "}"
        "QListWidget#SettingsNavigation::item:selected {"
        "  background: #eaf3ff; color: #175cd3; font-weight: 600;"
        "}"
        "QStackedWidget#SettingsPages {"
        "  background: #ffffff; border: 1px solid #eaecf0; border-radius: 8px;"
        "}"
        "QPushButton {"
        "  min-height: 30px; border: 1px solid #d0d5dd; border-radius: 6px;"
        "  padding: 4px 14px; background: #ffffff; color: #344054;"
        "}"
        "QPushButton:disabled { color: #98a2b3; background: #f2f4f7; }"
        "QPushButton:hover:enabled { background: #f9fafb; }"));

    connect(m_providerComboBox, &QComboBox::currentTextChanged, this, [this]() {
        switchProvider(m_providerComboBox->currentData().toString());
    });
    connect(m_testConnectionButton, &QPushButton::clicked, this, [this]() {
        testConnection();
    });
}

SettingsDialog::~SettingsDialog()
{
    if (m_testProvider)
    {
        m_testProvider->cancel();
    }
}

AIConfigStore SettingsDialog::aiConfigStore() const
{
    AIConfigStore store = m_configStore;
    store.providers.remove(QStringLiteral("claude"));
    store.providers.remove(QStringLiteral("calude"));
    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    store.activeProvider = provider;
    store.providers.insert(provider, configFromForm(provider));
    return store;
}

AppConfig SettingsDialog::appConfig() const
{
    AppConfig config = m_appConfig;
    config.scale = m_scaleSpinBox->value() / 100.0;
    config.performanceMode = m_performanceModeComboBox->currentData().toString();
    if (config.performanceMode.isEmpty())
    {
        config.performanceMode = QStringLiteral("standard");
    }
    config.pauseWhenHidden = m_pauseWhenHiddenCheckBox->isChecked();
    config.enableLazyLoad = m_enableLazyLoadCheckBox->isChecked();
    return config;
}

void SettingsDialog::cacheCurrentProvider()
{
    if (m_currentProvider.isEmpty())
    {
        return;
    }

    m_configStore.providers.insert(m_currentProvider, configFromForm(m_currentProvider));
}

void SettingsDialog::loadProviderConfig(const QString &provider)
{
    const QString normalizedProvider = normalizedProviderName(provider);
    const AIConfig config = m_configStore.providers.value(normalizedProvider, defaultAIConfigForProvider(normalizedProvider));

    m_baseUrlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(decryptedApiKey(config));
    m_modelEdit->setText(config.model);
    m_pathEdit->setText(config.path);
    m_timeoutSpinBox->setValue(config.timeoutMs);
    m_temperatureSpinBox->setValue(config.temperature);
    m_maxTokensSpinBox->setValue(config.maxTokens);
    m_allowPlainApiKeyCheckBox->setChecked(config.allowPlainApiKey);
}

void SettingsDialog::switchProvider(const QString &provider)
{
    const QString normalizedProvider = normalizedProviderName(provider);
    if (normalizedProvider == m_currentProvider)
    {
        return;
    }

    if (m_testProvider && m_testProvider->isBusy())
    {
        m_testProvider->cancel();
        m_testConnectionButton->setEnabled(true);
        setTestStatus(QStringLiteral("测试已取消。"), QStringLiteral("info"));
    }

    cacheCurrentProvider();
    loadProviderConfig(normalizedProvider);
    m_currentProvider = normalizedProvider;
}

AIConfig SettingsDialog::configFromForm(const QString &provider) const
{
    const QString normalizedProvider = normalizedProviderName(provider);
    AIConfig config = m_configStore.providers.value(normalizedProvider, defaultAIConfigForProvider(normalizedProvider));
    config.provider = normalizedProvider;
    config.protocol = defaultAIConfigForProvider(normalizedProvider).protocol;
    config.baseUrl = m_baseUrlEdit->text().trimmed();
    config.model = m_modelEdit->text().trimmed();
    config.path = m_pathEdit->text().trimmed();
    config.timeoutMs = m_timeoutSpinBox->value();
    config.temperature = m_temperatureSpinBox->value();
    config.maxTokens = m_maxTokensSpinBox->value();

    const QString apiKey = m_apiKeyEdit->text();
    config.apiKey.clear();
    config.apiKeyEncrypted.clear();
    config.allowPlainApiKey = m_allowPlainApiKeyCheckBox->isChecked();

    if (apiKey.isEmpty())
    {
        config.apiKeyStorage = QStringLiteral("none");
    }
    else if (SecretStore::isEncryptionAvailable())
    {
        const SecretStore::Result result = SecretStore::protectText(apiKey);
        if (result.success)
        {
            config.apiKeyStorage = SecretStore::preferredStorageName();
            config.apiKeyEncrypted = result.value;
        }
        else
        {
            config.apiKeyStorage = QStringLiteral("none");
        }
    }
    else if (config.allowPlainApiKey)
    {
        config.apiKeyStorage = QStringLiteral("plain-json");
        config.apiKey = apiKey;
    }
    else
    {
        config.apiKeyStorage = QStringLiteral("none");
    }

    return config;
}

AIConfig SettingsDialog::runtimeConfigFromForm(const QString &provider) const
{
    const QString normalizedProvider = normalizedProviderName(provider);
    AIConfig config = m_configStore.providers.value(normalizedProvider, defaultAIConfigForProvider(normalizedProvider));
    config.provider = normalizedProvider;
    config.protocol = defaultAIConfigForProvider(normalizedProvider).protocol;
    config.baseUrl = m_baseUrlEdit->text().trimmed();
    config.model = m_modelEdit->text().trimmed();
    config.path = m_pathEdit->text().trimmed();
    config.timeoutMs = m_timeoutSpinBox->value();
    config.maxTokens = m_maxTokensSpinBox->value();
    config.allowPlainApiKey = m_allowPlainApiKeyCheckBox->isChecked();

    const QString apiKey = m_apiKeyEdit->text();
    config.apiKey.clear();
    config.apiKeyEncrypted.clear();
    if (!apiKey.isEmpty())
    {
        config.apiKey = apiKey;
        config.apiKeyStorage = QStringLiteral("runtime");
    }
    else
    {
        config.apiKeyStorage = QStringLiteral("none");
    }

    config.stream = false;
    config.temperature = 0.0;
    config.maxTokens = qMin(config.maxTokens, 16);
    return config;
}

QString SettingsDialog::decryptedApiKey(const AIConfig &config) const
{
    if (config.apiKeyStorage == QStringLiteral("windows-dpapi") && !config.apiKeyEncrypted.isEmpty())
    {
        const SecretStore::Result result = SecretStore::unprotectText(config.apiKeyEncrypted);
        if (result.success)
        {
            return result.value;
        }

        return {};
    }

    return config.apiKey;
}

void SettingsDialog::testConnection()
{
    if (m_aiTestBlocked && m_aiTestBlocked())
    {
        QMessageBox::information(
            this,
            QStringLiteral("测试连接"),
            QStringLiteral("当前 AI 对话正在进行，请等待对话结束后再测试连接。"));
        return;
    }

    if (m_testProvider && m_testProvider->isBusy())
    {
        return;
    }

    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    AIConfig config = runtimeConfigFromForm(provider);
    QString errorMessage;
    if (!AIProviderFactory::prepareRuntimeConfig(config, &errorMessage))
    {
        setTestStatus(errorMessage, QStringLiteral("error"));
        return;
    }

    std::unique_ptr<LLMProvider> providerInstance = AIProviderFactory::createProvider(config);
    if (!providerInstance)
    {
        setTestStatus(QStringLiteral("当前 Provider 协议暂未接入。"), QStringLiteral("error"));
        return;
    }

    m_testProvider = std::move(providerInstance);
    m_testConnectionButton->setEnabled(false);
    setTestStatus(QStringLiteral("正在测试连接..."), QStringLiteral("info"));

    ChatRequest request;
    request.messages.append({
        QStringLiteral("user"),
        QStringLiteral("请只回复 OK，用于连接测试。")
    });

    QPointer<SettingsDialog> dialog(this);
    m_testProvider->sendChatRequest(request, [dialog](const ChatResponse &response) {
        if (dialog.isNull())
        {
            return;
        }

        dialog->m_testConnectionButton->setEnabled(true);
        if (response.success)
        {
            const QString status = response.httpStatus > 0
                ? QStringLiteral("连接成功，HTTP ") + QString::number(response.httpStatus)
                : QStringLiteral("连接成功。");
            dialog->setTestStatus(status, QStringLiteral("success"));
            return;
        }

        dialog->setTestStatus(QStringLiteral("连接失败：") + userVisibleErrorMessage(response), QStringLiteral("error"));
    });
}

void SettingsDialog::setTestStatus(const QString &message, const QString &state)
{
    m_testStatusLabel->setText(message);
    m_testStatusLabel->setToolTip(message);
    m_testStatusLabel->setProperty("status", state);
    m_testStatusLabel->style()->unpolish(m_testStatusLabel);
    m_testStatusLabel->style()->polish(m_testStatusLabel);
}
