#pragma once

#include "../config/AIConfig.h"
#include "../config/AppConfig.h"
#include "../ai/LLMTypes.h"
#include "../reminder/ReminderTypes.h"
#include "../web/WebConfig.h"
#include "../weather/WeatherConfig.h"

#include <QDialog>
#include <QVector>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class LLMProvider;
class WebChatManager;
class WeatherManager;

class SettingsDialog : public QDialog
{
public:
    explicit SettingsDialog(
        const AIConfigStore &configStore,
        const AppConfig &appConfig,
        const WeatherConfig &weatherConfig,
        const WebConfig &webConfig,
        const QVector<ChatMessage> &conversationHistory,
        const QVector<ReminderItem> &reminders,
        std::function<bool()> aiTestBlocked,
        std::function<void()> clearConversationHistoryCallback,
        std::function<bool(const QString &, QString *)> cancelReminderCallback,
        std::function<bool(const QString &, const QString &, const QDateTime &, const ReminderRecurrence &, ReminderItem *, QString *)> updateReminderCallback,
        std::function<bool(QString *)> clearFinishedRemindersCallback,
        std::function<void(const QString &, double)> playReminderSoundCallback,
        QWidget *parent = nullptr);
    ~SettingsDialog() override;

    AIConfigStore aiConfigStore() const;
    AppConfig appConfig() const;
    WeatherConfig weatherConfig() const;
    WebConfig webConfig() const;

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
    void reloadConversationHistoryList();
    QVector<ChatMessage> filteredConversationHistory() const;
    void exportConversationHistoryMarkdown();
    void exportConversationHistoryJson();
    void reloadCharacterList(const QString &selectedCharacterId = {});
    void importCharacterFolder();
    void deleteSelectedCharacter();
    void exportSelectedCharacter();
    void openUserCharacterDirectory();
    void reloadReminderList();
    void reloadReminderSoundList(const QString &selectedSoundId = {});
    QString selectedReminderSoundId() const;
    void updateReminderSoundButtons();
    void updateReminderActionButtons();
    void cancelSelectedReminder();
    void editSelectedReminder();
    void clearFinishedReminders();
    void importReminderSound();
    void deleteSelectedReminderSound();
    void testSelectedReminderSound();
    void testWeatherDefaultCity();
    void refreshWebCapabilityStatus();
    void testWebMode();

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
    QCheckBox *m_launchAtStartupCheckBox = nullptr;
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
    QLineEdit *m_historySearchEdit = nullptr;
    QComboBox *m_historyProviderFilterComboBox = nullptr;
    QComboBox *m_historyModelFilterComboBox = nullptr;
    QListWidget *m_historyListWidget = nullptr;
    QPushButton *m_exportHistoryMarkdownButton = nullptr;
    QPushButton *m_exportHistoryJsonButton = nullptr;
    QLineEdit *m_weatherDefaultCityEdit = nullptr;
    QCheckBox *m_weatherAutoLocateCheckBox = nullptr;
    QPushButton *m_testWeatherDefaultCityButton = nullptr;
    QLabel *m_weatherStatusLabel = nullptr;
    QLabel *m_webCapabilityStatusLabel = nullptr;
    QCheckBox *m_webEnabledCheckBox = nullptr;
    QCheckBox *m_webRememberToggleCheckBox = nullptr;
    QCheckBox *m_webDefaultOnCheckBox = nullptr;
    QComboBox *m_webProviderModeComboBox = nullptr;
    QSpinBox *m_webTimeoutSpinBox = nullptr;
    QCheckBox *m_webShowCitationsCheckBox = nullptr;
    QPushButton *m_testWebButton = nullptr;
    QLabel *m_webStatusLabel = nullptr;
    QComboBox *m_characterComboBox = nullptr;
    QPushButton *m_importCharacterButton = nullptr;
    QPushButton *m_deleteCharacterButton = nullptr;
    QPushButton *m_exportCharacterButton = nullptr;
    QPushButton *m_openUserCharacterDirButton = nullptr;
    QLabel *m_characterStatusLabel = nullptr;
    QComboBox *m_reminderStatusFilterComboBox = nullptr;
    QListWidget *m_reminderListWidget = nullptr;
    QPushButton *m_cancelReminderButton = nullptr;
    QPushButton *m_editReminderButton = nullptr;
    QPushButton *m_clearFinishedRemindersButton = nullptr;
    QLabel *m_reminderStatusLabel = nullptr;
    QCheckBox *m_reminderSoundEnabledCheckBox = nullptr;
    QSpinBox *m_reminderSoundVolumeSpinBox = nullptr;
    QComboBox *m_reminderSoundComboBox = nullptr;
    QPushButton *m_importReminderSoundButton = nullptr;
    QPushButton *m_deleteReminderSoundButton = nullptr;
    QPushButton *m_testReminderSoundButton = nullptr;
    QLabel *m_reminderSoundStatusLabel = nullptr;
    AIConfigStore m_configStore;
    AIConfigStore m_acceptedConfigStore;
    AppConfig m_appConfig;
    WeatherConfig m_weatherConfig;
    WebConfig m_webConfig;
    QVector<ChatMessage> m_conversationHistory;
    QVector<ReminderItem> m_reminders;
    QString m_currentProvider;
    std::function<bool()> m_aiTestBlocked;
    std::function<void()> m_clearConversationHistory;
    std::function<bool(const QString &, QString *)> m_cancelReminder;
    std::function<bool(const QString &, const QString &, const QDateTime &, const ReminderRecurrence &, ReminderItem *, QString *)> m_updateReminder;
    std::function<bool(QString *)> m_clearFinishedReminders;
    std::function<void(const QString &, double)> m_playReminderSound;
    std::unique_ptr<LLMProvider> m_testProvider;
    std::unique_ptr<WebChatManager> m_webTestManager;
    std::unique_ptr<WeatherManager> m_weatherTestManager;
    bool m_hasAcceptedConfigStore = false;
};
