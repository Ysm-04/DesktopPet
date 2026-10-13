#include "SettingsDialog.h"

#include "../ai/AIProviderFactory.h"
#include "../ai/LLMTypes.h"
#include "../character/CharacterPackageRepository.h"
#include "../config/SecretStore.h"
#include "../reminder/ReminderSoundRepository.h"
#include "../web/WebCapabilityDetector.h"
#include "../web/WebChatManager.h"
#include "../weather/WeatherManager.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPair>
#include <QPointer>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <utility>

namespace
{
constexpr int ReminderHistoryRetentionDays = 20;

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

QString reminderStatusDisplayText(ReminderStatus status)
{
    switch (status)
    {
    case ReminderStatus::Pending:
        return QStringLiteral("待提醒");
    case ReminderStatus::Triggered:
        return QStringLiteral("已触发");
    case ReminderStatus::Canceled:
        return QStringLiteral("已取消");
    }

    return QStringLiteral("待提醒");
}

bool reminderMatchesStatusFilter(const ReminderItem &item, const QString &filter)
{
    if (filter == QStringLiteral("all"))
    {
        return true;
    }

    if (filter == QStringLiteral("pending"))
    {
        return item.status == ReminderStatus::Pending;
    }

    if (filter == QStringLiteral("triggered"))
    {
        return item.status == ReminderStatus::Triggered;
    }

    if (filter == QStringLiteral("canceled"))
    {
        return item.status == ReminderStatus::Canceled;
    }

    return item.status == ReminderStatus::Pending;
}

bool isFinishedReminder(const ReminderItem &item)
{
    return item.status == ReminderStatus::Triggered || item.status == ReminderStatus::Canceled;
}

QDateTime reminderFinishedReferenceTime(const ReminderItem &item)
{
    return item.finishedAt.isValid() ? item.finishedAt : item.remindAt;
}

bool hasPrunableFinishedReminders(const QVector<ReminderItem> &reminders, int retentionDays)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-qMax(0, retentionDays));
    for (const ReminderItem &item : reminders)
    {
        if (isFinishedReminder(item) && reminderFinishedReferenceTime(item) < cutoff)
        {
            return true;
        }
    }

    return false;
}

const ReminderItem *findReminderById(const QVector<ReminderItem> &reminders, const QString &id)
{
    for (const ReminderItem &item : reminders)
    {
        if (item.id == id)
        {
            return &item;
        }
    }

    return nullptr;
}

QString weatherLocationDisplayName(const WeatherLocation &location)
{
    QStringList parts;
    if (!location.cityName.trimmed().isEmpty())
    {
        parts.append(location.cityName.trimmed());
    }
    if (!location.adminName.trimmed().isEmpty() && location.adminName.trimmed() != location.cityName.trimmed())
    {
        parts.append(location.adminName.trimmed());
    }
    if (!location.countryName.trimmed().isEmpty() && location.countryName.trimmed() != location.adminName.trimmed())
    {
        parts.append(location.countryName.trimmed());
    }
    return parts.isEmpty() ? QStringLiteral("未知地点") : parts.join(QStringLiteral("，"));
}

QString weatherCandidateDisplayName(const WeatherLocationCandidate &candidate)
{
    QStringList parts;
    if (!candidate.cityName.trimmed().isEmpty())
    {
        parts.append(candidate.cityName.trimmed());
    }
    if (!candidate.adminName.trimmed().isEmpty() && candidate.adminName.trimmed() != candidate.cityName.trimmed())
    {
        parts.append(candidate.adminName.trimmed());
    }
    if (!candidate.countryName.trimmed().isEmpty() && candidate.countryName.trimmed() != candidate.adminName.trimmed())
    {
        parts.append(candidate.countryName.trimmed());
    }
    return parts.isEmpty() ? QStringLiteral("未知地点") : parts.join(QStringLiteral("，"));
}

QString weatherLocationTestDisplayText(const LocationTestResult &result)
{
    QString text = QStringLiteral("匹配结果：%1。").arg(weatherLocationDisplayName(result.selectedLocation));
    if (!result.hasAmbiguity || result.candidates.size() <= 1)
    {
        return text;
    }

    QStringList otherCandidates;
    const int maxOtherCandidateCount = qMin(3, result.candidates.size() - 1);
    for (int index = 1; index <= maxOtherCandidateCount; ++index)
    {
        otherCandidates.append(weatherCandidateDisplayName(result.candidates.at(index)));
    }

    if (!otherCandidates.isEmpty())
    {
        text += QStringLiteral(" 可能存在同名城市，其他候选：%1。").arg(otherCandidates.join(QStringLiteral("；")));
    }
    return text;
}

QString webCapabilityDisplayText(const WebCapability &capability)
{
    const QString state = capability.supported ? QStringLiteral("可用") : QStringLiteral("不可用");
    return QStringLiteral("当前联网能力：%1。%2").arg(state, capability.userMessage);
}

QString chatRoleDisplayText(const QString &role)
{
    if (role == QStringLiteral("user"))
    {
        return QStringLiteral("用户");
    }
    if (role == QStringLiteral("assistant"))
    {
        return QStringLiteral("桌宠");
    }
    if (role == QStringLiteral("system"))
    {
        return QStringLiteral("系统");
    }
    return role.trimmed().isEmpty() ? QStringLiteral("未知") : role.trimmed();
}

QString chatMetaText(const ChatMessage &message)
{
    QStringList parts;
    if (message.timestamp.isValid())
    {
        parts.append(message.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    if (!message.provider.trimmed().isEmpty())
    {
        parts.append(message.provider.trimmed());
    }
    if (!message.model.trimmed().isEmpty())
    {
        parts.append(message.model.trimmed());
    }
    return parts.join(QStringLiteral(" / "));
}

QString compactChatContent(const QString &content)
{
    QString compact = content.simplified();
    if (compact.size() > 120)
    {
        compact = compact.left(120) + QStringLiteral("...");
    }
    return compact;
}

QString markdownEscaped(QString text)
{
    text.replace(QLatin1Char('\r'), QString());
    return text;
}

QJsonObject jsonFromChatMessage(const ChatMessage &message)
{
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    object.insert(QStringLiteral("content"), message.content);
    if (message.timestamp.isValid())
    {
        object.insert(QStringLiteral("timestamp"), message.timestamp.toString(Qt::ISODate));
    }
    if (!message.provider.trimmed().isEmpty())
    {
        object.insert(QStringLiteral("provider"), message.provider.trimmed());
    }
    if (!message.model.trimmed().isEmpty())
    {
        object.insert(QStringLiteral("model"), message.model.trimmed());
    }
    return object;
}

bool writeBytesAtomically(const QString &filePath, const QByteArray &payload, QString *errorMessage)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法写入文件。");
        }
        return false;
    }

    const qint64 written = file.write(payload);
    if (written != payload.size())
    {
        file.cancelWriting();
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("写入文件不完整。");
        }
        return false;
    }

    if (!file.commit())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("提交文件失败。");
        }
        return false;
    }

    return true;
}

class ReminderEditDialog : public QDialog
{
public:
    explicit ReminderEditDialog(const ReminderItem &item, QWidget *parent = nullptr)
        : QDialog(parent)
        , m_titleEdit(new QLineEdit(this))
        , m_dateTimeEdit(new QDateTimeEdit(this))
        , m_recurrenceComboBox(new QComboBox(this))
        , m_weekdayComboBox(new QComboBox(this))
        , m_monthDaySpinBox(new QSpinBox(this))
    {
        setWindowTitle(QStringLiteral("编辑提醒"));
        setModal(true);
        resize(420, 240);

        m_titleEdit->setText(item.title);

        const QDateTime minimumDateTime = QDateTime::currentDateTime().addSecs(1);
        const QDateTime selectedDateTime = item.remindAt.isValid() && item.remindAt > minimumDateTime
            ? item.remindAt
            : minimumDateTime;
        m_dateTimeEdit->setCalendarPopup(true);
        m_dateTimeEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
        m_dateTimeEdit->setMinimumDateTime(minimumDateTime);
        m_dateTimeEdit->setDateTime(selectedDateTime);

        m_recurrenceComboBox->addItem(QStringLiteral("一次"), reminderRecurrenceTypeToString(ReminderRecurrenceType::None));
        m_recurrenceComboBox->addItem(QStringLiteral("每天"), reminderRecurrenceTypeToString(ReminderRecurrenceType::Daily));
        m_recurrenceComboBox->addItem(QStringLiteral("每周"), reminderRecurrenceTypeToString(ReminderRecurrenceType::Weekly));
        m_recurrenceComboBox->addItem(QStringLiteral("每月"), reminderRecurrenceTypeToString(ReminderRecurrenceType::Monthly));
        const int recurrenceIndex = m_recurrenceComboBox->findData(reminderRecurrenceTypeToString(item.recurrence.type));
        m_recurrenceComboBox->setCurrentIndex(recurrenceIndex >= 0 ? recurrenceIndex : 0);

        m_weekdayComboBox->addItem(QStringLiteral("周一"), 1);
        m_weekdayComboBox->addItem(QStringLiteral("周二"), 2);
        m_weekdayComboBox->addItem(QStringLiteral("周三"), 3);
        m_weekdayComboBox->addItem(QStringLiteral("周四"), 4);
        m_weekdayComboBox->addItem(QStringLiteral("周五"), 5);
        m_weekdayComboBox->addItem(QStringLiteral("周六"), 6);
        m_weekdayComboBox->addItem(QStringLiteral("周日"), 7);
        const int weekday = item.recurrence.weekday >= 1 && item.recurrence.weekday <= 7
            ? item.recurrence.weekday
            : selectedDateTime.date().dayOfWeek();
        const int weekdayIndex = m_weekdayComboBox->findData(weekday);
        m_weekdayComboBox->setCurrentIndex(weekdayIndex >= 0 ? weekdayIndex : 0);

        m_monthDaySpinBox->setRange(1, 31);
        m_monthDaySpinBox->setValue(item.recurrence.monthDay >= 1 && item.recurrence.monthDay <= 31
            ? item.recurrence.monthDay
            : selectedDateTime.date().day());

        auto *formLayout = new QFormLayout();
        formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        formLayout->addRow(QStringLiteral("标题"), m_titleEdit);
        formLayout->addRow(QStringLiteral("下一次时间"), m_dateTimeEdit);
        formLayout->addRow(QStringLiteral("重复"), m_recurrenceComboBox);
        formLayout->addRow(QStringLiteral("每周"), m_weekdayComboBox);
        formLayout->addRow(QStringLiteral("每月日期"), m_monthDaySpinBox);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
            accept();
        });
        connect(buttonBox, &QDialogButtonBox::rejected, this, [this]() {
            reject();
        });
        connect(m_recurrenceComboBox, &QComboBox::currentIndexChanged, this, [this]() {
            updateRecurrenceControls();
        });

        auto *layout = new QVBoxLayout(this);
        layout->addLayout(formLayout);
        layout->addWidget(buttonBox);
        updateRecurrenceControls();
    }

    QString title() const
    {
        return m_titleEdit->text().trimmed();
    }

    QDateTime remindAt() const
    {
        return m_dateTimeEdit->dateTime();
    }

    ReminderRecurrence recurrence() const
    {
        ReminderRecurrence recurrence;
        recurrence.type = reminderRecurrenceTypeFromString(m_recurrenceComboBox->currentData().toString());
        if (recurrence.type == ReminderRecurrenceType::None)
        {
            return {};
        }

        const QTime time = m_dateTimeEdit->dateTime().time();
        recurrence.interval = 1;
        recurrence.hour = time.hour();
        recurrence.minute = time.minute();
        if (recurrence.type == ReminderRecurrenceType::Weekly)
        {
            recurrence.weekday = m_weekdayComboBox->currentData().toInt();
        }
        else if (recurrence.type == ReminderRecurrenceType::Monthly)
        {
            recurrence.monthDay = m_monthDaySpinBox->value();
        }

        return recurrence;
    }

protected:
    void accept() override
    {
        const ReminderRecurrenceType type = reminderRecurrenceTypeFromString(m_recurrenceComboBox->currentData().toString());
        if (type == ReminderRecurrenceType::None && m_dateTimeEdit->dateTime() <= QDateTime::currentDateTime())
        {
            QMessageBox::warning(this, QStringLiteral("编辑提醒"), QStringLiteral("提醒时间必须晚于当前时间。"));
            return;
        }

        QDialog::accept();
    }

private:
    void updateRecurrenceControls()
    {
        const ReminderRecurrenceType type = reminderRecurrenceTypeFromString(m_recurrenceComboBox->currentData().toString());
        m_weekdayComboBox->setEnabled(type == ReminderRecurrenceType::Weekly);
        m_monthDaySpinBox->setEnabled(type == ReminderRecurrenceType::Monthly);
    }

    QLineEdit *m_titleEdit = nullptr;
    QDateTimeEdit *m_dateTimeEdit = nullptr;
    QComboBox *m_recurrenceComboBox = nullptr;
    QComboBox *m_weekdayComboBox = nullptr;
    QSpinBox *m_monthDaySpinBox = nullptr;
};
}

SettingsDialog::SettingsDialog(
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
    , m_launchAtStartupCheckBox(new QCheckBox(QStringLiteral("开机自启动"), this))
    , m_scaleSpinBox(new QSpinBox(this))
    , m_performanceModeComboBox(new QComboBox(this))
    , m_pauseWhenHiddenCheckBox(new QCheckBox(QStringLiteral("隐藏到托盘时暂停动画"), this))
    , m_enableLazyLoadCheckBox(new QCheckBox(QStringLiteral("启用动画懒加载"), this))
    , m_enableAnimationPrewarmCheckBox(new QCheckBox(QStringLiteral("启动后预热常用动画"), this))
    , m_animationCacheLimitSpinBox(new QSpinBox(this))
    , m_unloadAnimationsWhenHiddenCheckBox(new QCheckBox(QStringLiteral("隐藏时释放动画缓存"), this))
    , m_requestContextMessageLimitSpinBox(new QSpinBox(this))
    , m_memoryHistoryMessageLimitSpinBox(new QSpinBox(this))
    , m_saveConversationHistoryCheckBox(new QCheckBox(QStringLiteral("保存聊天记录到本地"), this))
    , m_savedHistoryMessageLimitSpinBox(new QSpinBox(this))
    , m_clearConversationHistoryButton(new QPushButton(QStringLiteral("清空聊天记录"), this))
    , m_historySearchEdit(new QLineEdit(this))
    , m_historyProviderFilterComboBox(new QComboBox(this))
    , m_historyModelFilterComboBox(new QComboBox(this))
    , m_historyListWidget(new QListWidget(this))
    , m_exportHistoryMarkdownButton(new QPushButton(QStringLiteral("导出 Markdown"), this))
    , m_exportHistoryJsonButton(new QPushButton(QStringLiteral("导出 JSON"), this))
    , m_weatherDefaultCityEdit(new QLineEdit(this))
    , m_weatherAutoLocateCheckBox(new QCheckBox(QStringLiteral("无默认城市时根据公网 IP 判断城市"), this))
    , m_testWeatherDefaultCityButton(new QPushButton(QStringLiteral("测试默认城市"), this))
    , m_weatherStatusLabel(new QLabel(this))
    , m_webCapabilityStatusLabel(new QLabel(this))
    , m_webEnabledCheckBox(new QCheckBox(QStringLiteral("启用联网模式"), this))
    , m_webRememberToggleCheckBox(new QCheckBox(QStringLiteral("记住输入框联网开关状态"), this))
    , m_webDefaultOnCheckBox(new QCheckBox(QStringLiteral("默认打开输入框联网开关"), this))
    , m_webProviderModeComboBox(new QComboBox(this))
    , m_webTimeoutSpinBox(new QSpinBox(this))
    , m_webShowCitationsCheckBox(new QCheckBox(QStringLiteral("显示来源引用"), this))
    , m_testWebButton(new QPushButton(QStringLiteral("测试联网模式"), this))
    , m_webStatusLabel(new QLabel(this))
    , m_characterComboBox(new QComboBox(this))
    , m_importCharacterButton(new QPushButton(QStringLiteral("导入角色文件夹"), this))
    , m_deleteCharacterButton(new QPushButton(QStringLiteral("删除角色"), this))
    , m_exportCharacterButton(new QPushButton(QStringLiteral("导出角色"), this))
    , m_openUserCharacterDirButton(new QPushButton(QStringLiteral("打开用户角色目录"), this))
    , m_characterStatusLabel(new QLabel(this))
    , m_reminderStatusFilterComboBox(new QComboBox(this))
    , m_reminderListWidget(new QListWidget(this))
    , m_cancelReminderButton(new QPushButton(QStringLiteral("取消选中提醒"), this))
    , m_editReminderButton(new QPushButton(QStringLiteral("编辑选中提醒"), this))
    , m_clearFinishedRemindersButton(new QPushButton(QStringLiteral("清理20天前历史"), this))
    , m_reminderStatusLabel(new QLabel(this))
    , m_reminderSoundEnabledCheckBox(new QCheckBox(QStringLiteral("启用提醒音效"), this))
    , m_reminderSoundVolumeSpinBox(new QSpinBox(this))
    , m_reminderSoundComboBox(new QComboBox(this))
    , m_importReminderSoundButton(new QPushButton(QStringLiteral("导入音效"), this))
    , m_deleteReminderSoundButton(new QPushButton(QStringLiteral("删除音效"), this))
    , m_testReminderSoundButton(new QPushButton(QStringLiteral("试听"), this))
    , m_reminderSoundStatusLabel(new QLabel(this))
    , m_configStore(configStore)
    , m_appConfig(appConfig)
    , m_weatherConfig(weatherConfig)
    , m_webConfig(webConfig)
    , m_conversationHistory(conversationHistory)
    , m_reminders(reminders)
    , m_aiTestBlocked(std::move(aiTestBlocked))
    , m_clearConversationHistory(std::move(clearConversationHistoryCallback))
    , m_cancelReminder(std::move(cancelReminderCallback))
    , m_updateReminder(std::move(updateReminderCallback))
    , m_clearFinishedReminders(std::move(clearFinishedRemindersCallback))
    , m_playReminderSound(std::move(playReminderSoundCallback))
    , m_webTestManager(std::make_unique<WebChatManager>())
    , m_weatherTestManager(std::make_unique<WeatherManager>())
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
    m_launchAtStartupCheckBox->setChecked(m_appConfig.launchAtStartup);

    m_performanceModeComboBox->addItem(QStringLiteral("标准"), QStringLiteral("standard"));
    m_performanceModeComboBox->addItem(QStringLiteral("低功耗"), QStringLiteral("low-power"));
    const int performanceModeIndex = m_performanceModeComboBox->findData(m_appConfig.performanceMode);
    m_performanceModeComboBox->setCurrentIndex(performanceModeIndex >= 0 ? performanceModeIndex : 0);

    m_pauseWhenHiddenCheckBox->setChecked(m_appConfig.pauseWhenHidden);
    m_enableLazyLoadCheckBox->setChecked(m_appConfig.enableLazyLoad);
    m_enableAnimationPrewarmCheckBox->setChecked(m_appConfig.enableAnimationPrewarm);
    m_animationCacheLimitSpinBox->setRange(64, 1024);
    m_animationCacheLimitSpinBox->setSingleStep(16);
    m_animationCacheLimitSpinBox->setSuffix(QStringLiteral(" MB"));
    m_animationCacheLimitSpinBox->setValue(qBound(64, m_appConfig.animationCacheLimitMb, 1024));
    m_unloadAnimationsWhenHiddenCheckBox->setChecked(m_appConfig.unloadAnimationsWhenHidden);

    m_requestContextMessageLimitSpinBox->setRange(0, 200);
    m_requestContextMessageLimitSpinBox->setValue(qBound(0, m_appConfig.requestContextMessageLimit, 200));

    m_memoryHistoryMessageLimitSpinBox->setRange(2, 5000);
    m_memoryHistoryMessageLimitSpinBox->setSingleStep(2);
    m_memoryHistoryMessageLimitSpinBox->setValue(qBound(2, m_appConfig.memoryHistoryMessageLimit, 5000));

    m_saveConversationHistoryCheckBox->setChecked(m_appConfig.saveConversationHistory);

    m_savedHistoryMessageLimitSpinBox->setRange(2, 10000);
    m_savedHistoryMessageLimitSpinBox->setSingleStep(2);
    m_savedHistoryMessageLimitSpinBox->setValue(qBound(2, m_appConfig.savedHistoryMessageLimit, 10000));
    m_savedHistoryMessageLimitSpinBox->setEnabled(m_saveConversationHistoryCheckBox->isChecked());

    m_clearConversationStatusLabel = new QLabel(this);
    m_clearConversationStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_clearConversationStatusLabel->setWordWrap(true);

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
    appFormLayout->addRow(QString(), m_launchAtStartupCheckBox);
    appFormLayout->addRow(QStringLiteral("性能模式"), m_performanceModeComboBox);
    appFormLayout->addRow(QString(), m_pauseWhenHiddenCheckBox);
    appFormLayout->addRow(QString(), m_enableLazyLoadCheckBox);
    appFormLayout->addRow(QString(), m_enableAnimationPrewarmCheckBox);
    appFormLayout->addRow(QStringLiteral("动画缓存上限"), m_animationCacheLimitSpinBox);
    appFormLayout->addRow(QString(), m_unloadAnimationsWhenHiddenCheckBox);

    auto *appPageLayout = new QVBoxLayout();
    appPageLayout->setContentsMargins(24, 24, 24, 24);
    appPageLayout->setSpacing(16);
    appPageLayout->addWidget(appTitleLabel);
    appPageLayout->addLayout(appFormLayout);
    appPageLayout->addStretch();

    auto *appPage = new QWidget(this);
    appPage->setLayout(appPageLayout);

    auto *chatTitleLabel = new QLabel(QStringLiteral("聊天记录"), this);
    chatTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *chatHintLabel = new QLabel(QStringLiteral("聊天记录默认只保存在内存中；开启本地保存后会写入用户配置目录。"), this);
    chatHintLabel->setObjectName(QStringLiteral("HintLabel"));
    chatHintLabel->setWordWrap(true);

    auto *chatFormLayout = new QFormLayout();
    chatFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    chatFormLayout->setLabelAlignment(Qt::AlignRight);
    chatFormLayout->setFormAlignment(Qt::AlignTop);
    chatFormLayout->setHorizontalSpacing(18);
    chatFormLayout->setVerticalSpacing(12);
    chatFormLayout->addRow(QStringLiteral("请求上下文上限"), m_requestContextMessageLimitSpinBox);
    chatFormLayout->addRow(QStringLiteral("内存历史上限"), m_memoryHistoryMessageLimitSpinBox);
    chatFormLayout->addRow(QString(), m_saveConversationHistoryCheckBox);
    chatFormLayout->addRow(QStringLiteral("本地保存上限"), m_savedHistoryMessageLimitSpinBox);

    auto *clearHistoryLayout = new QHBoxLayout();
    clearHistoryLayout->addWidget(m_clearConversationHistoryButton);
    clearHistoryLayout->addWidget(m_clearConversationStatusLabel, 1);

    m_historySearchEdit->setPlaceholderText(QStringLiteral("搜索聊天内容"));
    m_historyProviderFilterComboBox->addItem(QStringLiteral("全部 Provider"), QStringLiteral("all"));
    m_historyModelFilterComboBox->addItem(QStringLiteral("全部模型"), QStringLiteral("all"));
    QSet<QString> historyProviders;
    QSet<QString> historyModels;
    for (const ChatMessage &message : m_conversationHistory)
    {
        if (!message.provider.trimmed().isEmpty())
        {
            historyProviders.insert(message.provider.trimmed());
        }
        if (!message.model.trimmed().isEmpty())
        {
            historyModels.insert(message.model.trimmed());
        }
    }
    QStringList sortedProviders = historyProviders.values();
    QStringList sortedModels = historyModels.values();
    sortedProviders.sort(Qt::CaseInsensitive);
    sortedModels.sort(Qt::CaseInsensitive);
    for (const QString &provider : sortedProviders)
    {
        m_historyProviderFilterComboBox->addItem(provider, provider);
    }
    for (const QString &model : sortedModels)
    {
        m_historyModelFilterComboBox->addItem(model, model);
    }
    m_historyListWidget->setFrameShape(QFrame::NoFrame);
    m_historyListWidget->setMinimumHeight(150);
    m_historyListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    reloadConversationHistoryList();

    auto *historyFilterLayout = new QHBoxLayout();
    historyFilterLayout->addWidget(m_historySearchEdit, 2);
    historyFilterLayout->addWidget(m_historyProviderFilterComboBox);
    historyFilterLayout->addWidget(m_historyModelFilterComboBox);

    auto *historyExportLayout = new QHBoxLayout();
    historyExportLayout->addWidget(m_exportHistoryMarkdownButton);
    historyExportLayout->addWidget(m_exportHistoryJsonButton);
    historyExportLayout->addStretch();

    auto *chatPageLayout = new QVBoxLayout();
    chatPageLayout->setContentsMargins(24, 24, 24, 24);
    chatPageLayout->setSpacing(16);
    chatPageLayout->addWidget(chatTitleLabel);
    chatPageLayout->addWidget(chatHintLabel);
    chatPageLayout->addLayout(chatFormLayout);
    chatPageLayout->addLayout(clearHistoryLayout);
    chatPageLayout->addLayout(historyFilterLayout);
    chatPageLayout->addWidget(m_historyListWidget);
    chatPageLayout->addLayout(historyExportLayout);
    chatPageLayout->addStretch();

    auto *chatPage = new QWidget(this);
    chatPage->setLayout(chatPageLayout);

    m_weatherStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_weatherStatusLabel->setWordWrap(true);
    m_weatherStatusLabel->setText(QStringLiteral("天气源：Open-Meteo。当前建议填写市级城市名；区县、乡镇、街道可能无法识别，或被匹配到上级/同名城市。没有默认城市时，可按公网 IP 判断城市；回复会明确说明城市来源。"));
    m_weatherDefaultCityEdit->setText(m_weatherConfig.defaultCityName);
    m_weatherDefaultCityEdit->setPlaceholderText(QStringLiteral("例如：西安 / 北京 / New York；区县乡镇不保证精确"));
    m_weatherAutoLocateCheckBox->setChecked(m_weatherConfig.autoLocateWhenNoDefault);

    auto *weatherTitleLabel = new QLabel(QStringLiteral("天气"), this);
    weatherTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *weatherHintLabel = new QLabel(QStringLiteral("天气查询使用 Open-Meteo，无需 API Key。当前使用基础地理编码，推荐填写市级城市名。默认城市为空时，可通过公网 IP 定位推断城市。"), this);
    weatherHintLabel->setObjectName(QStringLiteral("HintLabel"));
    weatherHintLabel->setWordWrap(true);

    auto *weatherFormLayout = new QFormLayout();
    weatherFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    weatherFormLayout->setLabelAlignment(Qt::AlignRight);
    weatherFormLayout->setFormAlignment(Qt::AlignTop);
    weatherFormLayout->setHorizontalSpacing(18);
    weatherFormLayout->setVerticalSpacing(12);
    weatherFormLayout->addRow(QStringLiteral("默认城市（建议市级）"), m_weatherDefaultCityEdit);
    weatherFormLayout->addRow(QString(), m_weatherAutoLocateCheckBox);

    auto *weatherActionLayout = new QHBoxLayout();
    weatherActionLayout->addWidget(m_testWeatherDefaultCityButton);
    weatherActionLayout->addWidget(m_weatherStatusLabel, 1);

    auto *weatherPageLayout = new QVBoxLayout();
    weatherPageLayout->setContentsMargins(24, 24, 24, 24);
    weatherPageLayout->setSpacing(16);
    weatherPageLayout->addWidget(weatherTitleLabel);
    weatherPageLayout->addWidget(weatherHintLabel);
    weatherPageLayout->addLayout(weatherFormLayout);
    weatherPageLayout->addLayout(weatherActionLayout);
    weatherPageLayout->addStretch();

    auto *weatherPage = new QWidget(this);
    weatherPage->setLayout(weatherPageLayout);

    m_webCapabilityStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_webCapabilityStatusLabel->setWordWrap(true);
    m_webStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_webStatusLabel->setWordWrap(true);
    m_webStatusLabel->setText(QStringLiteral("联网模式使用当前 AI Provider 的原生联网能力；OpenAI 官方和 Gemini 官方可用，DeepSeek 与第三方兼容接口默认不支持。"));
    m_webEnabledCheckBox->setChecked(m_webConfig.enabled);
    m_webRememberToggleCheckBox->setChecked(m_webConfig.rememberLastToggle);
    m_webDefaultOnCheckBox->setChecked(m_webConfig.defaultToggleOn);
    m_webProviderModeComboBox->addItem(QStringLiteral("自动"), QStringLiteral("auto"));
    m_webProviderModeComboBox->addItem(QStringLiteral("仅 OpenAI 原生联网"), QStringLiteral("openai"));
    m_webProviderModeComboBox->addItem(QStringLiteral("仅 Gemini 原生联网"), QStringLiteral("gemini"));
    const int webProviderModeIndex = m_webProviderModeComboBox->findData(m_webConfig.providerMode);
    m_webProviderModeComboBox->setCurrentIndex(webProviderModeIndex >= 0 ? webProviderModeIndex : 0);
    m_webTimeoutSpinBox->setRange(3000, 120000);
    m_webTimeoutSpinBox->setSingleStep(1000);
    m_webTimeoutSpinBox->setSuffix(QStringLiteral(" ms"));
    m_webTimeoutSpinBox->setValue(qBound(3000, m_webConfig.timeoutMs, 120000));
    m_webShowCitationsCheckBox->setChecked(m_webConfig.showCitations);

    auto *webTitleLabel = new QLabel(QStringLiteral("联网模式"), this);
    webTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *webHintLabel = new QLabel(QStringLiteral("普通用户只需要在输入框打开“联网”开关。当前模型没有原生联网能力时，会直接提示不可用，不再使用旧搜索引擎页面解析。"), this);
    webHintLabel->setObjectName(QStringLiteral("HintLabel"));
    webHintLabel->setWordWrap(true);

    auto *webFormLayout = new QFormLayout();
    webFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    webFormLayout->setLabelAlignment(Qt::AlignRight);
    webFormLayout->setFormAlignment(Qt::AlignTop);
    webFormLayout->setHorizontalSpacing(18);
    webFormLayout->setVerticalSpacing(12);
    webFormLayout->addRow(QString(), m_webEnabledCheckBox);
    webFormLayout->addRow(QString(), m_webRememberToggleCheckBox);
    webFormLayout->addRow(QString(), m_webDefaultOnCheckBox);
    webFormLayout->addRow(QStringLiteral("Provider 模式"), m_webProviderModeComboBox);
    webFormLayout->addRow(QStringLiteral("超时"), m_webTimeoutSpinBox);
    webFormLayout->addRow(QString(), m_webShowCitationsCheckBox);

    auto *webActionLayout = new QHBoxLayout();
    webActionLayout->addWidget(m_testWebButton);
    webActionLayout->addWidget(m_webStatusLabel, 1);

    auto *webPageLayout = new QVBoxLayout();
    webPageLayout->setContentsMargins(24, 24, 24, 24);
    webPageLayout->setSpacing(16);
    webPageLayout->addWidget(webTitleLabel);
    webPageLayout->addWidget(webHintLabel);
    webPageLayout->addWidget(m_webCapabilityStatusLabel);
    webPageLayout->addLayout(webFormLayout);
    webPageLayout->addLayout(webActionLayout);
    webPageLayout->addStretch();

    auto *webPage = new QWidget(this);
    webPage->setLayout(webPageLayout);


    m_reminderStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_reminderStatusLabel->setWordWrap(true);
    m_reminderSoundStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_reminderSoundStatusLabel->setWordWrap(true);

    m_reminderStatusFilterComboBox->addItem(QStringLiteral("待提醒"), QStringLiteral("pending"));
    m_reminderStatusFilterComboBox->addItem(QStringLiteral("已触发"), QStringLiteral("triggered"));
    m_reminderStatusFilterComboBox->addItem(QStringLiteral("已取消"), QStringLiteral("canceled"));
    m_reminderStatusFilterComboBox->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    m_reminderListWidget->setObjectName(QStringLiteral("ReminderList"));
    m_reminderListWidget->setFrameShape(QFrame::NoFrame);
    m_reminderListWidget->setMinimumHeight(150);
    m_reminderListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_reminderSoundEnabledCheckBox->setChecked(m_appConfig.reminderSoundEnabled);
    m_reminderSoundVolumeSpinBox->setRange(0, 100);
    m_reminderSoundVolumeSpinBox->setSingleStep(5);
    m_reminderSoundVolumeSpinBox->setSuffix(QStringLiteral("%"));
    m_reminderSoundVolumeSpinBox->setValue(qRound(qBound(0.0, m_appConfig.reminderSoundVolume, 1.0) * 100.0));
    reloadReminderList();
    reloadReminderSoundList(m_appConfig.reminderSoundId);

    auto *reminderTitleLabel = new QLabel(QStringLiteral("提醒"), this);
    reminderTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *reminderHintLabel = new QLabel(QStringLiteral("这里显示通过聊天创建的提醒，重复提醒会展示下一次触发时间。内置提醒音效不可删除，导入音效仅支持 PCM wav。"), this);
    reminderHintLabel->setObjectName(QStringLiteral("HintLabel"));
    reminderHintLabel->setWordWrap(true);

    auto *reminderListLayout = new QVBoxLayout();
    reminderListLayout->setSpacing(8);
    auto *reminderFilterLayout = new QHBoxLayout();
    reminderFilterLayout->addWidget(new QLabel(QStringLiteral("状态"), this));
    reminderFilterLayout->addWidget(m_reminderStatusFilterComboBox);
    reminderFilterLayout->addStretch();
    reminderListLayout->addLayout(reminderFilterLayout);
    reminderListLayout->addWidget(m_reminderListWidget);
    auto *reminderActionLayout = new QHBoxLayout();
    reminderActionLayout->addWidget(m_cancelReminderButton);
    reminderActionLayout->addWidget(m_editReminderButton);
    reminderActionLayout->addWidget(m_clearFinishedRemindersButton);
    reminderActionLayout->addWidget(m_reminderStatusLabel, 1);
    reminderListLayout->addLayout(reminderActionLayout);

    auto *reminderSoundFormLayout = new QFormLayout();
    reminderSoundFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    reminderSoundFormLayout->setLabelAlignment(Qt::AlignRight);
    reminderSoundFormLayout->setHorizontalSpacing(18);
    reminderSoundFormLayout->setVerticalSpacing(12);
    reminderSoundFormLayout->addRow(QString(), m_reminderSoundEnabledCheckBox);
    reminderSoundFormLayout->addRow(QStringLiteral("当前音效"), m_reminderSoundComboBox);
    reminderSoundFormLayout->addRow(QStringLiteral("音量"), m_reminderSoundVolumeSpinBox);

    auto *reminderSoundActionLayout = new QHBoxLayout();
    reminderSoundActionLayout->addWidget(m_importReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_deleteReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_testReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_reminderSoundStatusLabel, 1);

    auto *reminderPageLayout = new QVBoxLayout();
    reminderPageLayout->setContentsMargins(24, 24, 24, 24);
    reminderPageLayout->setSpacing(16);
    reminderPageLayout->addWidget(reminderTitleLabel);
    reminderPageLayout->addWidget(reminderHintLabel);
    reminderPageLayout->addLayout(reminderListLayout);
    reminderPageLayout->addLayout(reminderSoundFormLayout);
    reminderPageLayout->addLayout(reminderSoundActionLayout);
    reminderPageLayout->addStretch();

    auto *reminderPage = new QWidget(this);
    reminderPage->setLayout(reminderPageLayout);

    m_characterStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_characterStatusLabel->setWordWrap(true);
    reloadCharacterList(m_appConfig.characterId);

    auto *characterTitleLabel = new QLabel(QStringLiteral("角色"), this);
    characterTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *characterHintLabel = new QLabel(QStringLiteral("支持导入本地角色文件夹。导入前会先验证角色包，验证失败不会复制或覆盖任何文件。"), this);
    characterHintLabel->setObjectName(QStringLiteral("HintLabel"));
    characterHintLabel->setWordWrap(true);

    auto *characterFormLayout = new QFormLayout();
    characterFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    characterFormLayout->setLabelAlignment(Qt::AlignRight);
    characterFormLayout->setHorizontalSpacing(18);
    characterFormLayout->setVerticalSpacing(12);
    characterFormLayout->addRow(QStringLiteral("当前角色"), m_characterComboBox);

    auto *characterActionLayout = new QHBoxLayout();
    characterActionLayout->addWidget(m_importCharacterButton);
    characterActionLayout->addWidget(m_deleteCharacterButton);
    characterActionLayout->addWidget(m_exportCharacterButton);
    characterActionLayout->addWidget(m_openUserCharacterDirButton);
    characterActionLayout->addWidget(m_characterStatusLabel, 1);

    auto *characterPageLayout = new QVBoxLayout();
    characterPageLayout->setContentsMargins(24, 24, 24, 24);
    characterPageLayout->setSpacing(16);
    characterPageLayout->addWidget(characterTitleLabel);
    characterPageLayout->addLayout(characterFormLayout);
    characterPageLayout->addLayout(characterActionLayout);
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
    navigationList->addItem(QStringLiteral("聊天"));
    navigationList->addItem(QStringLiteral("天气"));
    navigationList->addItem(QStringLiteral("联网"));
    navigationList->addItem(QStringLiteral("提醒"));
    navigationList->addItem(QStringLiteral("应用"));
    navigationList->addItem(QStringLiteral("角色"));

    auto *pageStack = new QStackedWidget(this);
    pageStack->setObjectName(QStringLiteral("SettingsPages"));
    pageStack->addWidget(aiPage);
    pageStack->addWidget(chatPage);
    pageStack->addWidget(weatherPage);
    pageStack->addWidget(webPage);
    pageStack->addWidget(reminderPage);
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
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
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
        "QListWidget#ReminderList {"
        "  background: #ffffff; border: 1px solid #98a2b3; border-radius: 0px;"
        "  padding: 0px;"
        "}"
        "QListWidget#ReminderList::item {"
        "  min-height: 28px; padding: 5px 8px; color: #344054;"
        "}"
        "QListWidget#ReminderList::item:selected {"
        "  background: #eaf3ff; color: #175cd3;"
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
    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_testConnectionButton, &QPushButton::clicked, this, [this]() {
        testConnection();
    });
    const auto updateAnimationCacheControls = [this](bool enabled) {
        m_enableAnimationPrewarmCheckBox->setEnabled(enabled);
        m_animationCacheLimitSpinBox->setEnabled(enabled);
        m_unloadAnimationsWhenHiddenCheckBox->setEnabled(enabled);
    };
    updateAnimationCacheControls(m_enableLazyLoadCheckBox->isChecked());
    connect(m_enableLazyLoadCheckBox, &QCheckBox::toggled, this, updateAnimationCacheControls);
    connect(m_saveConversationHistoryCheckBox, &QCheckBox::toggled, m_savedHistoryMessageLimitSpinBox, &QSpinBox::setEnabled);
    connect(m_clearConversationHistoryButton, &QPushButton::clicked, this, [this]() {
        this->clearConversationHistory();
    });
    connect(m_historySearchEdit, &QLineEdit::textChanged, this, [this]() {
        reloadConversationHistoryList();
    });
    connect(m_historyProviderFilterComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        reloadConversationHistoryList();
    });
    connect(m_historyModelFilterComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        reloadConversationHistoryList();
    });
    connect(m_exportHistoryMarkdownButton, &QPushButton::clicked, this, [this]() {
        exportConversationHistoryMarkdown();
    });
    connect(m_exportHistoryJsonButton, &QPushButton::clicked, this, [this]() {
        exportConversationHistoryJson();
    });
    connect(m_testWeatherDefaultCityButton, &QPushButton::clicked, this, [this]() {
        testWeatherDefaultCity();
    });
    connect(m_webEnabledCheckBox, &QCheckBox::toggled, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_webProviderModeComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_testWebButton, &QPushButton::clicked, this, [this]() {
        testWebMode();
    });
    connect(m_importCharacterButton, &QPushButton::clicked, this, [this]() {
        importCharacterFolder();
    });
    connect(m_deleteCharacterButton, &QPushButton::clicked, this, [this]() {
        deleteSelectedCharacter();
    });
    connect(m_exportCharacterButton, &QPushButton::clicked, this, [this]() {
        exportSelectedCharacter();
    });
    connect(m_openUserCharacterDirButton, &QPushButton::clicked, this, [this]() {
        openUserCharacterDirectory();
    });
    connect(m_reminderStatusFilterComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        reloadReminderList();
    });
    connect(m_reminderListWidget, &QListWidget::currentItemChanged, this, [this]() {
        updateReminderActionButtons();
    });
    connect(m_cancelReminderButton, &QPushButton::clicked, this, [this]() {
        cancelSelectedReminder();
    });
    connect(m_editReminderButton, &QPushButton::clicked, this, [this]() {
        editSelectedReminder();
    });
    connect(m_clearFinishedRemindersButton, &QPushButton::clicked, this, [this]() {
        clearFinishedReminders();
    });
    connect(m_reminderSoundComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        updateReminderSoundButtons();
    });
    connect(m_importReminderSoundButton, &QPushButton::clicked, this, [this]() {
        importReminderSound();
    });
    connect(m_deleteReminderSoundButton, &QPushButton::clicked, this, [this]() {
        deleteSelectedReminderSound();
    });
    connect(m_testReminderSoundButton, &QPushButton::clicked, this, [this]() {
        testSelectedReminderSound();
    });
    updateReminderActionButtons();
    updateReminderSoundButtons();
    refreshWebCapabilityStatus();
}

SettingsDialog::~SettingsDialog()
{
    if (m_testProvider)
    {
        m_testProvider->cancel();
    }
    if (m_webTestManager)
    {
        m_webTestManager->cancel();
    }
    if (m_weatherTestManager)
    {
        m_weatherTestManager->cancel();
    }
}

AIConfigStore SettingsDialog::aiConfigStore() const
{
    if (m_hasAcceptedConfigStore)
    {
        return m_acceptedConfigStore;
    }

    AIConfigStore store;
    QString errorMessage;
    if (buildAIConfigStore(&store, &errorMessage))
    {
        return store;
    }

    return m_configStore;
}

AppConfig SettingsDialog::appConfig() const
{
    AppConfig config = m_appConfig;
    config.scale = m_scaleSpinBox->value() / 100.0;
    config.launchAtStartup = m_launchAtStartupCheckBox->isChecked();
    config.performanceMode = m_performanceModeComboBox->currentData().toString();
    if (config.performanceMode.isEmpty())
    {
        config.performanceMode = QStringLiteral("standard");
    }
    config.pauseWhenHidden = m_pauseWhenHiddenCheckBox->isChecked();
    config.enableLazyLoad = m_enableLazyLoadCheckBox->isChecked();
    config.enableAnimationPrewarm = m_enableAnimationPrewarmCheckBox->isChecked();
    config.animationCacheLimitMb = m_animationCacheLimitSpinBox->value();
    config.unloadAnimationsWhenHidden = m_unloadAnimationsWhenHiddenCheckBox->isChecked();
    config.requestContextMessageLimit = m_requestContextMessageLimitSpinBox->value();
    config.memoryHistoryMessageLimit = m_memoryHistoryMessageLimitSpinBox->value();
    config.saveConversationHistory = m_saveConversationHistoryCheckBox->isChecked();
    config.savedHistoryMessageLimit = m_savedHistoryMessageLimitSpinBox->value();
    config.characterId = m_characterComboBox->currentData().toString().trimmed();
    if (config.characterId.isEmpty())
    {
        config.characterId = CharacterPackageRepository::defaultCharacterId();
    }
    config.reminderSoundId = selectedReminderSoundId();
    if (config.reminderSoundId.isEmpty())
    {
        config.reminderSoundId = ReminderSoundRepository::defaultSoundId();
    }
    config.reminderSoundEnabled = m_reminderSoundEnabledCheckBox->isChecked();
    config.reminderSoundVolume = qBound(0.0, m_reminderSoundVolumeSpinBox->value() / 100.0, 1.0);
    return config;
}

WeatherConfig SettingsDialog::weatherConfig() const
{
    WeatherConfig config = m_weatherConfig;
    config.provider = QStringLiteral("open-meteo");
    config.defaultCityName = m_weatherDefaultCityEdit->text().trimmed();
    config.autoLocateWhenNoDefault = m_weatherAutoLocateCheckBox->isChecked();
    config.language = QStringLiteral("zh");
    config.timeoutMs = qBound(3000, config.timeoutMs, 60000);
    return config;
}

WebConfig SettingsDialog::webConfig() const
{
    WebConfig config = m_webConfig;
    config.enabled = m_webEnabledCheckBox->isChecked();
    config.rememberLastToggle = m_webRememberToggleCheckBox->isChecked();
    config.defaultToggleOn = m_webDefaultOnCheckBox->isChecked();
    config.providerMode = m_webProviderModeComboBox->currentData().toString();
    if (config.providerMode.isEmpty())
    {
        config.providerMode = QStringLiteral("auto");
    }
    config.timeoutMs = qBound(3000, m_webTimeoutSpinBox->value(), 120000);
    config.showCitations = m_webShowCitationsCheckBox->isChecked();
    return config;
}

void SettingsDialog::testWeatherDefaultCity()
{
    const QString cityName = m_weatherDefaultCityEdit->text().trimmed();
    if (cityName.isEmpty())
    {
        m_weatherStatusLabel->setText(QStringLiteral("请先填写默认城市。"));
        return;
    }

    if (!m_weatherTestManager)
    {
        m_weatherStatusLabel->setText(QStringLiteral("天气城市测试初始化失败。"));
        return;
    }

    m_testWeatherDefaultCityButton->setEnabled(false);
    m_weatherStatusLabel->setText(QStringLiteral("正在测试默认城市..."));

    WeatherConfig config = weatherConfig();
    config.defaultCityName = cityName;
    QPointer<SettingsDialog> dialog(this);
    m_weatherTestManager->resolveLocationForTest(cityName, config, [dialog](const LocationTestResult &result) {
        if (dialog.isNull())
        {
            return;
        }

        dialog->m_testWeatherDefaultCityButton->setEnabled(true);
        if (!result.success)
        {
            dialog->m_weatherStatusLabel->setText(result.errorMessage.isEmpty()
                ? QStringLiteral("城市测试失败。")
                : result.errorMessage);
            return;
        }

        dialog->m_weatherStatusLabel->setText(weatherLocationTestDisplayText(result));
    });
}

void SettingsDialog::refreshWebCapabilityStatus()
{
    if (!m_webCapabilityStatusLabel)
    {
        return;
    }

    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    const AIConfig config = runtimeConfigFromForm(provider);
    const WebCapability capability = WebCapabilityDetector::detect(config, webConfig());
    m_webCapabilityStatusLabel->setText(webCapabilityDisplayText(capability));
}

void SettingsDialog::testWebMode()
{
    if (!m_webTestManager)
    {
        m_webStatusLabel->setText(QStringLiteral("联网模式测试初始化失败。"));
        return;
    }

    if (m_webTestManager->isBusy())
    {
        return;
    }

    WebConfig config = webConfig();
    config.enabled = true;

    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    AIConfig aiConfig = runtimeConfigFromForm(provider);
    aiConfig.maxTokens = qMax(256, qMin(aiConfig.maxTokens, 1024));

    const WebCapability capability = WebCapabilityDetector::detect(aiConfig, config);
    refreshWebCapabilityStatus();
    if (!capability.supported)
    {
        m_webStatusLabel->setText(capability.userMessage);
        return;
    }

    ChatRequest request;
    request.messages.append(ChatMessage{
        QStringLiteral("system"),
        QStringLiteral("你是桌宠助手。请用中文简短回答，并尽量返回来源。")});
    request.messages.append(ChatMessage{
        QStringLiteral("user"),
        QStringLiteral("请联网确认 Qt 6 的最新稳定版本，并给出来源。")});

    WebChatRequest webRequest;
    webRequest.chatRequest = request;
    webRequest.aiConfig = aiConfig;
    webRequest.webConfig = config;

    m_testWebButton->setEnabled(false);
    m_webStatusLabel->setText(QStringLiteral("正在测试联网模式..."));

    QPointer<SettingsDialog> dialog(this);
    m_webTestManager->sendWebChat(webRequest, [dialog](const WebChatResponse &response) {
        if (dialog.isNull())
        {
            return;
        }

        dialog->m_testWebButton->setEnabled(true);
        if (!response.success)
        {
            dialog->m_webStatusLabel->setText(response.errorMessage.isEmpty()
                ? QStringLiteral("联网模式测试失败。")
                : response.errorMessage);
            return;
        }

        dialog->m_webStatusLabel->setText(QStringLiteral("联网模式测试成功。实际联网：%1，来源数：%2。")
            .arg(response.usedWeb ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(response.citations.size()));
    });
}


void SettingsDialog::accept()
{
    if (!SecretStore::isEncryptionAvailable()
        && !m_allowPlainApiKeyCheckBox->isChecked()
        && !m_apiKeyEdit->text().isEmpty())
    {
        QMessageBox::information(
            this,
            QStringLiteral("API Key 未保存"),
            QStringLiteral("当前平台不支持内置加密，且未允许明文保存。API Key 不会写入配置文件。"));
    }

    AIConfigStore store;
    QString errorMessage;
    if (!buildAIConfigStore(&store, &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("设置未保存"), errorMessage);
        setTestStatus(errorMessage, QStringLiteral("error"));
        return;
    }

    m_acceptedConfigStore = store;
    m_hasAcceptedConfigStore = true;
    QDialog::accept();
}

bool SettingsDialog::buildAIConfigStore(AIConfigStore *store, QString *errorMessage) const
{
    if (store == nullptr)
    {
        return false;
    }

    AIConfigStore result = m_configStore;
    result.providers.remove(QStringLiteral("claude"));
    result.providers.remove(QStringLiteral("calude"));

    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    QString configError;
    const AIConfig currentConfig = configFromForm(provider, &configError);
    if (!configError.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = configError;
        }
        return false;
    }

    result.activeProvider = provider;
    result.providers.insert(provider, currentConfig);
    *store = result;
    return true;
}

void SettingsDialog::cacheCurrentProvider()
{
    if (m_currentProvider.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const AIConfig config = configFromForm(m_currentProvider, &errorMessage);
    if (!errorMessage.isEmpty())
    {
        setTestStatus(errorMessage, QStringLiteral("error"));
        return;
    }

    m_configStore.providers.insert(m_currentProvider, config);
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
    refreshWebCapabilityStatus();
}

AIConfig SettingsDialog::configFromForm(const QString &provider, QString *errorMessage) const
{
    const QString normalizedProvider = normalizedProviderName(provider);
    AIConfig config = m_configStore.providers.value(normalizedProvider, defaultAIConfigForProvider(normalizedProvider));
    const QString previousApiKeyStorage = config.apiKeyStorage;
    const QString previousApiKeyEncrypted = config.apiKeyEncrypted;
    const QString previousApiKey = config.apiKey;
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
            config.apiKeyStorage = previousApiKeyStorage;
            config.apiKeyEncrypted = previousApiKeyEncrypted;
            config.apiKey = previousApiKey;
            if (errorMessage != nullptr)
            {
                *errorMessage = QStringLiteral("API Key 加密失败：") + result.errorMessage;
            }
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
    request.messages.append(ChatMessage{
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

void SettingsDialog::clearConversationHistory()
{
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        QStringLiteral("清空聊天记录"),
        QStringLiteral("确定要清空当前内存和本地保存的聊天记录吗？"));
    if (result != QMessageBox::Yes)
    {
        return;
    }

    if (m_clearConversationHistory)
    {
        m_clearConversationHistory();
    }

    m_conversationHistory.clear();
    reloadConversationHistoryList();
    m_clearConversationStatusLabel->setText(QStringLiteral("聊天记录已清空。"));
}

QVector<ChatMessage> SettingsDialog::filteredConversationHistory() const
{
    QVector<ChatMessage> filtered;
    const QString keyword = m_historySearchEdit->text().trimmed();
    const QString provider = m_historyProviderFilterComboBox->currentData().toString();
    const QString model = m_historyModelFilterComboBox->currentData().toString();

    for (const ChatMessage &message : m_conversationHistory)
    {
        if (!keyword.isEmpty() && !message.content.contains(keyword, Qt::CaseInsensitive))
        {
            continue;
        }
        if (provider != QStringLiteral("all") && message.provider.trimmed() != provider)
        {
            continue;
        }
        if (model != QStringLiteral("all") && message.model.trimmed() != model)
        {
            continue;
        }

        filtered.append(message);
    }
    return filtered;
}

void SettingsDialog::reloadConversationHistoryList()
{
    m_historyListWidget->clear();
    const QVector<ChatMessage> messages = filteredConversationHistory();
    for (const ChatMessage &message : messages)
    {
        QString title = QStringLiteral("%1  %2").arg(chatRoleDisplayText(message.role), compactChatContent(message.content));
        const QString meta = chatMetaText(message);
        if (!meta.isEmpty())
        {
            title.prepend(meta + QStringLiteral("  "));
        }

        auto *item = new QListWidgetItem(title, m_historyListWidget);
        item->setToolTip(message.content);
    }

    const bool hasMessages = !messages.isEmpty();
    m_exportHistoryMarkdownButton->setEnabled(hasMessages);
    m_exportHistoryJsonButton->setEnabled(hasMessages);
    if (!hasMessages)
    {
        auto *emptyItem = new QListWidgetItem(QStringLiteral("当前筛选条件下没有聊天记录。"), m_historyListWidget);
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
    }
}

void SettingsDialog::exportConversationHistoryMarkdown()
{
    const QVector<ChatMessage> messages = filteredConversationHistory();
    if (messages.isEmpty())
    {
        m_clearConversationStatusLabel->setText(QStringLiteral("没有可导出的聊天记录。"));
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出聊天记录 Markdown"),
        QStringLiteral("conversation_history.md"),
        QStringLiteral("Markdown (*.md)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QString markdown;
    QTextStream stream(&markdown);
    stream << "# QtDesktopPet 聊天记录\n\n";
    for (const ChatMessage &message : messages)
    {
        stream << "## " << chatRoleDisplayText(message.role);
        const QString meta = chatMetaText(message);
        if (!meta.isEmpty())
        {
            stream << " - " << meta;
        }
        stream << "\n\n";
        stream << markdownEscaped(message.content).trimmed() << "\n\n";
    }

    QString errorMessage;
    if (!writeBytesAtomically(filePath, markdown.toUtf8(), &errorMessage))
    {
        m_clearConversationStatusLabel->setText(errorMessage.isEmpty() ? QStringLiteral("导出 Markdown 失败。") : errorMessage);
        return;
    }

    m_clearConversationStatusLabel->setText(QStringLiteral("已导出 Markdown：%1").arg(filePath));
}

void SettingsDialog::exportConversationHistoryJson()
{
    const QVector<ChatMessage> messages = filteredConversationHistory();
    if (messages.isEmpty())
    {
        m_clearConversationStatusLabel->setText(QStringLiteral("没有可导出的聊天记录。"));
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出聊天记录 JSON"),
        QStringLiteral("conversation_history.json"),
        QStringLiteral("JSON (*.json)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QJsonArray messageArray;
    for (const ChatMessage &message : messages)
    {
        messageArray.append(jsonFromChatMessage(message));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("messages"), messageArray);

    QString errorMessage;
    if (!writeBytesAtomically(filePath, QJsonDocument(root).toJson(QJsonDocument::Indented), &errorMessage))
    {
        m_clearConversationStatusLabel->setText(errorMessage.isEmpty() ? QStringLiteral("导出 JSON 失败。") : errorMessage);
        return;
    }

    m_clearConversationStatusLabel->setText(QStringLiteral("已导出 JSON：%1").arg(filePath));
}

void SettingsDialog::reloadReminderList()
{
    m_reminderListWidget->clear();

    const QString filter = m_reminderStatusFilterComboBox->currentData().toString();
    const QVector<ReminderItem> reminders = sortedReminders(m_reminders);
    for (const ReminderItem &item : reminders)
    {
        if (!reminderMatchesStatusFilter(item, filter))
        {
            continue;
        }

        auto *listItem = new QListWidgetItem(
            QStringLiteral("%1  %2  %3  %4  %5")
                .arg(reminderDisplayTime(item.remindAt), reminderStatusDisplayText(item.status), reminderRecurrenceDisplayText(item), item.title, item.originalText),
            m_reminderListWidget);
        listItem->setData(Qt::UserRole, item.id);
        listItem->setData(Qt::UserRole + 1, reminderStatusToString(item.status));
        listItem->setToolTip(item.originalText);
    }

    if (m_reminderListWidget->count() == 0)
    {
        m_reminderStatusLabel->setText(QStringLiteral("当前筛选条件下没有提醒。"));
    }
    else
    {
        m_reminderStatusLabel->setText(QStringLiteral("共 %1 条提醒。").arg(m_reminderListWidget->count()));
    }
    updateReminderActionButtons();
}

void SettingsDialog::reloadReminderSoundList(const QString &selectedSoundId)
{
    const QString preferredSoundId = selectedSoundId.trimmed().isEmpty()
        ? ReminderSoundRepository::defaultSoundId()
        : selectedSoundId.trimmed();

    {
        const QSignalBlocker blocker(m_reminderSoundComboBox);
        m_reminderSoundComboBox->clear();
        const QVector<ReminderSoundInfo> sounds = ReminderSoundRepository::availableSounds();
        for (const ReminderSoundInfo &sound : sounds)
        {
            QString label = sound.displayName.trimmed().isEmpty() ? sound.id : sound.displayName.trimmed();
            if (label != sound.id)
            {
                label += QStringLiteral(" (") + sound.id + QStringLiteral(")");
            }
            label += sound.builtIn ? QStringLiteral(" - 内置") : QStringLiteral(" - 用户");
            m_reminderSoundComboBox->addItem(label, sound.id);
        }

        if (m_reminderSoundComboBox->count() == 0)
        {
            m_reminderSoundComboBox->addItem(ReminderSoundRepository::defaultSoundId(), ReminderSoundRepository::defaultSoundId());
        }

        int selectedIndex = m_reminderSoundComboBox->findData(preferredSoundId);
        if (selectedIndex < 0)
        {
            selectedIndex = m_reminderSoundComboBox->findData(ReminderSoundRepository::defaultSoundId());
        }
        m_reminderSoundComboBox->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
    }

    updateReminderSoundButtons();
}

QString SettingsDialog::selectedReminderSoundId() const
{
    return m_reminderSoundComboBox->currentData().toString().trimmed();
}

void SettingsDialog::updateReminderSoundButtons()
{
    const QString soundId = selectedReminderSoundId();
    const bool hasSound = !soundId.isEmpty();
    m_deleteReminderSoundButton->setEnabled(hasSound && !ReminderSoundRepository::isBuiltInSound(soundId));
    m_testReminderSoundButton->setEnabled(hasSound);
}

void SettingsDialog::updateReminderActionButtons()
{
    QListWidgetItem *item = m_reminderListWidget->currentItem();
    const bool selectedPending = item != nullptr
        && item->data(Qt::UserRole + 1).toString() == QStringLiteral("pending");
    m_cancelReminderButton->setEnabled(selectedPending);
    m_editReminderButton->setEnabled(selectedPending);
    m_clearFinishedRemindersButton->setEnabled(hasPrunableFinishedReminders(m_reminders, ReminderHistoryRetentionDays));
}

void SettingsDialog::cancelSelectedReminder()
{
    QListWidgetItem *item = m_reminderListWidget->currentItem();
    if (item == nullptr)
    {
        return;
    }

    const QString reminderId = item->data(Qt::UserRole).toString();
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        QStringLiteral("取消提醒"),
        QStringLiteral("确定要取消这条提醒吗？记录会保留为 canceled，不会物理删除。"));
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!m_cancelReminder || !m_cancelReminder(reminderId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("取消提醒失败"),
            errorMessage.isEmpty() ? QStringLiteral("没有找到可取消的提醒。") : errorMessage);
        return;
    }

    for (ReminderItem &reminder : m_reminders)
    {
        if (reminder.id == reminderId)
        {
            reminder.status = ReminderStatus::Canceled;
            reminder.finishedAt = QDateTime::currentDateTime();
            break;
        }
    }
    reloadReminderList();
    m_reminderStatusLabel->setText(QStringLiteral("已取消提醒：%1").arg(reminderId));
}

void SettingsDialog::editSelectedReminder()
{
    QListWidgetItem *item = m_reminderListWidget->currentItem();
    if (item == nullptr)
    {
        return;
    }

    const QString reminderId = item->data(Qt::UserRole).toString();
    const ReminderItem *reminder = findReminderById(m_reminders, reminderId);
    if (reminder == nullptr || reminder->status != ReminderStatus::Pending)
    {
        QMessageBox::information(
            this,
            QStringLiteral("编辑提醒"),
            QStringLiteral("只能编辑待提醒事项。"));
        return;
    }

    ReminderEditDialog dialog(*reminder, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    ReminderItem updatedReminder;
    QString errorMessage;
    if (!m_updateReminder
        || !m_updateReminder(reminderId, dialog.title(), dialog.remindAt(), dialog.recurrence(), &updatedReminder, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("编辑提醒失败"),
            errorMessage.isEmpty() ? QStringLiteral("编辑提醒失败。") : errorMessage);
        return;
    }

    for (ReminderItem &existingReminder : m_reminders)
    {
        if (existingReminder.id == reminderId)
        {
            existingReminder = updatedReminder;
            break;
        }
    }

    reloadReminderList();
    m_reminderStatusLabel->setText(QStringLiteral("已更新提醒：%1").arg(updatedReminder.id));
}

void SettingsDialog::clearFinishedReminders()
{
    if (!hasPrunableFinishedReminders(m_reminders, ReminderHistoryRetentionDays))
    {
        m_reminderStatusLabel->setText(QStringLiteral("没有可清理的20天前历史提醒。"));
        return;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("清理提醒历史"),
        QStringLiteral("确定要清理20天前的已触发和已取消提醒记录吗？\n\n最近20天历史会保留，待提醒事项不会被删除。"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!m_clearFinishedReminders || !m_clearFinishedReminders(&errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("清理提醒历史失败"),
            errorMessage.isEmpty() ? QStringLiteral("清理提醒历史失败。") : errorMessage);
        return;
    }

    for (int index = m_reminders.size() - 1; index >= 0; --index)
    {
        if (isFinishedReminder(m_reminders.at(index))
            && reminderFinishedReferenceTime(m_reminders.at(index)) < QDateTime::currentDateTime().addDays(-ReminderHistoryRetentionDays))
        {
            m_reminders.removeAt(index);
        }
    }

    reloadReminderList();
    m_reminderStatusLabel->setText(QStringLiteral("已清理20天前提醒历史。"));
}

void SettingsDialog::importReminderSound()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("导入提醒音效"),
        QString(),
        QStringLiteral("Wave 音频 (*.wav)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QString importedSoundId;
    QString errorMessage;
    if (!ReminderSoundRepository::importSoundFile(filePath, &importedSoundId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("导入音效失败"),
            errorMessage.isEmpty() ? QStringLiteral("导入音效失败。") : errorMessage);
        return;
    }

    reloadReminderSoundList(importedSoundId);
    m_reminderSoundStatusLabel->setText(QStringLiteral("已导入音效：%1").arg(importedSoundId));
}

void SettingsDialog::deleteSelectedReminderSound()
{
    const QString soundId = selectedReminderSoundId();
    if (soundId.isEmpty())
    {
        return;
    }

    if (ReminderSoundRepository::isBuiltInSound(soundId))
    {
        QMessageBox::information(
            this,
            QStringLiteral("不能删除内置音效"),
            QStringLiteral("内置提醒音效作为默认资源保留，不能在设置页删除。"));
        return;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("删除提醒音效"),
        QStringLiteral("确定要删除用户音效 \"%1\" 吗？\n\n这会删除用户音效文件，操作不可恢复。").arg(soundId),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!ReminderSoundRepository::deleteUserSound(soundId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("删除音效失败"),
            errorMessage.isEmpty() ? QStringLiteral("删除用户音效失败。") : errorMessage);
        return;
    }

    reloadReminderSoundList(ReminderSoundRepository::defaultSoundId());
    m_reminderSoundStatusLabel->setText(QStringLiteral("已删除音效：%1，当前已回退到默认音效。").arg(soundId));
}

void SettingsDialog::testSelectedReminderSound()
{
    const QString soundId = selectedReminderSoundId();
    if (soundId.isEmpty() || !m_playReminderSound)
    {
        return;
    }

    m_playReminderSound(soundId, qBound(0.0, m_reminderSoundVolumeSpinBox->value() / 100.0, 1.0));
    m_reminderSoundStatusLabel->setText(QStringLiteral("正在试听：%1").arg(soundId));
}

void SettingsDialog::reloadCharacterList(const QString &selectedCharacterId)
{
    const QString currentSelection = selectedCharacterId.trimmed().isEmpty()
        ? m_characterComboBox->currentData().toString()
        : selectedCharacterId.trimmed();

    m_characterComboBox->clear();
    const QVector<CharacterPackageInfo> packages = CharacterPackageRepository::availablePackages();
    for (const CharacterPackageInfo &package : packages)
    {
        QString label = package.displayName.trimmed().isEmpty() ? package.id : package.displayName.trimmed();
        if (label != package.id)
        {
            label += QStringLiteral(" (") + package.id + QStringLiteral(")");
        }
        label += package.userPackage ? QStringLiteral(" - 用户") : QStringLiteral(" - 内置");
        m_characterComboBox->addItem(label, package.id);
    }

    if (m_characterComboBox->count() == 0)
    {
        m_characterComboBox->addItem(CharacterPackageRepository::defaultCharacterId(), CharacterPackageRepository::defaultCharacterId());
    }

    int selectedIndex = m_characterComboBox->findData(currentSelection);
    if (selectedIndex < 0)
    {
        selectedIndex = m_characterComboBox->findData(CharacterPackageRepository::defaultCharacterId());
    }
    m_characterComboBox->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}

void SettingsDialog::deleteSelectedCharacter()
{
    const QString characterId = m_characterComboBox->currentData().toString().trimmed();
    if (characterId.isEmpty())
    {
        return;
    }

    if (CharacterPackageRepository::hasBuiltInPackage(characterId))
    {
        QMessageBox::information(
            this,
            QStringLiteral("不能删除内置角色"),
            QStringLiteral("当前选择的是内置角色，不能删除。"));
        return;
    }

    if (!CharacterPackageRepository::hasUserPackage(characterId))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("删除角色失败"),
            QStringLiteral("没有找到可删除的用户角色目录。"));
        return;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("删除角色"),
        QStringLiteral("确定要删除用户角色 \"%1\" 吗？\n\n这会删除该角色的用户角色目录，操作不可恢复。").arg(characterId),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!CharacterPackageRepository::deleteUserPackage(characterId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("删除角色失败"),
            errorMessage.isEmpty() ? QStringLiteral("删除用户角色目录失败。") : errorMessage);
        return;
    }

    if (m_appConfig.characterId == characterId)
    {
        m_appConfig.characterId = CharacterPackageRepository::defaultCharacterId();
    }

    reloadCharacterList(CharacterPackageRepository::defaultCharacterId());
    m_characterStatusLabel->setText(QStringLiteral("已删除角色：%1").arg(characterId));
}

void SettingsDialog::importCharacterFolder()
{
    const QString directoryPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择角色文件夹"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directoryPath.isEmpty())
    {
        return;
    }

    CharacterPackageValidationResult validation = CharacterPackageRepository::validatePackageDirectoryWithDetails(directoryPath);
    if (!validation.valid)
    {
        QMessageBox::warning(
            this,
            QStringLiteral("角色导入失败"),
            QStringLiteral("角色包验证未通过：\n") + validation.errorMessage);
        return;
    }

    const QString characterId = validation.characterId;
    const QString displayName = validation.displayName;
    if (CharacterPackageRepository::hasBuiltInPackage(characterId))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("角色导入失败"),
            QStringLiteral("角色 id 与内置角色重复，不能覆盖内置角色。\n请修改角色包 id 或文件夹名后再导入。"));
        return;
    }

    bool overwrite = false;
    const bool sourceIsExistingUserPackage =
        QDir::cleanPath(directoryPath) == QDir::cleanPath(CharacterPackageRepository::userPackagePath(characterId));
    if (CharacterPackageRepository::hasUserPackage(characterId) && !sourceIsExistingUserPackage)
    {
        const QMessageBox::StandardButton result = QMessageBox::question(
            this,
            QStringLiteral("覆盖已有角色"),
            QStringLiteral("已存在同名用户角色：%1\n是否覆盖？").arg(characterId),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes)
        {
            return;
        }

        overwrite = true;
    }

    if (!validation.warningMessage.isEmpty())
    {
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this,
            QStringLiteral("角色导入提示"),
            validation.warningMessage + QStringLiteral("\n\n仍然继续导入吗？"),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes)
        {
            return;
        }
    }

    QString errorMessage;
    QString importedCharacterId;
    if (!CharacterPackageRepository::importPackageDirectory(directoryPath, overwrite, &importedCharacterId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("角色导入失败"),
            errorMessage.isEmpty() ? QStringLiteral("复制角色文件夹失败。") : errorMessage);
        return;
    }

    reloadCharacterList(importedCharacterId);
    m_characterStatusLabel->setText(QStringLiteral("已导入角色：%1").arg(displayName.isEmpty() ? importedCharacterId : displayName));
}

void SettingsDialog::exportSelectedCharacter()
{
    const QString characterId = m_characterComboBox->currentData().toString().trimmed();
    if (characterId.isEmpty())
    {
        return;
    }

    const QString targetParentDirectoryPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择角色导出目录"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (targetParentDirectoryPath.isEmpty())
    {
        return;
    }

    const QString targetPath = QDir(targetParentDirectoryPath).filePath(characterId);
    bool overwrite = false;
    if (QFileInfo::exists(targetPath))
    {
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this,
            QStringLiteral("覆盖导出目标"),
            QStringLiteral("目标目录已存在：\n%1\n\n继续会替换该导出目录。是否继续？").arg(targetPath),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes)
        {
            return;
        }
        overwrite = true;
    }

    QString exportedPath;
    QString errorMessage;
    if (!CharacterPackageRepository::exportPackageDirectory(characterId, targetParentDirectoryPath, overwrite, &exportedPath, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("角色导出失败"),
            errorMessage.isEmpty() ? QStringLiteral("导出角色失败。") : errorMessage);
        return;
    }

    m_characterStatusLabel->setText(QStringLiteral("已导出角色到：%1").arg(exportedPath));
}

void SettingsDialog::openUserCharacterDirectory()
{
    const QString directoryPath = CharacterPackageRepository::userCharactersRootPath();
    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral(".")))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("打开用户角色目录失败"),
            QStringLiteral("无法创建用户角色目录。"));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory.absolutePath())))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("打开用户角色目录失败"),
            QStringLiteral("系统文件管理器无法打开该目录。"));
        return;
    }

    m_characterStatusLabel->setText(QStringLiteral("已打开用户角色目录。"));
}
