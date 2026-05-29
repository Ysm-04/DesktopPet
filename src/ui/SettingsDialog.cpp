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

SettingsDialog::SettingsDialog(const AIConfig &config, QWidget *parent)
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
    , m_initialConfig(config)
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    resize(460, 360);

    const QList<QPair<QString, QString>> providers = {
        {QStringLiteral("openai"), QStringLiteral("OpenAI")},
        {QStringLiteral("google"), QStringLiteral("Google")},
        {QStringLiteral("claude"), QStringLiteral("Claude")},
        {QStringLiteral("deepseek"), QStringLiteral("DeepSeek")},
        {QStringLiteral("custom"), QStringLiteral("Custom")},
    };

    for (const auto &provider : providers)
    {
        m_providerComboBox->addItem(provider.second, provider.first);
    }

    const int providerIndex = m_providerComboBox->findData(config.provider);
    m_providerComboBox->setCurrentIndex(providerIndex >= 0 ? providerIndex : m_providerComboBox->findData(QStringLiteral("custom")));

    QString apiKey = config.apiKey;
    if (config.apiKeyStorage == QStringLiteral("windows-dpapi") && !config.apiKeyEncrypted.isEmpty())
    {
        const SecretStore::Result result = SecretStore::unprotectText(config.apiKeyEncrypted);
        if (result.success)
        {
            apiKey = result.value;
        }
    }

    m_baseUrlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setText(apiKey);
    m_modelEdit->setText(config.model);
    m_pathEdit->setText(config.path);

    m_timeoutSpinBox->setRange(1000, 300000);
    m_timeoutSpinBox->setSingleStep(1000);
    m_timeoutSpinBox->setValue(config.timeoutMs);

    m_temperatureSpinBox->setRange(0.0, 2.0);
    m_temperatureSpinBox->setSingleStep(0.1);
    m_temperatureSpinBox->setDecimals(2);
    m_temperatureSpinBox->setValue(config.temperature);

    m_maxTokensSpinBox->setRange(1, 200000);
    m_maxTokensSpinBox->setValue(config.maxTokens);

    m_allowPlainApiKeyCheckBox->setChecked(config.allowPlainApiKey);
    m_allowPlainApiKeyCheckBox->setVisible(!SecretStore::isEncryptionAvailable());

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
        applyProviderPreset(m_providerComboBox->currentData().toString());
    });
}

AIConfig SettingsDialog::aiConfig() const
{
    AIConfig config = m_initialConfig;
    config.provider = m_providerComboBox->currentData().toString();
    config.protocol = defaultAIConfigForProvider(config.provider).protocol;
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

void SettingsDialog::applyProviderPreset(const QString &provider)
{
    const AIConfig preset = defaultAIConfigForProvider(provider);
    m_baseUrlEdit->setText(preset.baseUrl);
    m_pathEdit->setText(preset.path);
}
