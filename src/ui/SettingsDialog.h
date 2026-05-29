#pragma once

#include "../config/AIConfig.h"
#include "../config/AppConfig.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(const AIConfigStore &configStore, const AppConfig &appConfig, QWidget *parent = nullptr);

    AIConfigStore aiConfigStore() const;
    AppConfig appConfig() const;

private:
    void cacheCurrentProvider();
    void loadProviderConfig(const QString &provider);
    void switchProvider(const QString &provider);
    AIConfig configFromForm(const QString &provider) const;
    QString decryptedApiKey(const AIConfig &config) const;

    QComboBox *m_providerComboBox = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QSpinBox *m_timeoutSpinBox = nullptr;
    QDoubleSpinBox *m_temperatureSpinBox = nullptr;
    QSpinBox *m_maxTokensSpinBox = nullptr;
    QCheckBox *m_allowPlainApiKeyCheckBox = nullptr;
    QSpinBox *m_scaleSpinBox = nullptr;
    QComboBox *m_performanceModeComboBox = nullptr;
    QCheckBox *m_pauseWhenHiddenCheckBox = nullptr;
    QCheckBox *m_enableLazyLoadCheckBox = nullptr;
    AIConfigStore m_configStore;
    AppConfig m_appConfig;
    QString m_currentProvider;
};
