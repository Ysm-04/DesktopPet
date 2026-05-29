#include "SettingsDialog.h"

#include "../config/SecretStore.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QSpinBox>
#include <QVBoxLayout>

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
}

SettingsDialog::SettingsDialog(const AIConfigStore &configStore, QWidget *parent)
    : QDialog(parent)
    , m_providerComboBox(new QComboBox(this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_apiKeyEdit(new QLineEdit(this))
    , m_modelEdit(new QLineEdit(this))
    , m_pathEdit(new QLineEdit(this))
    , m_timeoutSpinBox(new QSpinBox(this))
    , m_temperatureSpinBox(new QDoubleSpinBox(this))
    , m_maxTokensSpinBox(new QSpinBox(this))
    , m_allowPlainApiKeyCheckBox(new QCheckBox(QStringLiteral("允许在非 Windows 环境明文保存 API Key"), this))
    , m_configStore(configStore)
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    resize(460, 360);

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

    m_allowPlainApiKeyCheckBox->setVisible(!SecretStore::isEncryptionAvailable());
    m_currentProvider = normalizedProviderName(m_providerComboBox->currentData().toString());
    loadProviderConfig(m_currentProvider);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(QStringLiteral("服务商"), m_providerComboBox);
    formLayout->addRow(QStringLiteral("Base URL"), m_baseUrlEdit);
    formLayout->addRow(QStringLiteral("API Key"), m_apiKeyEdit);
    formLayout->addRow(QStringLiteral("Model"), m_modelEdit);
    formLayout->addRow(QStringLiteral("Path"), m_pathEdit);
    formLayout->addRow(QStringLiteral("Timeout"), m_timeoutSpinBox);
    formLayout->addRow(QStringLiteral("Temperature"), m_temperatureSpinBox);
    formLayout->addRow(QStringLiteral("Max Tokens"), m_maxTokensSpinBox);
    formLayout->addRow(QString(), m_allowPlainApiKeyCheckBox);

    auto *storageHintLabel = new QLabel(this);
    storageHintLabel->setWordWrap(true);
    storageHintLabel->setText(SecretStore::isEncryptionAvailable()
        ? QStringLiteral("API Key 将使用 Windows DPAPI 加密后保存。")
        : QStringLiteral("当前平台不支持内置加密。只有勾选确认后才会明文保存 API Key。"));

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(formLayout);
    layout->addWidget(storageHintLabel);
    layout->addWidget(buttonBox);

    connect(m_providerComboBox, &QComboBox::currentTextChanged, this, [this]() {
        switchProvider(m_providerComboBox->currentData().toString());
    });
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
