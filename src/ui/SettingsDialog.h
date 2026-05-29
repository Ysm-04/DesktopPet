#pragma once

#include "../config/AIConfig.h"
#include "../config/AppConfig.h"

#include <QDialog>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class LLMProvider;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(
        const AIConfigStore &configStore,
        const AppConfig &appConfig,
        std::function<bool()> aiTestBlocked,
        QWidget *parent = nullptr);
    ~SettingsDialog() override;

    AIConfigStore aiConfigStore() const;
    AppConfig appConfig() const;

private:
    void cacheCurrentProvider();
    void loadProviderConfig(const QString &provider);
    void switchProvider(const QString &provider);
    AIConfig configFromForm(const QString &provider) const;
    AIConfig runtimeConfigFromForm(const QString &provider) const;
    QString decryptedApiKey(const AIConfig &config) const;
    void testConnection();
    void setTestStatus(const QString &message, const QString &state);

    QComboBox *m_providerComboBox = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QSpinBox *m_timeoutSpinBox = nullptr;
    QDoubleSpinBox *m_temperatureSpinBox = nullptr;
    QSpinBox *m_maxTokensSpinBox = nullptr;
    QPushButton *m_testConnectionButton = nullptr;
    QLabel *m_testStatusLabel = nullptr;
    QCheckBox *m_allowPlainApiKeyCheckBox = nullptr;
    QSpinBox *m_scaleSpinBox = nullptr;
    QComboBox *m_performanceModeComboBox = nullptr;
    QCheckBox *m_pauseWhenHiddenCheckBox = nullptr;
    QCheckBox *m_enableLazyLoadCheckBox = nullptr;
    QComboBox *m_characterComboBox = nullptr;
    AIConfigStore m_configStore;
    AppConfig m_appConfig;
    QString m_currentProvider;
    std::function<bool()> m_aiTestBlocked;
    std::unique_ptr<LLMProvider> m_testProvider;
};
