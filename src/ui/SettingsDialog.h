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
    explicit SettingsDialog(const AIConfig &config, QWidget *parent = nullptr);

    AIConfig aiConfig() const;

private:
    void applyProviderPreset(const QString &provider);

    QComboBox *m_providerComboBox = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QSpinBox *m_timeoutSpinBox = nullptr;
    QDoubleSpinBox *m_temperatureSpinBox = nullptr;
    QSpinBox *m_maxTokensSpinBox = nullptr;
    QCheckBox *m_allowPlainApiKeyCheckBox = nullptr;
    AIConfig m_initialConfig;
};
