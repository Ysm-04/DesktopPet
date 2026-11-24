#pragma once

#include "../agent_bridge/AgentHookManager.h"
#include "../config/AIConfig.h"
#include "../config/AppConfig.h"
#include "../ai/LLMTypes.h"
#include "../ai/LongTermMemoryTypes.h"
#include "../character/CharacterPersonaStore.h"
#include "../reminder/ReminderTypes.h"
#include "../web/WebConfig.h"
#include "../weather/WeatherConfig.h"

#include <QDialog>
#include <QMap>
#include <QVector>

#include <functional>
#include <memory>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QSpinBox;
class LLMProvider;
class WebChatManager;
class WeatherManager;

class SettingsDialog : public QDialog
{
public:
    struct LongTermMemoryCallbacks
    {
        std::function<bool(const QString &, LongTermMemoryCategory, LongTermMemoryItem *, QString *)> addItem;
        std::function<bool(const QString &, const QString &, LongTermMemoryCategory, LongTermMemoryItem *, QString *)> updateItem;
        std::function<bool(const QString &, QString *)> removeItem;
        std::function<bool(QString *)> clearAll;
    };

    explicit SettingsDialog(
        const AIConfigStore &configStore,
        const AppConfig &appConfig,
        const WeatherConfig &weatherConfig,
        const WebConfig &webConfig,
        const QVector<ChatMessage> &conversationHistory,
        const QVector<ReminderItem> &reminders,
        const QVector<LongTermMemoryItem> &longTermMemories,
        std::function<bool()> aiTestBlocked,
        std::function<void()> clearConversationHistoryCallback,
        std::function<bool(const QString &, QString *)> cancelReminderCallback,
        std::function<bool(const QString &, const QString &, const QDateTime &, const ReminderRecurrence &, ReminderItem *, QString *)> updateReminderCallback,
        std::function<bool(QString *)> clearFinishedRemindersCallback,
        std::function<void(const QString &, double)> playReminderSoundCallback,
        LongTermMemoryCallbacks longTermMemoryCallbacks,
        std::function<void(const QString &)> characterPersonaChangedCallback = {},
        QWidget *parent = nullptr);
    ~SettingsDialog() override;

    AIConfigStore aiConfigStore() const;
    AppConfig appConfig() const;
    WeatherConfig weatherConfig() const;
    WebConfig webConfig() const;

protected:
    void accept() override;
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct ProviderSnapshot
    {
        QString protocol;
        QString baseUrl;
        QString apiKey;
        QString model;
        QString path;
        int timeoutMs = 0;
        int temperatureHundredths = 0;
        int maxTokens = 0;
        bool allowPlainApiKey = false;
    };

    ProviderSnapshot providerSnapshotFromConfig(const QString &provider, const AIConfig &config) const;
    QMap<QString, ProviderSnapshot> currentProviderSnapshots() const;
    void captureInitialSettingsSnapshot();
    bool hasUnsavedChanges() const;
    void cacheCurrentProvider();
    void loadProviderConfig(const QString &provider);
    void switchProvider(const QString &provider);
    QString activeFormProtocol(const QString &provider) const;
    void updateCustomProtocolUi(const QString &provider, const QString &protocol);
    void handleCustomProtocolChanged();
    void updateResetProviderButtonText(const QString &provider);
    void resetCurrentProviderConfig();
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
    void reloadLongTermMemoryList();
    void updateLongTermMemoryButtons();
    void addLongTermMemory();
    void editSelectedLongTermMemory();
    void removeSelectedLongTermMemory();
    void clearAllLongTermMemories();
    void reloadCharacterList(const QString &selectedCharacterId = {});
    void importCharacterFolder();
    void deleteSelectedCharacter();
    void exportSelectedCharacter();
    void openUserCharacterDirectory();
    void loadSelectedCharacterPersona();
    void updateCharacterPersonaUiState();
    bool hasUnsavedCharacterPersonaDraft() const;
    bool saveSelectedCharacterPersona(bool showSuccessStatus = true);
    void resetSelectedCharacterPersona();
    bool handleCharacterSelectionChanged();
    bool confirmLeaveUnsavedCharacterPersonaDraft(const QString &actionDescription);
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
    void refreshAgentHookStatuses();
    void handleInstallOrFixAgentHook(AgentHookTarget target);
    void handleUninstallAgentHook(AgentHookTarget target);
    void openAgentTemplatesDirectory();
    void testAgentBridgeConnection();
    void setAgentHookActionStatus(const QString &message, const QString &state = QString());

    QComboBox *m_providerComboBox = nullptr;
    QLabel *m_protocolRowLabel = nullptr;
    QComboBox *m_protocolComboBox = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QSpinBox *m_timeoutSpinBox = nullptr;
    QDoubleSpinBox *m_temperatureSpinBox = nullptr;
    QSpinBox *m_maxTokensSpinBox = nullptr;
    QPushButton *m_testConnectionButton = nullptr;
    QPushButton *m_resetProviderButton = nullptr;
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
    QCheckBox *m_requestContextUseMaxCheckBox = nullptr;
    QSpinBox *m_requestContextMaxCharsSpinBox = nullptr;
    QSpinBox *m_memoryHistoryMessageLimitSpinBox = nullptr;
    QCheckBox *m_saveConversationHistoryCheckBox = nullptr;
    QCheckBox *m_saveAllConversationHistoryCheckBox = nullptr;
    QSpinBox *m_savedHistoryMessageLimitSpinBox = nullptr;
    QCheckBox *m_enableHistoryRecallCheckBox = nullptr;
    QPushButton *m_clearConversationHistoryButton = nullptr;
    QLabel *m_clearConversationStatusLabel = nullptr;
    QLineEdit *m_historySearchEdit = nullptr;
    QComboBox *m_historyProviderFilterComboBox = nullptr;
    QComboBox *m_historyModelFilterComboBox = nullptr;
    QListWidget *m_historyListWidget = nullptr;
    QPushButton *m_exportHistoryMarkdownButton = nullptr;
    QPushButton *m_exportHistoryJsonButton = nullptr;
    QCheckBox *m_enableLongTermMemoryCheckBox = nullptr;
    QCheckBox *m_autoExtractLongTermMemoryCheckBox = nullptr;
    QComboBox *m_memoryCategoryFilterComboBox = nullptr;
    QLineEdit *m_memorySearchEdit = nullptr;
    QListWidget *m_memoryListWidget = nullptr;
    QPushButton *m_addMemoryButton = nullptr;
    QPushButton *m_editMemoryButton = nullptr;
    QPushButton *m_removeMemoryButton = nullptr;
    QPushButton *m_clearMemoriesButton = nullptr;
    QLabel *m_memoryStatusLabel = nullptr;
    QComboBox *m_weatherProviderComboBox = nullptr;
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
    QLabel *m_characterPersonaSourceLabel = nullptr;
    QPlainTextEdit *m_characterPersonaEdit = nullptr;
    QLabel *m_characterPersonaCountLabel = nullptr;
    QPushButton *m_saveCharacterPersonaButton = nullptr;
    QPushButton *m_resetCharacterPersonaButton = nullptr;
    QLabel *m_characterPersonaStatusLabel = nullptr;
    QCheckBox *m_enableTouchInteractionCheckBox = nullptr;
    QCheckBox *m_enableCharacterEffectsCheckBox = nullptr;
    QCheckBox *m_enableStartupGreetingCheckBox = nullptr;
    QCheckBox *m_enableProactiveCareCheckBox = nullptr;
    QSpinBox *m_sedentaryReminderMinutesSpinBox = nullptr;
    QCheckBox *m_enableAgentBridgeCheckBox = nullptr;
    QCheckBox *m_showAgentStatusBarCheckBox = nullptr;
    QComboBox *m_agentBubbleNotifyLevelComboBox = nullptr;
    QCheckBox *m_agentAlertSoundEnabledCheckBox = nullptr;
    QLabel *m_codexHookNameLabel = nullptr;
    QLabel *m_codexHookStatusLabel = nullptr;
    QPushButton *m_codexHookInstallButton = nullptr;
    QPushButton *m_codexHookUninstallButton = nullptr;
    QLabel *m_antigravityHookStatusLabel = nullptr;
    QPushButton *m_antigravityHookInstallButton = nullptr;
    QPushButton *m_antigravityHookUninstallButton = nullptr;
    QPushButton *m_openDshTemplatesButton = nullptr;
    QPushButton *m_refreshAgentHookButton = nullptr;
    QPushButton *m_testAgentBridgeButton = nullptr;
    QLabel *m_agentHookActionStatusLabel = nullptr;
    QProcess *m_agentBridgeTestProcess = nullptr;
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
    QVector<LongTermMemoryItem> m_longTermMemories;
    QString m_currentProvider;
    std::function<bool()> m_aiTestBlocked;
    std::function<void()> m_clearConversationHistory;
    std::function<bool(const QString &, QString *)> m_cancelReminder;
    std::function<bool(const QString &, const QString &, const QDateTime &, const ReminderRecurrence &, ReminderItem *, QString *)> m_updateReminder;
    std::function<bool(QString *)> m_clearFinishedReminders;
    std::function<void(const QString &, double)> m_playReminderSound;
    LongTermMemoryCallbacks m_longTermMemoryCallbacks;
    std::function<void(const QString &)> m_characterPersonaChanged;
    std::unique_ptr<LLMProvider> m_testProvider;
    std::unique_ptr<WebChatManager> m_webTestManager;
    std::unique_ptr<WeatherManager> m_weatherTestManager;
    QString m_initialActiveProvider;
    QMap<QString, ProviderSnapshot> m_initialProviderSnapshots;
    AppConfig m_initialAppConfig;
    WeatherConfig m_initialWeatherConfig;
    WebConfig m_initialWebConfig;
    QString m_loadedPersonaCharacterId;
    QString m_savedCharacterPersona;
    QString m_savedPackagePersona;
    CharacterPersonaSource m_loadedPersonaSource = CharacterPersonaSource::ProgramDefault;
    bool m_loadedPersonaHasUserOverride = false;
    bool m_loadedPersonaIsUserPackage = false;
    bool m_suppressCharacterSelectionPrompt = false;
    bool m_hasAcceptedConfigStore = false;
};
