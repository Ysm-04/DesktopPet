#pragma once

#include "../config/AIConfig.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(const AIConfigStore &configStore, QWidget *parent = nullptr);

    AIConfigStore aiConfigStore() const;

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
    AIConfigStore m_configStore;
    QString m_currentProvider;
};
