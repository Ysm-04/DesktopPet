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
        std::function<void()> clearConversationHistoryCallback,
        QWidget *parent = nullptr);
    ~SettingsDialog() override;

    AIConfigStore aiConfigStore() const;
    AppConfig appConfig() const;

protected:
    void accept() override;

private:
    void cacheCurrentProvider();
    void loadProviderConfig(const QString &provider);
    void switchProvider(const QString &provider);
    bool buildAIConfigStore(AIConfigStore *store, QString *errorMessage) const;
    AIConfig configFromForm(const QString &provider, QString *errorMessage = nullptr) const;
    AIConfig runtimeConfigFromForm(const QString &provider) const;
    QString decryptedApiKey(const AIConfig &config) const;
    void testConnection();
    void setTestStatus(const QString &message, const QString &state);
    void clearConversationHistory();
    void reloadCharacterList(const QString &selectedCharacterId = {});
    void importCharacterFolder();
    void deleteSelectedCharacter();

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
    QCheckBox *m_enableAnimationPrewarmCheckBox = nullptr;
    QSpinBox *m_animationCacheLimitSpinBox = nullptr;
    QCheckBox *m_unloadAnimationsWhenHiddenCheckBox = nullptr;
    QSpinBox *m_requestContextMessageLimitSpinBox = nullptr;
    QSpinBox *m_memoryHistoryMessageLimitSpinBox = nullptr;
    QCheckBox *m_saveConversationHistoryCheckBox = nullptr;
    QSpinBox *m_savedHistoryMessageLimitSpinBox = nullptr;
    QPushButton *m_clearConversationHistoryButton = nullptr;
    QLabel *m_clearConversationStatusLabel = nullptr;
    QComboBox *m_characterComboBox = nullptr;
    QPushButton *m_importCharacterButton = nullptr;
    QPushButton *m_deleteCharacterButton = nullptr;
    QLabel *m_characterStatusLabel = nullptr;
    AIConfigStore m_configStore;
    AIConfigStore m_acceptedConfigStore;
    AppConfig m_appConfig;
    QString m_currentProvider;
    std::function<bool()> m_aiTestBlocked;
    std::function<void()> m_clearConversationHistory;
    std::unique_ptr<LLMProvider> m_testProvider;
    bool m_hasAcceptedConfigStore = false;
};
