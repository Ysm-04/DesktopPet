#include "SettingsDialog.h"

#include "../ai/AIProviderFactory.h"
#include "../ai/LLMTypes.h"
#include "../ai/LongTermMemoryStore.h"
#include "../character/CharacterPackage.h"
#include "../character/CharacterPackageRepository.h"
#include "../config/SecretStore.h"
#include "../reminder/ReminderSoundRepository.h"
#include "../web/WebCapabilityDetector.h"
#include "../web/WebChatManager.h"
#include "../weather/WeatherManager.h"
#include "../weather/WeatherProviderCodec.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
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
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <limits>
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

const LongTermMemoryItem *findLongTermMemoryById(const QVector<LongTermMemoryItem> &items, const QString &id)
{
    for (const LongTermMemoryItem &item : items)
    {
        if (item.id == id)
        {
            return &item;
        }
    }

    return nullptr;
}

QString longTermMemoryTimeText(const QDateTime &dateTime)
{
    return dateTime.isValid()
        ? dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QStringLiteral("-");
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

class LongTermMemoryEditDialog : public QDialog
{
public:
    explicit LongTermMemoryEditDialog(
        const LongTermMemoryItem *existingItem = nullptr,
        QWidget *parent = nullptr)
        : QDialog(parent)
        , m_categoryComboBox(new QComboBox(this))
        , m_contentEdit(new QPlainTextEdit(this))
        , m_lengthHintLabel(new QLabel(this))
    {
        setWindowTitle(existingItem == nullptr ? QStringLiteral("新增长期记忆") : QStringLiteral("编辑长期记忆"));
        setModal(true);
        resize(460, 260);

        m_categoryComboBox->addItem(QStringLiteral("个人画像"), longTermMemoryCategoryToString(LongTermMemoryCategory::Profile));
        m_categoryComboBox->addItem(QStringLiteral("偏好习惯"), longTermMemoryCategoryToString(LongTermMemoryCategory::Preference));
        m_categoryComboBox->addItem(QStringLiteral("项目背景"), longTermMemoryCategoryToString(LongTermMemoryCategory::Project));
        m_categoryComboBox->addItem(QStringLiteral("重要事实"), longTermMemoryCategoryToString(LongTermMemoryCategory::Fact));

        const LongTermMemoryCategory initialCategory = existingItem != nullptr
            ? existingItem->category
            : LongTermMemoryCategory::Fact;
        const int categoryIndex = m_categoryComboBox->findData(longTermMemoryCategoryToString(initialCategory));
        m_categoryComboBox->setCurrentIndex(categoryIndex >= 0 ? categoryIndex : 3);

        m_contentEdit->setPlaceholderText(QStringLiteral("例如：我偏好简洁直接的回答风格 / 目前正在开发 QtDesktopPet 项目"));
        if (existingItem != nullptr)
        {
            m_contentEdit->setPlainText(existingItem->content);
        }

        m_lengthHintLabel->setObjectName(QStringLiteral("HintLabel"));
        m_lengthHintLabel->setWordWrap(true);
        updateLengthHint();

        auto *formLayout = new QFormLayout();
        formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        formLayout->addRow(QStringLiteral("分类"), m_categoryComboBox);
        formLayout->addRow(QStringLiteral("内容"), m_contentEdit);

        auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
            accept();
        });
        connect(buttonBox, &QDialogButtonBox::rejected, this, [this]() {
            reject();
        });
        connect(m_contentEdit, &QPlainTextEdit::textChanged, this, [this]() {
            updateLengthHint();
        });

        auto *layout = new QVBoxLayout(this);
        layout->addLayout(formLayout);
        layout->addWidget(m_lengthHintLabel);
        layout->addWidget(buttonBox);
    }

    LongTermMemoryCategory category() const
    {
        return longTermMemoryCategoryFromString(m_categoryComboBox->currentData().toString());
    }

    QString content() const
    {
        return m_contentEdit->toPlainText().simplified();
    }

protected:
    void accept() override
    {
        const QString cleaned = content();
        if (cleaned.size() < 2)
        {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("记忆内容至少需要 2 个有效字符。"));
            return;
        }

        if (cleaned.size() > LongTermMemoryStore::MaxContentChars)
        {
            QMessageBox::warning(
                this,
                windowTitle(),
                QStringLiteral("每条长期记忆最多 %1 字，当前为 %2 字，请适当精简。")
                    .arg(LongTermMemoryStore::MaxContentChars)
                    .arg(cleaned.size()));
            return;
        }

        if (LongTermMemoryStore::containsSensitiveSecret(cleaned))
        {
            QMessageBox::warning(
                this,
                windowTitle(),
                QStringLiteral("检测到疑似密钥、Token 或密码等敏感信息，长期记忆不会明文保存此类内容。"));
            return;
        }

        QDialog::accept();
    }

private:
    void updateLengthHint()
    {
        const int length = content().size();
        m_lengthHintLabel->setText(
            QStringLiteral("当前 %1 / %2 字（至少 2 字；严禁填写 API Key、Token 或密码等敏感凭据）。")
                .arg(length)
                .arg(LongTermMemoryStore::MaxContentChars));
    }

    QComboBox *m_categoryComboBox = nullptr;
    QPlainTextEdit *m_contentEdit = nullptr;
    QLabel *m_lengthHintLabel = nullptr;
};

class ScrollWheelGuard : public QObject
{
public:
    explicit ScrollWheelGuard(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event != nullptr && event->type() == QEvent::Wheel)
        {
            if (auto *widget = qobject_cast<QWidget *>(watched); widget != nullptr && !widget->hasFocus())
            {
                event->ignore();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

QScrollArea *wrapInScrollArea(QWidget *page, QWidget *parent)
{
    if (page != nullptr)
    {
        page->setObjectName(QStringLiteral("SettingsPageContent"));
    }

    auto *scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(QStringLiteral("SettingsPageScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setWidget(page);
    return scrollArea;
}

void applyAgentHookStatusToRow(
    const AgentHookStatus &status,
    QLabel *statusLabel,
    QPushButton *installButton,
    QPushButton *uninstallButton)
{
    if (statusLabel == nullptr || installButton == nullptr || uninstallButton == nullptr)
    {
        return;
    }

    QString badgeText;
    QString badgeColor = QStringLiteral("#667085");
    QString installText = QStringLiteral("一键接入");
    bool canUninstall = false;

    switch (status.state)
    {
    case AgentHookInstallState::NotDetected:
        badgeText = QStringLiteral("未检测到目录");
        badgeColor = QStringLiteral("#667085");
        break;
    case AgentHookInstallState::NotInstalled:
        badgeText = QStringLiteral("未接入");
        badgeColor = QStringLiteral("#475467");
        break;
    case AgentHookInstallState::Installed:
        badgeText = QStringLiteral("已接入");
        badgeColor = QStringLiteral("#067647");
        installText = QStringLiteral("重新写入");
        canUninstall = true;
        break;
    case AgentHookInstallState::PathMismatch:
        badgeText = QStringLiteral("路径待修复");
        badgeColor = QStringLiteral("#b54708");
        installText = QStringLiteral("修复路径");
        canUninstall = true;
        break;
    case AgentHookInstallState::ConfigInvalid:
        badgeText = QStringLiteral("JSON损坏");
        badgeColor = QStringLiteral("#b42318");
        break;
    }

    statusLabel->setText(badgeText);
    statusLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(badgeColor));
    statusLabel->setToolTip(QStringLiteral("%1\n配置文件：%2").arg(status.detail, status.configPath));
    installButton->setText(installText);
    uninstallButton->setEnabled(canUninstall);
}

QString locateAgentTemplatesDirectory()
{
    QStringList candidates;
    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates.append(appDir.filePath(QStringLiteral("examples/agent_bridge")));
#ifdef PET_SOURCE_DIR
    candidates.append(QDir(QStringLiteral(PET_SOURCE_DIR)).filePath(QStringLiteral("examples/agent_bridge")));
#endif
    candidates.append(appDir.filePath(QStringLiteral("../examples/agent_bridge")));
    candidates.append(appDir.filePath(QStringLiteral("../../examples/agent_bridge")));

    for (const QString &candidate : candidates)
    {
        const QFileInfo info(candidate);
        if (info.exists() && info.isDir())
        {
            return QDir::cleanPath(info.absoluteFilePath());
        }
    }
    return {};
}

bool sameAppConfigForSettings(const AppConfig &left, const AppConfig &right)
{
    return qRound(left.scale * 100.0) == qRound(right.scale * 100.0)
        && left.launchAtStartup == right.launchAtStartup
        && left.performanceMode == right.performanceMode
        && left.pauseWhenHidden == right.pauseWhenHidden
        && left.enableLazyLoad == right.enableLazyLoad
        && left.enableAnimationPrewarm == right.enableAnimationPrewarm
        && left.animationCacheLimitMb == right.animationCacheLimitMb
        && left.unloadAnimationsWhenHidden == right.unloadAnimationsWhenHidden
        && left.requestContextMessageLimit == right.requestContextMessageLimit
        && left.requestContextUseMax == right.requestContextUseMax
        && left.requestContextMaxChars == right.requestContextMaxChars
        && left.memoryHistoryMessageLimit == right.memoryHistoryMessageLimit
        && left.saveConversationHistory == right.saveConversationHistory
        && left.saveAllConversationHistory == right.saveAllConversationHistory
        && left.savedHistoryMessageLimit == right.savedHistoryMessageLimit
        && left.enableHistoryRecall == right.enableHistoryRecall
        && left.enableLongTermMemory == right.enableLongTermMemory
        && left.autoExtractLongTermMemory == right.autoExtractLongTermMemory
        && left.characterId == right.characterId
        && left.reminderSoundId == right.reminderSoundId
        && left.reminderSoundEnabled == right.reminderSoundEnabled
        && qRound(left.reminderSoundVolume * 100.0) == qRound(right.reminderSoundVolume * 100.0)
        && left.enableCharacterEffects == right.enableCharacterEffects
        && left.enableTouchInteraction == right.enableTouchInteraction
        && left.enableStartupGreeting == right.enableStartupGreeting
        && left.enableProactiveCare == right.enableProactiveCare
        && left.sedentaryReminderMinutes == right.sedentaryReminderMinutes
        && left.enableAgentBridge == right.enableAgentBridge
        && left.showAgentStatusBar == right.showAgentStatusBar
        && left.agentBubbleNotifyLevel == right.agentBubbleNotifyLevel
        && left.agentAlertSoundEnabled == right.agentAlertSoundEnabled;
}

bool sameWeatherConfigForSettings(const WeatherConfig &left, const WeatherConfig &right)
{
    return left.provider == right.provider
        && left.defaultCityName == right.defaultCityName
        && left.autoLocateWhenNoDefault == right.autoLocateWhenNoDefault
        && left.language == right.language
        && left.timeoutMs == right.timeoutMs;
}

bool sameWebConfigForSettings(const WebConfig &left, const WebConfig &right)
{
    return left.enabled == right.enabled
        && left.rememberLastToggle == right.rememberLastToggle
        && left.defaultToggleOn == right.defaultToggleOn
        && left.providerMode == right.providerMode
        && left.timeoutMs == right.timeoutMs
        && left.showCitations == right.showCitations;
}
}

SettingsDialog::SettingsDialog(
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
    std::function<void(const QString &)> characterPersonaChangedCallback,
    QWidget *parent)
    : QDialog(parent)
    , m_providerComboBox(new QComboBox(this))
    , m_protocolRowLabel(new QLabel(QStringLiteral("接口协议"), this))
    , m_protocolComboBox(new QComboBox(this))
    , m_baseUrlEdit(new QLineEdit(this))
    , m_apiKeyEdit(new QLineEdit(this))
    , m_modelEdit(new QLineEdit(this))
    , m_pathEdit(new QLineEdit(this))
    , m_timeoutSpinBox(new QSpinBox(this))
    , m_temperatureSpinBox(new QDoubleSpinBox(this))
    , m_maxTokensSpinBox(new QSpinBox(this))
    , m_testConnectionButton(new QPushButton(QStringLiteral("测试连接"), this))
    , m_resetProviderButton(new QPushButton(this))
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
    , m_requestContextUseMaxCheckBox(new QCheckBox(QStringLiteral("尽量全量发送（MAX，带安全字数保护）"), this))
    , m_requestContextMaxCharsSpinBox(new QSpinBox(this))
    , m_memoryHistoryMessageLimitSpinBox(new QSpinBox(this))
    , m_saveConversationHistoryCheckBox(new QCheckBox(QStringLiteral("保存聊天记录到本地"), this))
    , m_saveAllConversationHistoryCheckBox(new QCheckBox(QStringLiteral("全量保存本地记录（不限制条数）"), this))
    , m_savedHistoryMessageLimitSpinBox(new QSpinBox(this))
    , m_enableHistoryRecallCheckBox(new QCheckBox(QStringLiteral("启用长期记忆回顾（按需检索早期本地对话）"), this))
    , m_clearConversationHistoryButton(new QPushButton(QStringLiteral("清空聊天记录"), this))
    , m_historySearchEdit(new QLineEdit(this))
    , m_historyProviderFilterComboBox(new QComboBox(this))
    , m_historyModelFilterComboBox(new QComboBox(this))
    , m_historyListWidget(new QListWidget(this))
    , m_exportHistoryMarkdownButton(new QPushButton(QStringLiteral("导出 Markdown"), this))
    , m_exportHistoryJsonButton(new QPushButton(QStringLiteral("导出 JSON"), this))
    , m_enableLongTermMemoryCheckBox(new QCheckBox(QStringLiteral("启用长期记忆（每轮对话自动附带已保存的个人偏好与背景）"), this))
    , m_autoExtractLongTermMemoryCheckBox(new QCheckBox(QStringLiteral("对话结束后后台自动提炼长期记忆（默认关闭，开启后会额外消耗少量 Token）"), this))
    , m_memoryCategoryFilterComboBox(new QComboBox(this))
    , m_memorySearchEdit(new QLineEdit(this))
    , m_memoryListWidget(new QListWidget(this))
    , m_addMemoryButton(new QPushButton(QStringLiteral("新增记忆"), this))
    , m_editMemoryButton(new QPushButton(QStringLiteral("编辑记忆"), this))
    , m_removeMemoryButton(new QPushButton(QStringLiteral("删除选中"), this))
    , m_clearMemoriesButton(new QPushButton(QStringLiteral("清空全部"), this))
    , m_memoryStatusLabel(new QLabel(this))
    , m_weatherProviderComboBox(new QComboBox(this))
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
    , m_characterPersonaSourceLabel(new QLabel(this))
    , m_characterPersonaEdit(new QPlainTextEdit(this))
    , m_characterPersonaCountLabel(new QLabel(this))
    , m_saveCharacterPersonaButton(new QPushButton(QStringLiteral("保存人设"), this))
    , m_resetCharacterPersonaButton(new QPushButton(QStringLiteral("恢复角色包默认"), this))
    , m_characterPersonaStatusLabel(new QLabel(this))
    , m_enableCharacterEffectsCheckBox(new QCheckBox(QStringLiteral("角色轻动效，仅对已声明角色生效"), this))
    , m_enableTouchInteractionCheckBox(new QCheckBox(QStringLiteral("启用触摸与拖拽微互动（单击身体、摸头与拖拽放手反馈，0 Token）"), this))
    , m_enableStartupGreetingCheckBox(new QCheckBox(QStringLiteral("启动时按当前时段问候一次"), this))
    , m_enableProactiveCareCheckBox(new QCheckBox(QStringLiteral("启用轻量久坐与深夜休息关怀（忙碌或隐藏时自动避让）"), this))
    , m_sedentaryReminderMinutesSpinBox(new QSpinBox(this))
    , m_enableAgentBridgeCheckBox(new QCheckBox(QStringLiteral("启用外部编程 Agent 本地状态联动（只读副屏，监听 QtDesktopPet.AgentStatus.v1）"), this))
    , m_showAgentStatusBarCheckBox(new QCheckBox(QStringLiteral("在桌宠旁显示并行微型状态胶囊条"), this))
    , m_agentBubbleNotifyLevelComboBox(new QComboBox(this))
    , m_agentAlertSoundEnabledCheckBox(new QCheckBox(QStringLiteral("待确认或异常时播放提示音（默认关闭，不发重复托盘通知）"), this))
    , m_codexHookNameLabel(new QLabel(QStringLiteral("Codex"), this))
    , m_codexHookStatusLabel(new QLabel(this))
    , m_codexHookInstallButton(new QPushButton(QStringLiteral("一键接入"), this))
    , m_codexHookUninstallButton(new QPushButton(QStringLiteral("解除接入"), this))
    , m_antigravityHookStatusLabel(new QLabel(this))
    , m_antigravityHookInstallButton(new QPushButton(QStringLiteral("一键接入"), this))
    , m_antigravityHookUninstallButton(new QPushButton(QStringLiteral("解除接入"), this))
    , m_openDshTemplatesButton(new QPushButton(QStringLiteral("打开模板目录"), this))
    , m_refreshAgentHookButton(new QPushButton(QStringLiteral("刷新状态"), this))
    , m_testAgentBridgeButton(new QPushButton(QStringLiteral("测试联动"), this))
    , m_agentHookActionStatusLabel(new QLabel(this))
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
    , m_longTermMemories(longTermMemories)
    , m_aiTestBlocked(std::move(aiTestBlocked))
    , m_clearConversationHistory(std::move(clearConversationHistoryCallback))
    , m_cancelReminder(std::move(cancelReminderCallback))
    , m_updateReminder(std::move(updateReminderCallback))
    , m_clearFinishedReminders(std::move(clearFinishedRemindersCallback))
    , m_playReminderSound(std::move(playReminderSoundCallback))
    , m_longTermMemoryCallbacks(std::move(longTermMemoryCallbacks))
    , m_characterPersonaChanged(std::move(characterPersonaChangedCallback))
    , m_webTestManager(std::make_unique<WebChatManager>())
    , m_weatherTestManager(std::make_unique<WeatherManager>())
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);
    setMinimumSize(640, 420);
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

    m_protocolComboBox->addItem(QStringLiteral("OpenAI Compatible"), QStringLiteral("openai-compatible"));
    m_protocolComboBox->addItem(QStringLiteral("Google Gemini 原生"), QStringLiteral("google-generative-language"));

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

    m_enableAgentBridgeCheckBox->setChecked(m_appConfig.enableAgentBridge);
    m_showAgentStatusBarCheckBox->setChecked(m_appConfig.showAgentStatusBar);
    m_agentBubbleNotifyLevelComboBox->addItem(QStringLiteral("仅待确认与异常（推荐）"), QStringLiteral("attention"));
    m_agentBubbleNotifyLevelComboBox->addItem(QStringLiteral("全部状态跃迁（含开始与完成）"), QStringLiteral("all"));
    m_agentBubbleNotifyLevelComboBox->addItem(QStringLiteral("静默（仅胶囊与动画）"), QStringLiteral("silent"));
    const int notifyLevelIndex = m_agentBubbleNotifyLevelComboBox->findData(m_appConfig.agentBubbleNotifyLevel.trimmed().toLower());
    m_agentBubbleNotifyLevelComboBox->setCurrentIndex(notifyLevelIndex >= 0 ? notifyLevelIndex : 0);
    m_agentAlertSoundEnabledCheckBox->setChecked(m_appConfig.agentAlertSoundEnabled);
    m_showAgentStatusBarCheckBox->setEnabled(m_appConfig.enableAgentBridge);
    m_agentBubbleNotifyLevelComboBox->setEnabled(m_appConfig.enableAgentBridge);
    m_agentAlertSoundEnabledCheckBox->setEnabled(m_appConfig.enableAgentBridge);

    m_requestContextMessageLimitSpinBox->setRange(0, 500);
    m_requestContextMessageLimitSpinBox->setValue(qBound(0, m_appConfig.requestContextMessageLimit, 500));
    m_requestContextMessageLimitSpinBox->setEnabled(!m_appConfig.requestContextUseMax);

    m_requestContextUseMaxCheckBox->setChecked(m_appConfig.requestContextUseMax);

    m_requestContextMaxCharsSpinBox->setRange(4000, 120000);
    m_requestContextMaxCharsSpinBox->setSingleStep(4000);
    m_requestContextMaxCharsSpinBox->setSuffix(QStringLiteral(" 字符"));
    m_requestContextMaxCharsSpinBox->setValue(qBound(4000, m_appConfig.requestContextMaxChars, 120000));

    m_memoryHistoryMessageLimitSpinBox->setRange(2, 5000);
    m_memoryHistoryMessageLimitSpinBox->setSingleStep(2);
    m_memoryHistoryMessageLimitSpinBox->setValue(qBound(2, m_appConfig.memoryHistoryMessageLimit, 5000));

    m_saveConversationHistoryCheckBox->setChecked(m_appConfig.saveConversationHistory);
    m_saveAllConversationHistoryCheckBox->setChecked(m_appConfig.saveAllConversationHistory);
    m_saveAllConversationHistoryCheckBox->setEnabled(m_saveConversationHistoryCheckBox->isChecked());

    m_savedHistoryMessageLimitSpinBox->setRange(2, 10000);
    m_savedHistoryMessageLimitSpinBox->setSingleStep(2);
    m_savedHistoryMessageLimitSpinBox->setValue(qBound(2, m_appConfig.savedHistoryMessageLimit, 10000));
    m_savedHistoryMessageLimitSpinBox->setEnabled(
        m_saveConversationHistoryCheckBox->isChecked() && !m_saveAllConversationHistoryCheckBox->isChecked());

    m_enableHistoryRecallCheckBox->setChecked(m_appConfig.enableHistoryRecall);

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
    aiFormLayout->addRow(m_protocolRowLabel, m_protocolComboBox);
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
    aiActionLayout->addWidget(m_resetProviderButton);
    aiActionLayout->addWidget(m_testStatusLabel, 1);

    auto *aiPageLayout = new QVBoxLayout();
    aiPageLayout->setContentsMargins(20, 18, 20, 18);
    aiPageLayout->setSpacing(12);
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
    appFormLayout->setVerticalSpacing(10);
    appFormLayout->addRow(QStringLiteral("缩放"), m_scaleSpinBox);
    appFormLayout->addRow(QString(), m_launchAtStartupCheckBox);
    appFormLayout->addRow(QStringLiteral("性能模式"), m_performanceModeComboBox);
    appFormLayout->addRow(QString(), m_pauseWhenHiddenCheckBox);
    appFormLayout->addRow(QString(), m_enableLazyLoadCheckBox);
    appFormLayout->addRow(QString(), m_enableAnimationPrewarmCheckBox);
    appFormLayout->addRow(QStringLiteral("动画缓存上限"), m_animationCacheLimitSpinBox);
    appFormLayout->addRow(QString(), m_unloadAnimationsWhenHiddenCheckBox);

    auto *agentBridgeDivider = new QFrame(this);
    agentBridgeDivider->setFrameShape(QFrame::HLine);
    agentBridgeDivider->setFrameShadow(QFrame::Sunken);

    auto *agentBridgeTitleLabel = new QLabel(QStringLiteral("外部编程 Agent 状态联动"), this);
    agentBridgeTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto *agentBridgeHintLabel = new QLabel(
        QStringLiteral("仅接收 Codex / Antigravity / DSH 等通过 QtDesktopPetBridge 上报的只读状态摘要，不发送系统托盘通知，也不向外部 Agent 下发任务或审批权限。"),
        this);
    agentBridgeHintLabel->setObjectName(QStringLiteral("HintLabel"));
    agentBridgeHintLabel->setWordWrap(true);

    auto *agentBridgeFormLayout = new QFormLayout();
    agentBridgeFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    agentBridgeFormLayout->setLabelAlignment(Qt::AlignRight);
    agentBridgeFormLayout->setHorizontalSpacing(18);
    agentBridgeFormLayout->setVerticalSpacing(10);
    agentBridgeFormLayout->addRow(QString(), m_enableAgentBridgeCheckBox);
    agentBridgeFormLayout->addRow(QString(), m_showAgentStatusBarCheckBox);
    agentBridgeFormLayout->addRow(QStringLiteral("气泡播报策略"), m_agentBubbleNotifyLevelComboBox);
    agentBridgeFormLayout->addRow(QString(), m_agentAlertSoundEnabledCheckBox);

    auto *agentHookCard = new QFrame(this);
    agentHookCard->setObjectName(QStringLiteral("AgentHookCard"));
    auto *agentHookCardLayout = new QVBoxLayout(agentHookCard);
    agentHookCardLayout->setContentsMargins(14, 12, 14, 12);
    agentHookCardLayout->setSpacing(8);

    auto *agentHookCardTitle = new QLabel(QStringLiteral("快速接入外部编程 Agent（写入前自动备份并保留现有配置）"), agentHookCard);
    agentHookCardTitle->setStyleSheet(QStringLiteral("font-weight: 600; color: #111827;"));

    auto *codexRowLayout = new QHBoxLayout();
    codexRowLayout->setSpacing(8);
    m_codexHookNameLabel->setWordWrap(true);
    codexRowLayout->addWidget(m_codexHookNameLabel, 1);
    codexRowLayout->addWidget(m_codexHookStatusLabel);
    codexRowLayout->addWidget(m_codexHookInstallButton);
    codexRowLayout->addWidget(m_codexHookUninstallButton);

    auto *agyRowLayout = new QHBoxLayout();
    agyRowLayout->setSpacing(8);
    auto *agyNameLabel = new QLabel(QStringLiteral("Antigravity (~/.gemini/config/hooks.json)"), agentHookCard);
    agyRowLayout->addWidget(agyNameLabel, 1);
    agyRowLayout->addWidget(m_antigravityHookStatusLabel);
    agyRowLayout->addWidget(m_antigravityHookInstallButton);
    agyRowLayout->addWidget(m_antigravityHookUninstallButton);

    auto *dshRowLayout = new QHBoxLayout();
    dshRowLayout->setSpacing(8);
    auto *dshNameLabel = new QLabel(QStringLiteral("DeepSeek Harness (DSH)"), agentHookCard);
    auto *dshBadgeLabel = new QLabel(QStringLiteral("参考模板手动接入"), agentHookCard);
    dshBadgeLabel->setStyleSheet(QStringLiteral("color: #667085;"));
    dshRowLayout->addWidget(dshNameLabel, 1);
    dshRowLayout->addWidget(dshBadgeLabel);
    dshRowLayout->addWidget(m_openDshTemplatesButton);

    m_agentHookActionStatusLabel->setObjectName(QStringLiteral("TestStatusLabel"));
    m_agentHookActionStatusLabel->setWordWrap(true);
    auto *agentHookActionLayout = new QHBoxLayout();
    agentHookActionLayout->setSpacing(8);
    agentHookActionLayout->addWidget(m_refreshAgentHookButton);
    agentHookActionLayout->addWidget(m_testAgentBridgeButton);
    agentHookActionLayout->addWidget(m_agentHookActionStatusLabel, 1);

    agentHookCardLayout->addWidget(agentHookCardTitle);
    agentHookCardLayout->addLayout(codexRowLayout);
    agentHookCardLayout->addLayout(agyRowLayout);
    agentHookCardLayout->addLayout(dshRowLayout);
    agentHookCardLayout->addLayout(agentHookActionLayout);

    auto *appPageLayout = new QVBoxLayout();
    appPageLayout->setContentsMargins(20, 18, 20, 18);
    appPageLayout->setSpacing(12);
    appPageLayout->addWidget(appTitleLabel);
    appPageLayout->addLayout(appFormLayout);
    appPageLayout->addWidget(agentBridgeDivider);
    appPageLayout->addWidget(agentBridgeTitleLabel);
    appPageLayout->addWidget(agentBridgeHintLabel);
    appPageLayout->addLayout(agentBridgeFormLayout);
    appPageLayout->addWidget(agentHookCard);
    appPageLayout->addStretch();

    auto *appPage = new QWidget(this);
    appPage->setLayout(appPageLayout);

    auto *chatTitleLabel = new QLabel(QStringLiteral("聊天记录"), this);
    chatTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *chatHintLabel = new QLabel(
        QStringLiteral("内存历史上限仅控制窗口内保留的热记录数量，确保面板流畅；开启本地全量保存与长期记忆回顾后，提问时会自动从本地存档按需检索早期对话。"),
        this);
    chatHintLabel->setObjectName(QStringLiteral("HintLabel"));
    chatHintLabel->setWordWrap(true);

    auto *chatFormLayout = new QFormLayout();
    chatFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    chatFormLayout->setLabelAlignment(Qt::AlignRight);
    chatFormLayout->setFormAlignment(Qt::AlignTop);
    chatFormLayout->setHorizontalSpacing(18);
    chatFormLayout->setVerticalSpacing(10);
    chatFormLayout->addRow(QStringLiteral("请求上下文上限"), m_requestContextMessageLimitSpinBox);
    chatFormLayout->addRow(QString(), m_requestContextUseMaxCheckBox);
    chatFormLayout->addRow(QStringLiteral("请求字数安全上限"), m_requestContextMaxCharsSpinBox);
    chatFormLayout->addRow(QStringLiteral("内存历史上限"), m_memoryHistoryMessageLimitSpinBox);
    chatFormLayout->addRow(QString(), m_saveConversationHistoryCheckBox);
    chatFormLayout->addRow(QString(), m_saveAllConversationHistoryCheckBox);
    chatFormLayout->addRow(QStringLiteral("本地保存上限"), m_savedHistoryMessageLimitSpinBox);
    chatFormLayout->addRow(QString(), m_enableHistoryRecallCheckBox);

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
    m_historyListWidget->setMinimumHeight(140);
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
    chatPageLayout->setContentsMargins(20, 18, 20, 18);
    chatPageLayout->setSpacing(12);
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

    m_enableLongTermMemoryCheckBox->setChecked(m_appConfig.enableLongTermMemory);
    m_autoExtractLongTermMemoryCheckBox->setChecked(m_appConfig.autoExtractLongTermMemory);
    m_autoExtractLongTermMemoryCheckBox->setEnabled(m_enableLongTermMemoryCheckBox->isChecked());
    m_memoryCategoryFilterComboBox->addItem(QStringLiteral("全部分类"), QStringLiteral("all"));
    m_memoryCategoryFilterComboBox->addItem(QStringLiteral("个人画像"), longTermMemoryCategoryToString(LongTermMemoryCategory::Profile));
    m_memoryCategoryFilterComboBox->addItem(QStringLiteral("偏好习惯"), longTermMemoryCategoryToString(LongTermMemoryCategory::Preference));
    m_memoryCategoryFilterComboBox->addItem(QStringLiteral("项目背景"), longTermMemoryCategoryToString(LongTermMemoryCategory::Project));
    m_memoryCategoryFilterComboBox->addItem(QStringLiteral("重要事实"), longTermMemoryCategoryToString(LongTermMemoryCategory::Fact));
    m_memorySearchEdit->setPlaceholderText(QStringLiteral("搜索长期记忆内容"));
    m_memoryListWidget->setObjectName(QStringLiteral("ReminderList"));
    m_memoryListWidget->setFrameShape(QFrame::NoFrame);
    m_memoryListWidget->setMinimumHeight(140);
    m_memoryListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memoryStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_memoryStatusLabel->setWordWrap(true);

    auto *memoryTitleLabel = new QLabel(QStringLiteral("长期记忆"), this);
    memoryTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *memoryHintLabel = new QLabel(
        QStringLiteral("长期记忆会在每轮对话中自动附带，适合存放你的偏好习惯、个人画像与长期项目背景（最多 %1 条，每条最多 %2 字）。严禁写入 API Key、Token 或密码；也可以直接在聊天框说“记住我喜欢…”或“忘掉关于…的记忆”。")
            .arg(LongTermMemoryStore::DefaultMaxItems)
            .arg(LongTermMemoryStore::MaxContentChars),
        this);
    memoryHintLabel->setObjectName(QStringLiteral("HintLabel"));
    memoryHintLabel->setWordWrap(true);

    auto *memoryToggleLayout = new QVBoxLayout();
    memoryToggleLayout->setSpacing(6);
    memoryToggleLayout->addWidget(m_enableLongTermMemoryCheckBox);
    memoryToggleLayout->addWidget(m_autoExtractLongTermMemoryCheckBox);

    auto *memoryFilterLayout = new QHBoxLayout();
    memoryFilterLayout->addWidget(m_memoryCategoryFilterComboBox);
    memoryFilterLayout->addWidget(m_memorySearchEdit, 1);

    auto *memoryActionLayout = new QHBoxLayout();
    memoryActionLayout->addWidget(m_addMemoryButton);
    memoryActionLayout->addWidget(m_editMemoryButton);
    memoryActionLayout->addWidget(m_removeMemoryButton);
    memoryActionLayout->addWidget(m_clearMemoriesButton);
    memoryActionLayout->addWidget(m_memoryStatusLabel, 1);

    auto *memoryPageLayout = new QVBoxLayout();
    memoryPageLayout->setContentsMargins(20, 18, 20, 18);
    memoryPageLayout->setSpacing(12);
    memoryPageLayout->addWidget(memoryTitleLabel);
    memoryPageLayout->addWidget(memoryHintLabel);
    memoryPageLayout->addLayout(memoryToggleLayout);
    memoryPageLayout->addLayout(memoryFilterLayout);
    memoryPageLayout->addWidget(m_memoryListWidget);
    memoryPageLayout->addLayout(memoryActionLayout);
    memoryPageLayout->addStretch();

    auto *memoryPage = new QWidget(this);
    memoryPage->setLayout(memoryPageLayout);
    reloadLongTermMemoryList();

    m_weatherProviderComboBox->addItem(
        QStringLiteral("自动（Open-Meteo 优先，超时切换备用）"),
        QStringLiteral("open-meteo"));
    m_weatherProviderComboBox->addItem(
        QStringLiteral("wttr.in 优先（超时自动切换备用）"),
        QStringLiteral("wttr"));
    m_weatherProviderComboBox->addItem(
        QStringLiteral("MET Norway 优先（超时自动切换备用）"),
        QStringLiteral("met-no"));
    const QString normalizedWeatherProvider = WeatherProviderCodec::normalizedProviderSetting(m_weatherConfig.provider);
    const int weatherProviderIndex = m_weatherProviderComboBox->findData(normalizedWeatherProvider);
    m_weatherProviderComboBox->setCurrentIndex(weatherProviderIndex >= 0 ? weatherProviderIndex : 0);

    m_weatherStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_weatherStatusLabel->setWordWrap(true);
    m_weatherStatusLabel->setText(QStringLiteral("天气支持 Open-Meteo / wttr.in / MET Norway 与多源地理编码自动切换及定位缓存；建议填写市级城市名。没有默认城市时，可按公网 IP 判断城市；回复会明确说明城市来源。"));
    m_weatherDefaultCityEdit->setText(m_weatherConfig.defaultCityName);
    m_weatherDefaultCityEdit->setPlaceholderText(QStringLiteral("例如：西安 / 北京 / New York；区县乡镇不保证精确"));
    m_weatherAutoLocateCheckBox->setChecked(m_weatherConfig.autoLocateWhenNoDefault);

    auto *weatherTitleLabel = new QLabel(QStringLiteral("天气"), this);
    weatherTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *weatherHintLabel = new QLabel(QStringLiteral("天气查询内置多免 Key 数据源与地理编码缓存（Open-Meteo / Nominatim / Photon / wttr.in / MET Norway），单源超时或失败会自动切换备用源。默认城市为空时，可通过公网 IP 定位推断城市。"), this);
    weatherHintLabel->setObjectName(QStringLiteral("HintLabel"));
    weatherHintLabel->setWordWrap(true);

    auto *weatherFormLayout = new QFormLayout();
    weatherFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    weatherFormLayout->setLabelAlignment(Qt::AlignRight);
    weatherFormLayout->setFormAlignment(Qt::AlignTop);
    weatherFormLayout->setHorizontalSpacing(18);
    weatherFormLayout->setVerticalSpacing(10);
    weatherFormLayout->addRow(QStringLiteral("首选天气源"), m_weatherProviderComboBox);
    weatherFormLayout->addRow(QStringLiteral("默认城市（建议市级）"), m_weatherDefaultCityEdit);
    weatherFormLayout->addRow(QString(), m_weatherAutoLocateCheckBox);

    auto *weatherActionLayout = new QHBoxLayout();
    weatherActionLayout->addWidget(m_testWeatherDefaultCityButton);
    weatherActionLayout->addWidget(m_weatherStatusLabel, 1);

    auto *weatherPageLayout = new QVBoxLayout();
    weatherPageLayout->setContentsMargins(20, 18, 20, 18);
    weatherPageLayout->setSpacing(12);
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
    m_webStatusLabel->setText(QStringLiteral("点击“测试联网模式”验证当前配置是否连通并真正触发服务端联网；中转站配置建议先测试再使用。"));
    m_webEnabledCheckBox->setChecked(m_webConfig.enabled);
    m_webRememberToggleCheckBox->setChecked(m_webConfig.rememberLastToggle);
    m_webDefaultOnCheckBox->setChecked(m_webConfig.defaultToggleOn);
    m_webProviderModeComboBox->addItem(QStringLiteral("自动（官方自动识别）"), QStringLiteral("auto"));
    m_webProviderModeComboBox->addItem(QStringLiteral("OpenAI Responses（/v1/responses + web_search）"), QStringLiteral("openai-responses"));
    m_webProviderModeComboBox->addItem(QStringLiteral("OpenAI Chat 兼容（/chat/completions + web_search_options）"), QStringLiteral("openai-chat-search"));
    m_webProviderModeComboBox->addItem(QStringLiteral("Gemini 原生联网（google_search）"), QStringLiteral("gemini-grounding"));
    m_webProviderModeComboBox->addItem(QStringLiteral("DeepSeek 原生联网（deepseek_web_search）"), QStringLiteral("deepseek-web-tools"));
    const QString normalizedMode = WebCapabilityDetector::normalizedProviderMode(m_webConfig.providerMode);
    const int webProviderModeIndex = m_webProviderModeComboBox->findData(normalizedMode);
    m_webProviderModeComboBox->setCurrentIndex(webProviderModeIndex >= 0 ? webProviderModeIndex : 0);
    m_webTimeoutSpinBox->setRange(3000, 120000);
    m_webTimeoutSpinBox->setSingleStep(1000);
    m_webTimeoutSpinBox->setSuffix(QStringLiteral(" ms"));
    m_webTimeoutSpinBox->setValue(qBound(3000, m_webConfig.timeoutMs, 120000));
    m_webShowCitationsCheckBox->setChecked(m_webConfig.showCitations);

    auto *webTitleLabel = new QLabel(QStringLiteral("联网模式"), this);
    webTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *webHintLabel = new QLabel(QStringLiteral("普通用户只需要在输入框打开“联网”开关。自动模式支持 OpenAI 官方、DeepSeek 官方原生联网及 Gemini 原生协议；使用 OpenAI / DeepSeek 中转站或 Custom 自定义供应商时，请在下方“Provider 模式”手动选择对应协议并先点击测试。"), this);
    webHintLabel->setObjectName(QStringLiteral("HintLabel"));
    webHintLabel->setWordWrap(true);
    auto *webGlobalHintLabel = new QLabel(QStringLiteral("说明：“Provider 模式”为全局设置。为中转站手动指定协议后，若后续切换到其他服务商（例如切回 Google 或其他接口协议），请改回“自动”或切换为匹配协议，否则会提示协议不匹配。“测试联网模式”会发送实效性测试问题，同时校验接口连通性与服务端是否真正触发联网（用于识别中转站静默忽略搜索参数的“假联网”）。"), this);
    webGlobalHintLabel->setObjectName(QStringLiteral("HintLabel"));
    webGlobalHintLabel->setWordWrap(true);

    auto *webFormLayout = new QFormLayout();
    webFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    webFormLayout->setLabelAlignment(Qt::AlignRight);
    webFormLayout->setFormAlignment(Qt::AlignTop);
    webFormLayout->setHorizontalSpacing(18);
    webFormLayout->setVerticalSpacing(10);
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
    webPageLayout->setContentsMargins(20, 18, 20, 18);
    webPageLayout->setSpacing(12);
    webPageLayout->addWidget(webTitleLabel);
    webPageLayout->addWidget(webHintLabel);
    webPageLayout->addWidget(m_webCapabilityStatusLabel);
    webPageLayout->addLayout(webFormLayout);
    webPageLayout->addWidget(webGlobalHintLabel);
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
    m_reminderListWidget->setMinimumHeight(140);
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
    reminderSoundFormLayout->setVerticalSpacing(10);
    reminderSoundFormLayout->addRow(QString(), m_reminderSoundEnabledCheckBox);
    reminderSoundFormLayout->addRow(QStringLiteral("当前音效"), m_reminderSoundComboBox);
    reminderSoundFormLayout->addRow(QStringLiteral("音量"), m_reminderSoundVolumeSpinBox);

    auto *reminderSoundActionLayout = new QHBoxLayout();
    reminderSoundActionLayout->addWidget(m_importReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_deleteReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_testReminderSoundButton);
    reminderSoundActionLayout->addWidget(m_reminderSoundStatusLabel, 1);

    auto *reminderPageLayout = new QVBoxLayout();
    reminderPageLayout->setContentsMargins(20, 18, 20, 18);
    reminderPageLayout->setSpacing(12);
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
    {
        const QSignalBlocker blocker(m_characterComboBox);
        reloadCharacterList(m_appConfig.characterId);
    }

    auto *characterTitleLabel = new QLabel(QStringLiteral("角色"), this);
    characterTitleLabel->setObjectName(QStringLiteral("PageTitle"));
    auto *characterHintLabel = new QLabel(QStringLiteral("支持导入本地角色文件夹。导入前会先验证角色包，验证失败不会复制或覆盖任何文件。"), this);
    characterHintLabel->setObjectName(QStringLiteral("HintLabel"));
    characterHintLabel->setWordWrap(true);

    auto *characterFormLayout = new QFormLayout();
    characterFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    characterFormLayout->setLabelAlignment(Qt::AlignRight);
    characterFormLayout->setHorizontalSpacing(18);
    characterFormLayout->setVerticalSpacing(10);
    characterFormLayout->addRow(QStringLiteral("当前角色"), m_characterComboBox);

    auto *characterActionLayout = new QHBoxLayout();
    characterActionLayout->addWidget(m_importCharacterButton);
    characterActionLayout->addWidget(m_deleteCharacterButton);
    characterActionLayout->addWidget(m_exportCharacterButton);
    characterActionLayout->addWidget(m_openUserCharacterDirButton);
    characterActionLayout->addWidget(m_characterStatusLabel, 1);

    auto *characterPersonaDivider = new QFrame(this);
    characterPersonaDivider->setFrameShape(QFrame::HLine);
    characterPersonaDivider->setFrameShadow(QFrame::Sunken);

    auto *characterPersonaTitleLabel = new QLabel(QStringLiteral("当前角色人设"), this);
    characterPersonaTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));

    m_characterPersonaSourceLabel->setObjectName(QStringLiteral("HintLabel"));
    m_characterPersonaSourceLabel->setWordWrap(true);

    auto *characterPersonaRuleHintLabel = new QLabel(
        QStringLiteral("人设仅用于定制当前角色的身份、说话语气与口头禅；程序固定的气泡排版、提醒/天气/联网/长期记忆说明与能力安全边界会自动拼接在人设之后生效。"),
        this);
    characterPersonaRuleHintLabel->setObjectName(QStringLiteral("HintLabel"));
    characterPersonaRuleHintLabel->setWordWrap(true);

    m_characterPersonaEdit->setObjectName(QStringLiteral("CharacterPersonaEdit"));
    m_characterPersonaEdit->setMinimumHeight(120);
    m_characterPersonaEdit->setPlaceholderText(
        QStringLiteral("留空将自动使用程序内置默认人设。可在此描述角色身份、说话风格、自称、对用户称呼与口头禅（最多 1200 字）。"));

    m_characterPersonaCountLabel->setObjectName(QStringLiteral("HintLabel"));
    m_characterPersonaStatusLabel->setObjectName(QStringLiteral("HintLabel"));
    m_characterPersonaStatusLabel->setWordWrap(true);

    auto *characterPersonaActionLayout = new QHBoxLayout();
    characterPersonaActionLayout->addWidget(m_characterPersonaCountLabel);
    characterPersonaActionLayout->addStretch();
    characterPersonaActionLayout->addWidget(m_resetCharacterPersonaButton);
    characterPersonaActionLayout->addWidget(m_saveCharacterPersonaButton);

    auto *characterInteractionDivider = new QFrame(this);
    characterInteractionDivider->setFrameShape(QFrame::HLine);
    characterInteractionDivider->setFrameShadow(QFrame::Sunken);

    auto *characterInteractionTitleLabel = new QLabel(QStringLiteral("角色互动与轻关怀"), this);
    characterInteractionTitleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));

    m_enableCharacterEffectsCheckBox->setChecked(m_appConfig.enableCharacterEffects);
    m_enableTouchInteractionCheckBox->setChecked(m_appConfig.enableTouchInteraction);
    m_enableStartupGreetingCheckBox->setChecked(m_appConfig.enableStartupGreeting);
    m_enableProactiveCareCheckBox->setChecked(m_appConfig.enableProactiveCare);
    m_sedentaryReminderMinutesSpinBox->setRange(30, 180);
    m_sedentaryReminderMinutesSpinBox->setSingleStep(10);
    m_sedentaryReminderMinutesSpinBox->setSuffix(QStringLiteral(" 分钟"));
    m_sedentaryReminderMinutesSpinBox->setValue(qBound(30, m_appConfig.sedentaryReminderMinutes, 180));
    m_sedentaryReminderMinutesSpinBox->setEnabled(m_appConfig.enableProactiveCare);

    auto *characterInteractionFormLayout = new QFormLayout();
    characterInteractionFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    characterInteractionFormLayout->setLabelAlignment(Qt::AlignRight);
    characterInteractionFormLayout->setHorizontalSpacing(18);
    characterInteractionFormLayout->setVerticalSpacing(10);
    characterInteractionFormLayout->addRow(QString(), m_enableCharacterEffectsCheckBox);
    characterInteractionFormLayout->addRow(QString(), m_enableTouchInteractionCheckBox);
    characterInteractionFormLayout->addRow(QString(), m_enableStartupGreetingCheckBox);
    characterInteractionFormLayout->addRow(QString(), m_enableProactiveCareCheckBox);
    characterInteractionFormLayout->addRow(QStringLiteral("久坐关怀间隔"), m_sedentaryReminderMinutesSpinBox);

    auto *characterPageLayout = new QVBoxLayout();
    characterPageLayout->setContentsMargins(20, 18, 20, 18);
    characterPageLayout->setSpacing(12);
    characterPageLayout->addWidget(characterTitleLabel);
    characterPageLayout->addLayout(characterFormLayout);
    characterPageLayout->addLayout(characterActionLayout);
    characterPageLayout->addWidget(characterHintLabel);
    characterPageLayout->addWidget(characterPersonaDivider);
    characterPageLayout->addWidget(characterPersonaTitleLabel);
    characterPageLayout->addWidget(m_characterPersonaSourceLabel);
    characterPageLayout->addWidget(characterPersonaRuleHintLabel);
    characterPageLayout->addWidget(m_characterPersonaEdit);
    characterPageLayout->addLayout(characterPersonaActionLayout);
    characterPageLayout->addWidget(m_characterPersonaStatusLabel);
    characterPageLayout->addWidget(characterInteractionDivider);
    characterPageLayout->addWidget(characterInteractionTitleLabel);
    characterPageLayout->addLayout(characterInteractionFormLayout);
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
    navigationList->addItem(QStringLiteral("长期记忆"));
    navigationList->addItem(QStringLiteral("天气"));
    navigationList->addItem(QStringLiteral("联网"));
    navigationList->addItem(QStringLiteral("提醒"));
    navigationList->addItem(QStringLiteral("应用"));
    navigationList->addItem(QStringLiteral("角色"));

    auto *pageStack = new QStackedWidget(this);
    pageStack->setObjectName(QStringLiteral("SettingsPages"));
    pageStack->addWidget(wrapInScrollArea(aiPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(chatPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(memoryPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(weatherPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(webPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(reminderPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(appPage, pageStack));
    pageStack->addWidget(wrapInScrollArea(characterPage, pageStack));

    auto *wheelGuard = new ScrollWheelGuard(this);
    for (QComboBox *comboBox : pageStack->findChildren<QComboBox *>())
    {
        comboBox->setFocusPolicy(Qt::StrongFocus);
        comboBox->installEventFilter(wheelGuard);
    }
    for (QSpinBox *spinBox : pageStack->findChildren<QSpinBox *>())
    {
        spinBox->setFocusPolicy(Qt::StrongFocus);
        spinBox->installEventFilter(wheelGuard);
    }
    for (QDoubleSpinBox *doubleSpinBox : pageStack->findChildren<QDoubleSpinBox *>())
    {
        doubleSpinBox->setFocusPolicy(Qt::StrongFocus);
        doubleSpinBox->installEventFilter(wheelGuard);
    }

    connect(navigationList, &QListWidget::currentRowChanged, pageStack, &QStackedWidget::setCurrentIndex);
    navigationList->setCurrentRow(0);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);
    contentLayout->addWidget(navigationList);
    contentLayout->addWidget(pageStack, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(12);
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
        "QPlainTextEdit {"
        "  border: 1px solid #98a2b3; border-radius: 6px;"
        "  padding: 6px 8px; background: #ffffff; color: #111827;"
        "}"
        "QPlainTextEdit:hover {"
        "  border: 1px solid #667085;"
        "}"
        "QPlainTextEdit:focus {"
        "  border: 1.5px solid #2e90fa;"
        "}"
        "QPlainTextEdit:disabled {"
        "  background: #f2f4f7; color: #667085; border: 1px solid #d0d5dd;"
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
        "QFrame#AgentHookCard {"
        "  background: #f8fafc; border: 1px solid #eaecf0; border-radius: 8px;"
        "}"
        "QScrollArea#SettingsPageScrollArea,"
        "QScrollArea#SettingsPageScrollArea QWidget#qt_scrollarea_viewport,"
        "QWidget#SettingsPageContent {"
        "  background: transparent; border: none;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent; width: 8px; margin: 4px 2px 4px 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #d0d5dd; border-radius: 4px; min-height: 28px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #98a2b3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px; border: none; background: none;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
        "QScrollBar:horizontal {"
        "  background: transparent; height: 8px; margin: 0px 4px 2px 4px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #d0d5dd; border-radius: 4px; min-width: 28px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: #98a2b3; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0px; border: none; background: none;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
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
    connect(m_protocolComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        handleCustomProtocolChanged();
    });
    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_modelEdit, &QLineEdit::textChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_pathEdit, &QLineEdit::textChanged, this, [this]() {
        refreshWebCapabilityStatus();
    });
    connect(m_testConnectionButton, &QPushButton::clicked, this, [this]() {
        testConnection();
    });
    connect(m_resetProviderButton, &QPushButton::clicked, this, [this]() {
        resetCurrentProviderConfig();
    });
    const auto updateAnimationCacheControls = [this](bool enabled) {
        m_enableAnimationPrewarmCheckBox->setEnabled(enabled);
        m_animationCacheLimitSpinBox->setEnabled(enabled);
        m_unloadAnimationsWhenHiddenCheckBox->setEnabled(enabled);
    };
    updateAnimationCacheControls(m_enableLazyLoadCheckBox->isChecked());
    connect(m_enableLazyLoadCheckBox, &QCheckBox::toggled, this, updateAnimationCacheControls);
    connect(m_requestContextUseMaxCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_requestContextMessageLimitSpinBox->setEnabled(!checked);
    });
    const auto updateConversationSaveControls = [this]() {
        const bool saveEnabled = m_saveConversationHistoryCheckBox->isChecked();
        const bool saveAll = m_saveAllConversationHistoryCheckBox->isChecked();
        m_saveAllConversationHistoryCheckBox->setEnabled(saveEnabled);
        m_savedHistoryMessageLimitSpinBox->setEnabled(saveEnabled && !saveAll);
    };
    updateConversationSaveControls();
    connect(m_saveConversationHistoryCheckBox, &QCheckBox::toggled, this, updateConversationSaveControls);
    connect(m_saveAllConversationHistoryCheckBox, &QCheckBox::toggled, this, updateConversationSaveControls);
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
    const auto updateLongTermMemoryControls = [this](bool enabled) {
        m_autoExtractLongTermMemoryCheckBox->setEnabled(enabled);
    };
    updateLongTermMemoryControls(m_enableLongTermMemoryCheckBox->isChecked());
    connect(m_enableLongTermMemoryCheckBox, &QCheckBox::toggled, this, updateLongTermMemoryControls);
    connect(m_memoryCategoryFilterComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        reloadLongTermMemoryList();
    });
    connect(m_memorySearchEdit, &QLineEdit::textChanged, this, [this]() {
        reloadLongTermMemoryList();
    });
    connect(m_memoryListWidget, &QListWidget::currentItemChanged, this, [this]() {
        updateLongTermMemoryButtons();
    });
    connect(m_memoryListWidget, &QListWidget::itemDoubleClicked, this, [this]() {
        editSelectedLongTermMemory();
    });
    connect(m_addMemoryButton, &QPushButton::clicked, this, [this]() {
        addLongTermMemory();
    });
    connect(m_editMemoryButton, &QPushButton::clicked, this, [this]() {
        editSelectedLongTermMemory();
    });
    connect(m_removeMemoryButton, &QPushButton::clicked, this, [this]() {
        removeSelectedLongTermMemory();
    });
    connect(m_clearMemoriesButton, &QPushButton::clicked, this, [this]() {
        clearAllLongTermMemories();
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
    connect(m_characterComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        handleCharacterSelectionChanged();
    });
    connect(m_characterPersonaEdit, &QPlainTextEdit::textChanged, this, [this]() {
        updateCharacterPersonaUiState();
    });
    connect(m_saveCharacterPersonaButton, &QPushButton::clicked, this, [this]() {
        saveSelectedCharacterPersona();
    });
    connect(m_resetCharacterPersonaButton, &QPushButton::clicked, this, [this]() {
        resetSelectedCharacterPersona();
    });
    connect(m_enableProactiveCareCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_sedentaryReminderMinutesSpinBox->setEnabled(checked);
    });
    connect(m_enableAgentBridgeCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        m_showAgentStatusBarCheckBox->setEnabled(checked);
        m_agentBubbleNotifyLevelComboBox->setEnabled(checked);
        m_agentAlertSoundEnabledCheckBox->setEnabled(checked);
    });
    connect(m_codexHookInstallButton, &QPushButton::clicked, this, [this]() {
        handleInstallOrFixAgentHook(AgentHookTarget::Codex);
    });
    connect(m_codexHookUninstallButton, &QPushButton::clicked, this, [this]() {
        handleUninstallAgentHook(AgentHookTarget::Codex);
    });
    connect(m_antigravityHookInstallButton, &QPushButton::clicked, this, [this]() {
        handleInstallOrFixAgentHook(AgentHookTarget::Antigravity);
    });
    connect(m_antigravityHookUninstallButton, &QPushButton::clicked, this, [this]() {
        handleUninstallAgentHook(AgentHookTarget::Antigravity);
    });
    connect(m_openDshTemplatesButton, &QPushButton::clicked, this, [this]() {
        openAgentTemplatesDirectory();
    });
    connect(m_refreshAgentHookButton, &QPushButton::clicked, this, [this]() {
        refreshAgentHookStatuses();
        setAgentHookActionStatus(QStringLiteral("已刷新本机外部 Agent Hook 状态。"), QStringLiteral("info"));
    });
    connect(m_testAgentBridgeButton, &QPushButton::clicked, this, [this]() {
        testAgentBridgeConnection();
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
    refreshAgentHookStatuses();
    loadSelectedCharacterPersona();
    captureInitialSettingsSnapshot();
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
    if (m_agentBridgeTestProcess != nullptr)
    {
        m_agentBridgeTestProcess->disconnect(this);
        if (m_agentBridgeTestProcess->state() != QProcess::NotRunning)
        {
            m_agentBridgeTestProcess->kill();
            m_agentBridgeTestProcess->waitForFinished(300);
        }
        m_agentBridgeTestProcess = nullptr;
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
    config.requestContextUseMax = m_requestContextUseMaxCheckBox->isChecked();
    config.requestContextMaxChars = m_requestContextMaxCharsSpinBox->value();
    config.memoryHistoryMessageLimit = m_memoryHistoryMessageLimitSpinBox->value();
    config.saveConversationHistory = m_saveConversationHistoryCheckBox->isChecked();
    config.saveAllConversationHistory = m_saveAllConversationHistoryCheckBox->isChecked();
    config.savedHistoryMessageLimit = m_savedHistoryMessageLimitSpinBox->value();
    config.enableHistoryRecall = m_enableHistoryRecallCheckBox->isChecked();
    config.enableLongTermMemory = m_enableLongTermMemoryCheckBox->isChecked();
    config.autoExtractLongTermMemory = m_autoExtractLongTermMemoryCheckBox->isChecked();
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
    config.enableCharacterEffects = m_enableCharacterEffectsCheckBox->isChecked();
    config.enableTouchInteraction = m_enableTouchInteractionCheckBox->isChecked();
    config.enableStartupGreeting = m_enableStartupGreetingCheckBox->isChecked();
    config.enableProactiveCare = m_enableProactiveCareCheckBox->isChecked();
    config.sedentaryReminderMinutes = qBound(30, m_sedentaryReminderMinutesSpinBox->value(), 180);
    config.enableAgentBridge = m_enableAgentBridgeCheckBox->isChecked();
    config.showAgentStatusBar = m_showAgentStatusBarCheckBox->isChecked();
    config.agentBubbleNotifyLevel = m_agentBubbleNotifyLevelComboBox->currentData().toString().trimmed();
    if (config.agentBubbleNotifyLevel.isEmpty())
    {
        config.agentBubbleNotifyLevel = QStringLiteral("attention");
    }
    config.agentAlertSoundEnabled = m_agentAlertSoundEnabledCheckBox->isChecked();
    return config;
}

WeatherConfig SettingsDialog::weatherConfig() const
{
    WeatherConfig config = m_weatherConfig;
    config.provider = WeatherProviderCodec::normalizedProviderSetting(
        m_weatherProviderComboBox != nullptr
            ? m_weatherProviderComboBox->currentData().toString()
            : config.provider);
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
    config.providerMode = WebCapabilityDetector::normalizedProviderMode(
        m_webProviderModeComboBox->currentData().toString());
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

        if (!response.usedWeb)
        {
            dialog->m_webStatusLabel->setText(
                QStringLiteral("警告：接口返回成功，但未检测到服务端联网痕迹或来源引用（实际联网：否）。当前中转站或模型可能忽略了搜索参数，请确认所选联网协议、模型是否支持联网，或更换支持透传的中转渠道。"));
            return;
        }

        if (response.citations.isEmpty())
        {
            dialog->m_webStatusLabel->setText(
                QStringLiteral("联网模式测试成功：已检测到服务端联网调用，但未返回可展示来源（来源数：0）。"));
            return;
        }

        dialog->m_webStatusLabel->setText(QStringLiteral("联网模式测试成功：已触发服务端联网，返回来源数：%1。")
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

    if (hasUnsavedCharacterPersonaDraft())
    {
        QMessageBox box(
            QMessageBox::Question,
            QStringLiteral("未保存的角色人设草稿"),
            QStringLiteral("角色 \"%1\" 的人设已修改但尚未点击“保存人设”。是否先保存人设草稿再关闭设置？")
                .arg(m_loadedPersonaCharacterId),
            QMessageBox::NoButton,
            this);
        QPushButton *savePersonaButton = box.addButton(QStringLiteral("保存人设并继续"), QMessageBox::AcceptRole);
        QPushButton *discardPersonaButton = box.addButton(QStringLiteral("忽略草稿并继续"), QMessageBox::DestructiveRole);
        QPushButton *backButton = box.addButton(QStringLiteral("返回编辑"), QMessageBox::RejectRole);
        box.setDefaultButton(savePersonaButton);
        box.setEscapeButton(backButton);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        if (clicked == backButton)
        {
            return;
        }

        if (clicked == savePersonaButton)
        {
            if (!saveSelectedCharacterPersona(false))
            {
                return;
            }
        }
        else if (clicked != discardPersonaButton)
        {
            return;
        }
    }

    m_acceptedConfigStore = store;
    m_hasAcceptedConfigStore = true;
    QDialog::accept();
}

void SettingsDialog::reject()
{
    if (!hasUnsavedChanges())
    {
        QDialog::reject();
        return;
    }

    QMessageBox box(
        QMessageBox::Question,
        QStringLiteral("未保存的设置"),
        QStringLiteral("当前有尚未保存的设置修改，是否在关闭前保存？"),
        QMessageBox::NoButton,
        this);
    QPushButton *saveButton = box.addButton(QStringLiteral("保存并关闭"), QMessageBox::AcceptRole);
    QPushButton *discardButton = box.addButton(QStringLiteral("不保存"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("继续编辑"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancelButton);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == saveButton)
    {
        accept();
        return;
    }

    if (clicked == discardButton)
    {
        QDialog::reject();
    }
}

void SettingsDialog::closeEvent(QCloseEvent *event)
{
    reject();
    if (isVisible())
    {
        event->ignore();
        return;
    }

    event->accept();
}

SettingsDialog::ProviderSnapshot SettingsDialog::providerSnapshotFromConfig(
    const QString &provider,
    const AIConfig &config) const
{
    const QString normalizedProvider = normalizedProviderName(provider);
    ProviderSnapshot snapshot;
    if (normalizedProvider == QStringLiteral("custom"))
    {
        const QString protocol = config.protocol.trimmed();
        snapshot.protocol = (protocol == QStringLiteral("google-generative-language")
                             || protocol == QStringLiteral("openai-compatible"))
            ? protocol
            : QStringLiteral("openai-compatible");
    }
    else
    {
        snapshot.protocol = defaultAIConfigForProvider(normalizedProvider).protocol;
    }

    snapshot.baseUrl = config.baseUrl.trimmed();
    snapshot.apiKey = decryptedApiKey(config);
    snapshot.model = config.model.trimmed();
    snapshot.path = config.path.trimmed();
    snapshot.timeoutMs = qBound(1000, config.timeoutMs, 300000);
    snapshot.temperatureHundredths = qBound(0, qRound(config.temperature * 100.0), 200);
    snapshot.maxTokens = qBound(1, config.maxTokens, 200000);
    snapshot.allowPlainApiKey = config.allowPlainApiKey;
    return snapshot;
}

QMap<QString, SettingsDialog::ProviderSnapshot> SettingsDialog::currentProviderSnapshots() const
{
    QMap<QString, ProviderSnapshot> snapshots;
    const QString activeFormProvider = normalizedProviderName(m_providerComboBox->currentData().toString());
    const QStringList allProviders = {
        QStringLiteral("openai"),
        QStringLiteral("google"),
        QStringLiteral("deepseek"),
        QStringLiteral("custom"),
    };

    for (const QString &provider : allProviders)
    {
        if (provider == activeFormProvider)
        {
            ProviderSnapshot snapshot;
            snapshot.protocol = activeFormProtocol(provider);
            snapshot.baseUrl = m_baseUrlEdit->text().trimmed();
            snapshot.apiKey = m_apiKeyEdit->text();
            snapshot.model = m_modelEdit->text().trimmed();
            snapshot.path = m_pathEdit->text().trimmed();
            snapshot.timeoutMs = m_timeoutSpinBox->value();
            snapshot.temperatureHundredths = qRound(m_temperatureSpinBox->value() * 100.0);
            snapshot.maxTokens = m_maxTokensSpinBox->value();
            snapshot.allowPlainApiKey = m_allowPlainApiKeyCheckBox->isChecked();
            snapshots.insert(provider, snapshot);
            continue;
        }

        const AIConfig config = m_configStore.providers.value(provider, defaultAIConfigForProvider(provider));
        snapshots.insert(provider, providerSnapshotFromConfig(provider, config));
    }

    return snapshots;
}

void SettingsDialog::captureInitialSettingsSnapshot()
{
    m_initialActiveProvider = normalizedProviderName(m_providerComboBox->currentData().toString());
    m_initialProviderSnapshots = currentProviderSnapshots();
    m_initialAppConfig = appConfig();
    m_initialWeatherConfig = weatherConfig();
    m_initialWebConfig = webConfig();
}

bool SettingsDialog::hasUnsavedChanges() const
{
    const QString currentActiveProvider = normalizedProviderName(m_providerComboBox->currentData().toString());
    if (currentActiveProvider != m_initialActiveProvider)
    {
        return true;
    }

    const QMap<QString, ProviderSnapshot> currentSnapshots = currentProviderSnapshots();
    if (currentSnapshots.size() != m_initialProviderSnapshots.size())
    {
        return true;
    }

    for (auto it = m_initialProviderSnapshots.constBegin(); it != m_initialProviderSnapshots.constEnd(); ++it)
    {
        const auto currentIt = currentSnapshots.constFind(it.key());
        if (currentIt == currentSnapshots.constEnd())
        {
            return true;
        }

        const ProviderSnapshot &initial = it.value();
        const ProviderSnapshot &current = currentIt.value();
        if (initial.protocol != current.protocol
            || initial.baseUrl != current.baseUrl
            || initial.apiKey != current.apiKey
            || initial.model != current.model
            || initial.path != current.path
            || initial.timeoutMs != current.timeoutMs
            || initial.temperatureHundredths != current.temperatureHundredths
            || initial.maxTokens != current.maxTokens
            || initial.allowPlainApiKey != current.allowPlainApiKey)
        {
            return true;
        }
    }

    if (!sameAppConfigForSettings(appConfig(), m_initialAppConfig)
        || !sameWeatherConfigForSettings(weatherConfig(), m_initialWeatherConfig)
        || !sameWebConfigForSettings(webConfig(), m_initialWebConfig)
        || hasUnsavedCharacterPersonaDraft())
    {
        return true;
    }

    return false;
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

    updateCustomProtocolUi(normalizedProvider, config.protocol);
    updateResetProviderButtonText(normalizedProvider);
    m_baseUrlEdit->setText(config.baseUrl);
    m_apiKeyEdit->setText(decryptedApiKey(config));
    m_modelEdit->setText(config.model);
    m_pathEdit->setText(config.path);
    m_timeoutSpinBox->setValue(config.timeoutMs);
    m_temperatureSpinBox->setValue(config.temperature);
    m_maxTokensSpinBox->setValue(config.maxTokens);
    m_allowPlainApiKeyCheckBox->setChecked(config.allowPlainApiKey);
}

QString SettingsDialog::activeFormProtocol(const QString &provider) const
{
    const QString normalizedProvider = normalizedProviderName(provider);
    if (normalizedProvider != QStringLiteral("custom"))
    {
        return defaultAIConfigForProvider(normalizedProvider).protocol;
    }

    const QString selectedProtocol = m_protocolComboBox->currentData().toString().trimmed();
    if (selectedProtocol == QStringLiteral("google-generative-language")
        || selectedProtocol == QStringLiteral("openai-compatible"))
    {
        return selectedProtocol;
    }

    return QStringLiteral("openai-compatible");
}

void SettingsDialog::updateCustomProtocolUi(const QString &provider, const QString &protocol)
{
    const bool isCustom = normalizedProviderName(provider) == QStringLiteral("custom");
    if (m_protocolRowLabel != nullptr)
    {
        m_protocolRowLabel->setVisible(isCustom);
    }
    if (m_protocolComboBox == nullptr)
    {
        return;
    }

    const QSignalBlocker blocker(m_protocolComboBox);
    m_protocolComboBox->setVisible(isCustom);
    const QString targetProtocol = isCustom
        ? protocol.trimmed()
        : defaultAIConfigForProvider(provider).protocol;
    int index = m_protocolComboBox->findData(targetProtocol);
    if (index < 0)
    {
        index = m_protocolComboBox->findData(QStringLiteral("openai-compatible"));
    }
    m_protocolComboBox->setCurrentIndex(index >= 0 ? index : 0);
}

void SettingsDialog::handleCustomProtocolChanged()
{
    if (m_currentProvider != QStringLiteral("custom"))
    {
        return;
    }

    const QString protocol = activeFormProtocol(m_currentProvider);
    const QString currentPath = m_pathEdit->text().trimmed();
    const QString openAIDefaultPath = QStringLiteral("/chat/completions");
    const QString geminiDefaultPath = QStringLiteral("/v1beta/models/{model}:generateContent");

    if (protocol == QStringLiteral("google-generative-language"))
    {
        if (currentPath.isEmpty() || currentPath == openAIDefaultPath)
        {
            const QSignalBlocker pathBlocker(m_pathEdit);
            m_pathEdit->setText(geminiDefaultPath);
        }
    }
    else if (currentPath.isEmpty() || currentPath == geminiDefaultPath)
    {
        const QSignalBlocker pathBlocker(m_pathEdit);
        m_pathEdit->setText(openAIDefaultPath);
    }

    refreshWebCapabilityStatus();
}

void SettingsDialog::updateResetProviderButtonText(const QString &provider)
{
    if (m_resetProviderButton == nullptr)
    {
        return;
    }

    const bool isCustom = normalizedProviderName(provider) == QStringLiteral("custom");
    m_resetProviderButton->setText(isCustom
        ? QStringLiteral("清空当前配置")
        : QStringLiteral("恢复官方默认"));
}

void SettingsDialog::resetCurrentProviderConfig()
{
    const QString provider = normalizedProviderName(m_providerComboBox->currentData().toString());
    const bool isCustom = provider == QStringLiteral("custom");
    const QString title = isCustom ? QStringLiteral("清空当前配置") : QStringLiteral("恢复官方默认");
    const QString prompt = isCustom
        ? QStringLiteral("确定要清空 Custom 的 Base URL、API Key、Model 和 Path，并将接口协议与数值参数恢复默认吗？\n\n点击 Save 后才会正式保存。")
        : QStringLiteral("确定要清空当前服务商的 API Key 和 Model，并将 Base URL、Path 与数值参数恢复为官方默认值吗？\n\n点击 Save 后才会正式保存。");

    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        title,
        prompt,
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
    {
        return;
    }

    if (m_testProvider && m_testProvider->isBusy())
    {
        m_testProvider->cancel();
        m_testConnectionButton->setEnabled(true);
    }
    if (m_webTestManager && m_webTestManager->isBusy())
    {
        m_webTestManager->cancel();
        m_testWebButton->setEnabled(true);
        m_webStatusLabel->setText(QStringLiteral("联网测试已取消。"));
    }

    AIConfig resetConfig = defaultAIConfigForProvider(provider);
    if (isCustom)
    {
        resetConfig.path.clear();
    }

    m_configStore.providers.insert(provider, resetConfig);
    m_currentProvider = provider;
    loadProviderConfig(provider);
    refreshWebCapabilityStatus();
    setTestStatus(
        isCustom
            ? QStringLiteral("已清空当前 Custom 配置，点击 Save 后生效。")
            : QStringLiteral("已恢复官方默认配置，点击 Save 后生效。"),
        QStringLiteral("info"));
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
    m_currentProvider = normalizedProvider;
    loadProviderConfig(normalizedProvider);
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
    config.protocol = activeFormProtocol(normalizedProvider);
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
    config.protocol = activeFormProtocol(normalizedProvider);
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
    constexpr int MaxVisibleHistoryItems = 200;
    m_historyListWidget->clear();
    const QVector<ChatMessage> messages = filteredConversationHistory();
    const int totalCount = static_cast<int>(messages.size());
    const int startIndex = qMax(0, totalCount - MaxVisibleHistoryItems);
    if (startIndex > 0)
    {
        auto *noticeItem = new QListWidgetItem(
            QStringLiteral("共匹配 %1 条记录，为保持界面流畅仅展示最近 %2 条；导出功能包含全部匹配记录。")
                .arg(totalCount)
                .arg(MaxVisibleHistoryItems),
            m_historyListWidget);
        noticeItem->setFlags(noticeItem->flags() & ~Qt::ItemIsSelectable);
    }

    for (int index = startIndex; index < totalCount; ++index)
    {
        const ChatMessage &message = messages.at(index);
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

void SettingsDialog::reloadLongTermMemoryList()
{
    m_memoryListWidget->clear();
    const QString categoryFilter = m_memoryCategoryFilterComboBox->currentData().toString();
    const QString keyword = m_memorySearchEdit->text().trimmed();

    QVector<LongTermMemoryItem> ordered = m_longTermMemories;
    std::stable_sort(ordered.begin(), ordered.end(), [](const LongTermMemoryItem &left, const LongTermMemoryItem &right) {
        const qint64 leftTs = left.updatedAt.isValid() ? left.updatedAt.toMSecsSinceEpoch() : -1;
        const qint64 rightTs = right.updatedAt.isValid() ? right.updatedAt.toMSecsSinceEpoch() : -1;
        return leftTs > rightTs;
    });

    int visibleCount = 0;
    for (const LongTermMemoryItem &item : ordered)
    {
        if (categoryFilter != QStringLiteral("all")
            && longTermMemoryCategoryToString(item.category) != categoryFilter)
        {
            continue;
        }
        if (!keyword.isEmpty() && !item.content.contains(keyword, Qt::CaseInsensitive))
        {
            continue;
        }

        const QString title = QStringLiteral("[%1] (%2)  %3  %4")
                                  .arg(longTermMemoryCategoryDisplayName(item.category),
                                       longTermMemorySourceDisplayName(item.source),
                                       longTermMemoryTimeText(item.updatedAt),
                                       item.content);
        auto *listItem = new QListWidgetItem(title, m_memoryListWidget);
        listItem->setData(Qt::UserRole, item.id);
        listItem->setToolTip(title);
        ++visibleCount;
    }

    if (m_longTermMemories.isEmpty())
    {
        m_memoryStatusLabel->setText(
            QStringLiteral("当前共 0 / %1 条长期记忆。").arg(LongTermMemoryStore::DefaultMaxItems));
    }
    else
    {
        m_memoryStatusLabel->setText(
            QStringLiteral("当前共 %1 / %2 条长期记忆（显示 %3 条）。")
                .arg(m_longTermMemories.size())
                .arg(LongTermMemoryStore::DefaultMaxItems)
                .arg(visibleCount));
    }
    updateLongTermMemoryButtons();
}

void SettingsDialog::updateLongTermMemoryButtons()
{
    const bool hasSelection = m_memoryListWidget->currentItem() != nullptr;
    m_editMemoryButton->setEnabled(hasSelection);
    m_removeMemoryButton->setEnabled(hasSelection);
    m_clearMemoriesButton->setEnabled(!m_longTermMemories.isEmpty());
}

void SettingsDialog::addLongTermMemory()
{
    LongTermMemoryEditDialog dialog(nullptr, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    LongTermMemoryItem savedItem;
    QString errorMessage;
    if (!m_longTermMemoryCallbacks.addItem
        || !m_longTermMemoryCallbacks.addItem(dialog.content(), dialog.category(), &savedItem, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("新增长期记忆失败"),
            errorMessage.isEmpty() ? QStringLiteral("保存长期记忆失败。") : errorMessage);
        return;
    }

    bool replaced = false;
    for (LongTermMemoryItem &existing : m_longTermMemories)
    {
        if (existing.id == savedItem.id)
        {
            existing = savedItem;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        if (m_longTermMemories.size() >= LongTermMemoryStore::DefaultMaxItems)
        {
            int oldestAutoIndex = -1;
            qint64 oldestAutoTs = std::numeric_limits<qint64>::max();
            for (int index = 0; index < m_longTermMemories.size(); ++index)
            {
                if (m_longTermMemories.at(index).source != LongTermMemorySource::Auto)
                {
                    continue;
                }
                const qint64 ts = m_longTermMemories.at(index).updatedAt.isValid()
                    ? m_longTermMemories.at(index).updatedAt.toMSecsSinceEpoch()
                    : 0;
                if (oldestAutoIndex < 0 || ts < oldestAutoTs)
                {
                    oldestAutoIndex = index;
                    oldestAutoTs = ts;
                }
            }
            if (oldestAutoIndex >= 0)
            {
                m_longTermMemories.removeAt(oldestAutoIndex);
            }
        }
        m_longTermMemories.prepend(savedItem);
    }

    reloadLongTermMemoryList();
    m_memoryStatusLabel->setText(
        replaced
            ? QStringLiteral("已更新内容相同的长期记忆（当前 %1 / %2 条）。")
                  .arg(m_longTermMemories.size())
                  .arg(LongTermMemoryStore::DefaultMaxItems)
            : QStringLiteral("已新增长期记忆（当前 %1 / %2 条）。")
                  .arg(m_longTermMemories.size())
                  .arg(LongTermMemoryStore::DefaultMaxItems));
}

void SettingsDialog::editSelectedLongTermMemory()
{
    QListWidgetItem *listItem = m_memoryListWidget->currentItem();
    if (listItem == nullptr)
    {
        return;
    }

    const QString memoryId = listItem->data(Qt::UserRole).toString();
    const LongTermMemoryItem *targetItem = findLongTermMemoryById(m_longTermMemories, memoryId);
    if (targetItem == nullptr)
    {
        return;
    }

    LongTermMemoryEditDialog dialog(targetItem, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    LongTermMemoryItem updatedItem;
    QString errorMessage;
    if (!m_longTermMemoryCallbacks.updateItem
        || !m_longTermMemoryCallbacks.updateItem(memoryId, dialog.content(), dialog.category(), &updatedItem, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("编辑长期记忆失败"),
            errorMessage.isEmpty() ? QStringLiteral("更新长期记忆失败。") : errorMessage);
        return;
    }

    for (LongTermMemoryItem &existing : m_longTermMemories)
    {
        if (existing.id == memoryId)
        {
            existing = updatedItem;
            break;
        }
    }

    reloadLongTermMemoryList();
    m_memoryStatusLabel->setText(QStringLiteral("已更新选中长期记忆。"));
}

void SettingsDialog::removeSelectedLongTermMemory()
{
    QListWidgetItem *listItem = m_memoryListWidget->currentItem();
    if (listItem == nullptr)
    {
        return;
    }

    const QString memoryId = listItem->data(Qt::UserRole).toString();
    const LongTermMemoryItem *targetItem = findLongTermMemoryById(m_longTermMemories, memoryId);
    const QString preview = targetItem != nullptr ? targetItem->content : QString();
    const QMessageBox::StandardButton result = QMessageBox::question(
        this,
        QStringLiteral("删除长期记忆"),
        preview.isEmpty()
            ? QStringLiteral("确定要删除选中的长期记忆吗？")
            : QStringLiteral("确定要删除这条长期记忆吗？\n\n%1").arg(preview));
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!m_longTermMemoryCallbacks.removeItem || !m_longTermMemoryCallbacks.removeItem(memoryId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("删除长期记忆失败"),
            errorMessage.isEmpty() ? QStringLiteral("删除长期记忆失败。") : errorMessage);
        return;
    }

    for (int index = m_longTermMemories.size() - 1; index >= 0; --index)
    {
        if (m_longTermMemories.at(index).id == memoryId)
        {
            m_longTermMemories.removeAt(index);
        }
    }

    reloadLongTermMemoryList();
    m_memoryStatusLabel->setText(QStringLiteral("已删除选中长期记忆。"));
}

void SettingsDialog::clearAllLongTermMemories()
{
    if (m_longTermMemories.isEmpty())
    {
        return;
    }

    const QMessageBox::StandardButton result = QMessageBox::warning(
        this,
        QStringLiteral("清空长期记忆"),
        QStringLiteral("确定要清空全部 %1 条长期记忆吗？\n\n清空后将删除本地长期记忆本，此操作不可恢复。")
            .arg(m_longTermMemories.size()),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!m_longTermMemoryCallbacks.clearAll || !m_longTermMemoryCallbacks.clearAll(&errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("清空长期记忆失败"),
            errorMessage.isEmpty() ? QStringLiteral("清空长期记忆失败。") : errorMessage);
        return;
    }

    m_longTermMemories.clear();
    reloadLongTermMemoryList();
    m_memoryStatusLabel->setText(QStringLiteral("已清空全部长期记忆。"));
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

    m_suppressCharacterSelectionPrompt = true;
    {
        const QSignalBlocker blocker(m_characterComboBox);
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
    m_suppressCharacterSelectionPrompt = false;
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

    if (!confirmLeaveUnsavedCharacterPersonaDraft(QStringLiteral("删除用户角色")))
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
    loadSelectedCharacterPersona();
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

    if (!confirmLeaveUnsavedCharacterPersonaDraft(QStringLiteral("导入角色")))
    {
        return;
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
    loadSelectedCharacterPersona();
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

void SettingsDialog::loadSelectedCharacterPersona()
{
    const QString characterId = m_characterComboBox->currentData().toString().trimmed();
    m_loadedPersonaCharacterId = characterId;
    m_loadedPersonaIsUserPackage = !characterId.isEmpty() && CharacterPackageRepository::hasUserPackage(characterId);

    const ResolvedCharacterPersona resolved = characterId.isEmpty()
        ? ResolvedCharacterPersona{}
        : CharacterPackageRepository::resolveCharacterPersona(characterId);
    m_savedCharacterPersona = resolved.persona;
    m_savedPackagePersona = resolved.packagePersona;
    m_loadedPersonaSource = resolved.source;
    m_loadedPersonaHasUserOverride = resolved.hasUserOverride;

    {
        const QSignalBlocker blocker(m_characterPersonaEdit);
        m_characterPersonaEdit->setPlainText(m_savedCharacterPersona);
    }

    m_characterPersonaStatusLabel->clear();
    updateCharacterPersonaUiState();
}

void SettingsDialog::updateCharacterPersonaUiState()
{
    QString sourceText;
    switch (m_loadedPersonaSource)
    {
    case CharacterPersonaSource::UserOverride:
        sourceText = QStringLiteral("当前人设来源：用户自定义覆盖（保存于用户配置目录，不影响内置角色包）");
        break;
    case CharacterPersonaSource::PackageBuiltIn:
        sourceText = m_loadedPersonaIsUserPackage
            ? QStringLiteral("当前人设来源：用户角色包自带（character.json）")
            : QStringLiteral("当前人设来源：角色包自带（character.json）");
        break;
    case CharacterPersonaSource::ProgramDefault:
        sourceText = QStringLiteral("当前人设来源：程序默认兜底（角色包未内置人设）");
        break;
    }
    m_characterPersonaSourceLabel->setText(sourceText);

    const QString draftText = CharacterPersonaStore::normalizeDraftText(m_characterPersonaEdit->toPlainText());
    const int charCount = draftText.size();
    const bool overLimit = charCount > CharacterPersonaMaxChars;
    const bool dirty = hasUnsavedCharacterPersonaDraft();
    QString countText = QStringLiteral("当前字数：%1 / %2 字").arg(charCount).arg(CharacterPersonaMaxChars);
    if (overLimit)
    {
        countText += QStringLiteral("（已超出上限）");
        m_characterPersonaCountLabel->setStyleSheet(QStringLiteral("color: #c62828; font-weight: 600;"));
    }
    else if (dirty)
    {
        countText += QStringLiteral("（有未保存草稿）");
        m_characterPersonaCountLabel->setStyleSheet(QString());
    }
    else
    {
        m_characterPersonaCountLabel->setStyleSheet(QString());
    }
    m_characterPersonaCountLabel->setText(countText);

    const bool hasValidCharacter = !m_loadedPersonaCharacterId.isEmpty();
    m_characterPersonaEdit->setEnabled(hasValidCharacter);
    m_saveCharacterPersonaButton->setEnabled(hasValidCharacter && !overLimit && dirty);

    if (m_loadedPersonaIsUserPackage)
    {
        m_resetCharacterPersonaButton->setText(QStringLiteral("恢复程序默认"));
        m_resetCharacterPersonaButton->setEnabled(
            hasValidCharacter && (!m_savedCharacterPersona.isEmpty() || m_loadedPersonaHasUserOverride));
    }
    else
    {
        m_resetCharacterPersonaButton->setText(QStringLiteral("恢复角色包默认"));
        m_resetCharacterPersonaButton->setEnabled(hasValidCharacter && m_loadedPersonaHasUserOverride);
    }
}

bool SettingsDialog::hasUnsavedCharacterPersonaDraft() const
{
    if (m_loadedPersonaCharacterId.isEmpty())
    {
        return false;
    }

    const QString draftNormalized = CharacterPersonaStore::normalizeDraftText(m_characterPersonaEdit->toPlainText());
    const QString savedNormalized = CharacterPersonaStore::normalizeDraftText(m_savedCharacterPersona);
    return draftNormalized != savedNormalized;
}

bool SettingsDialog::saveSelectedCharacterPersona(bool showSuccessStatus)
{
    const QString characterId = m_loadedPersonaCharacterId.trimmed();
    if (characterId.isEmpty())
    {
        return false;
    }

    const QString draftText = CharacterPersonaStore::normalizeDraftText(m_characterPersonaEdit->toPlainText());
    if (draftText.size() > CharacterPersonaMaxChars)
    {
        QMessageBox::warning(
            this,
            QStringLiteral("人设字数超限"),
            QStringLiteral("当前人设共 %1 字，超过 %2 字上限，请精简后再保存。")
                .arg(draftText.size())
                .arg(CharacterPersonaMaxChars));
        return false;
    }

    const QString storageDescription = m_loadedPersonaIsUserPackage
        ? QStringLiteral("将原子更新用户角色包 %1 的 character.json 中的 persona 字段（保留其余配置字段），并清理同 ID 覆盖层条目。").arg(characterId)
        : QStringLiteral("内置角色包目录保持只读；自定义人设将写入用户配置目录的 character_personas.json 覆盖层。");
    const QString emptyActionHint = draftText.isEmpty()
        ? QStringLiteral("\n\n当前编辑框为空，保存后将清除自定义人设并回退默认人设。")
        : QString();

    const QMessageBox::StandardButton confirm = QMessageBox::question(
        this,
        QStringLiteral("确认保存角色人设"),
        QStringLiteral("确定要保存角色 \"%1\" 的人设吗？\n\n%2\n程序固定能力提示词与安全边界会自动拼接在人设之后生效。%3")
            .arg(characterId, storageDescription, emptyActionHint),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Yes);
    if (confirm != QMessageBox::Yes)
    {
        return false;
    }

    QString errorMessage;
    if (!CharacterPackageRepository::saveCharacterPersona(characterId, draftText, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("保存人设失败"),
            errorMessage.isEmpty() ? QStringLiteral("写入角色人设失败。") : errorMessage);
        return false;
    }

    loadSelectedCharacterPersona();
    if (showSuccessStatus)
    {
        m_characterPersonaStatusLabel->setText(
            draftText.isEmpty()
                ? QStringLiteral("已清除 \"%1\" 的自定义人设并回退默认设置。").arg(characterId)
                : QStringLiteral("已保存 \"%1\" 的角色人设。").arg(characterId));
    }

    if (m_characterPersonaChanged)
    {
        m_characterPersonaChanged(characterId);
    }
    return true;
}

void SettingsDialog::resetSelectedCharacterPersona()
{
    const QString characterId = m_loadedPersonaCharacterId.trimmed();
    if (characterId.isEmpty())
    {
        return;
    }

    const QString promptText = m_loadedPersonaIsUserPackage
        ? QStringLiteral("确定要清除用户角色 \"%1\" 的人设并恢复为程序默认兜底吗？\n\n这会移除该用户角色包 character.json 中的 persona 字段。")
              .arg(characterId)
        : QStringLiteral("确定要清除内置角色 \"%1\" 的用户自定义覆盖并恢复角色包默认吗？")
              .arg(characterId);

    const QMessageBox::StandardButton confirm = QMessageBox::question(
        this,
        QStringLiteral("恢复默认人设"),
        promptText,
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (confirm != QMessageBox::Yes)
    {
        return;
    }

    QString errorMessage;
    if (!CharacterPackageRepository::resetCharacterPersonaToDefault(characterId, &errorMessage))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("恢复默认失败"),
            errorMessage.isEmpty() ? QStringLiteral("恢复默认人设失败。") : errorMessage);
        return;
    }

    loadSelectedCharacterPersona();
    m_characterPersonaStatusLabel->setText(
        m_loadedPersonaIsUserPackage
            ? QStringLiteral("已恢复 \"%1\" 为程序默认人设。").arg(characterId)
            : QStringLiteral("已恢复 \"%1\" 的角色包默认人设。").arg(characterId));

    if (m_characterPersonaChanged)
    {
        m_characterPersonaChanged(characterId);
    }
}

bool SettingsDialog::handleCharacterSelectionChanged()
{
    if (m_suppressCharacterSelectionPrompt)
    {
        return true;
    }

    const QString targetCharacterId = m_characterComboBox->currentData().toString().trimmed();
    if (targetCharacterId == m_loadedPersonaCharacterId)
    {
        return true;
    }

    if (hasUnsavedCharacterPersonaDraft())
    {
        QMessageBox box(
            QMessageBox::Question,
            QStringLiteral("未保存的角色人设草稿"),
            QStringLiteral("角色 \"%1\" 的人设尚未保存，切换角色前要如何处理？")
                .arg(m_loadedPersonaCharacterId),
            QMessageBox::NoButton,
            this);
        QPushButton *saveButton = box.addButton(QStringLiteral("保存人设并切换"), QMessageBox::AcceptRole);
        QPushButton *discardButton = box.addButton(QStringLiteral("放弃草稿并切换"), QMessageBox::DestructiveRole);
        QPushButton *cancelButton = box.addButton(QStringLiteral("取消切换"), QMessageBox::RejectRole);
        box.setDefaultButton(saveButton);
        box.setEscapeButton(cancelButton);
        box.exec();

        QAbstractButton *clicked = box.clickedButton();
        bool allowSwitch = false;
        if (clicked == saveButton)
        {
            allowSwitch = saveSelectedCharacterPersona(false);
        }
        else if (clicked == discardButton)
        {
            allowSwitch = true;
        }

        if (!allowSwitch)
        {
            m_suppressCharacterSelectionPrompt = true;
            {
                const QSignalBlocker blocker(m_characterComboBox);
                const int previousIndex = m_characterComboBox->findData(m_loadedPersonaCharacterId);
                if (previousIndex >= 0)
                {
                    m_characterComboBox->setCurrentIndex(previousIndex);
                }
            }
            m_suppressCharacterSelectionPrompt = false;
            return false;
        }
    }

    loadSelectedCharacterPersona();
    return true;
}

bool SettingsDialog::confirmLeaveUnsavedCharacterPersonaDraft(const QString &actionDescription)
{
    if (!hasUnsavedCharacterPersonaDraft())
    {
        return true;
    }

    QMessageBox box(
        QMessageBox::Question,
        QStringLiteral("未保存的角色人设草稿"),
        QStringLiteral("角色 \"%1\" 存在未保存的人设草稿，在%2前要如何处理？")
            .arg(m_loadedPersonaCharacterId, actionDescription),
        QMessageBox::NoButton,
        this);
    QPushButton *saveButton = box.addButton(QStringLiteral("保存人设并继续"), QMessageBox::AcceptRole);
    QPushButton *discardButton = box.addButton(QStringLiteral("放弃草稿并继续"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancelButton);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == saveButton)
    {
        return saveSelectedCharacterPersona(false);
    }

    return clicked == discardButton;
}

void SettingsDialog::refreshAgentHookStatuses()
{
    const QString expectedBridge = AgentHookManager::defaultBridgeExecutablePath();
    const AgentHookStatus codexStatus = AgentHookManager::inspect(AgentHookTarget::Codex, expectedBridge);
    m_codexHookNameLabel->setText(QStringLiteral("Codex (%1)").arg(codexStatus.configPath));
    m_codexHookNameLabel->setToolTip(codexStatus.configPath);
    applyAgentHookStatusToRow(
        codexStatus,
        m_codexHookStatusLabel,
        m_codexHookInstallButton,
        m_codexHookUninstallButton);
    applyAgentHookStatusToRow(
        AgentHookManager::inspect(AgentHookTarget::Antigravity, expectedBridge),
        m_antigravityHookStatusLabel,
        m_antigravityHookInstallButton,
        m_antigravityHookUninstallButton);
}

void SettingsDialog::handleInstallOrFixAgentHook(AgentHookTarget target)
{
    const QString bridgeExe = AgentHookManager::defaultBridgeExecutablePath();
    if (!QFileInfo::exists(bridgeExe))
    {
        const QString msg = QStringLiteral("当前目录未找到 QtDesktopPetBridge.exe（%1）。请先同时构建 QtDesktopPetBridge 目标或使用完整发布包。")
            .arg(QDir::toNativeSeparators(bridgeExe));
        setAgentHookActionStatus(msg, QStringLiteral("error"));
        QMessageBox::warning(this, QStringLiteral("缺少桥接程序"), msg);
        return;
    }

    const AgentHookStatus current = AgentHookManager::inspect(target, bridgeExe);
    if (current.state == AgentHookInstallState::ConfigInvalid)
    {
        const QString msg = QStringLiteral("%1 配置文件当前不是合法 JSON，为保护你的既有配置已拒绝覆盖：\n%2")
            .arg(AgentHookManager::targetDisplayName(target), current.configPath);
        setAgentHookActionStatus(QStringLiteral("%1 配置 JSON 损坏，已中止修改。").arg(AgentHookManager::targetDisplayName(target)), QStringLiteral("error"));
        QMessageBox::warning(this, QStringLiteral("配置文件格式异常"), msg);
        return;
    }

    const QString safetyNote = (target == AgentHookTarget::Codex)
        ? QStringLiteral("仅在 hooks 中合并 QtDesktopPetBridge 条目，绝不修改 config.toml。")
        : QStringLiteral("仅写入独立键 qt-desktop-pet-status，绝不挂载 PreToolUse。");
    const QString confirmText = QStringLiteral(
        "确定要写入 %1 Hook 配置吗？\n\n"
        "目标文件：%2\n"
        "桥接程序：%3\n"
        "安全保护：若目标文件已存在，会先创建 .bak 备份并保留其他 Hook；%4")
        .arg(AgentHookManager::targetDisplayName(target), current.configPath, QDir::toNativeSeparators(bridgeExe), safetyNote);

    if (QMessageBox::question(this, QStringLiteral("确认接入外部 Agent"), confirmText) != QMessageBox::Yes)
    {
        return;
    }

    const AgentHookOperationResult op = AgentHookManager::installOrUpdate(target, bridgeExe);
    refreshAgentHookStatuses();
    if (!op.success)
    {
        setAgentHookActionStatus(op.message, QStringLiteral("error"));
        QMessageBox::warning(this, QStringLiteral("接入失败"), op.message);
        return;
    }

    QString okMsg = op.message;
    if (!op.backupPath.isEmpty())
    {
        okMsg += QStringLiteral("（已备份旧文件：%1）").arg(QFileInfo(op.backupPath).fileName());
    }
    setAgentHookActionStatus(okMsg, QStringLiteral("success"));
}

void SettingsDialog::handleUninstallAgentHook(AgentHookTarget target)
{
    const QString bridgeExe = AgentHookManager::defaultBridgeExecutablePath();
    const AgentHookStatus current = AgentHookManager::inspect(target, bridgeExe);
    if (current.state == AgentHookInstallState::ConfigInvalid)
    {
        const QString msg = QStringLiteral("%1 配置文件当前不是合法 JSON，无法自动修改：\n%2")
            .arg(AgentHookManager::targetDisplayName(target), current.configPath);
        setAgentHookActionStatus(QStringLiteral("%1 配置 JSON 损坏，已中止卸载。").arg(AgentHookManager::targetDisplayName(target)), QStringLiteral("error"));
        QMessageBox::warning(this, QStringLiteral("配置文件格式异常"), msg);
        return;
    }

    const QString confirmText = QStringLiteral(
        "确定要解除 %1 的外部状态接入吗？\n\n"
        "目标文件：%2\n"
        "将先创建 .bak 备份并仅移除 QtDesktopPetBridge 相关条目，其余用户 Hook 保持不变。")
        .arg(AgentHookManager::targetDisplayName(target), current.configPath);
    if (QMessageBox::question(this, QStringLiteral("确认解除接入"), confirmText) != QMessageBox::Yes)
    {
        return;
    }

    const AgentHookOperationResult op = AgentHookManager::uninstall(target);
    refreshAgentHookStatuses();
    if (!op.success)
    {
        setAgentHookActionStatus(op.message, QStringLiteral("error"));
        QMessageBox::warning(this, QStringLiteral("解除接入失败"), op.message);
        return;
    }

    QString okMsg = op.message;
    if (!op.backupPath.isEmpty())
    {
        okMsg += QStringLiteral("（已备份：%1）").arg(QFileInfo(op.backupPath).fileName());
    }
    setAgentHookActionStatus(okMsg, QStringLiteral("success"));
}

void SettingsDialog::openAgentTemplatesDirectory()
{
    const QString templatesDir = locateAgentTemplatesDirectory();
    if (!templatesDir.isEmpty())
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(templatesDir));
        setAgentHookActionStatus(
            QStringLiteral("已打开模板目录（DSH 请参考其中的 dsh-cordis-plugin.js 手动配置）：%1")
                .arg(QDir::toNativeSeparators(templatesDir)),
            QStringLiteral("info"));
        return;
    }

    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    QMessageBox::information(
        this,
        QStringLiteral("DSH 手动接入说明"),
        QStringLiteral(
            "当前运行目录未随附仓库 examples/agent_bridge 示例目录。\n\n"
            "你可以在 DeepSeek Harness (DSH) 的 Cordis 插件或自定义脚本中直接调用当前目录的桥接程序：\n"
            "%1\\QtDesktopPetBridge.exe --source dsh --state working --summary \"DSH running\"")
            .arg(appDir));
}

void SettingsDialog::testAgentBridgeConnection()
{
    if (m_agentBridgeTestProcess != nullptr && m_agentBridgeTestProcess->state() != QProcess::NotRunning)
    {
        return;
    }

    const QString bridgeExe = AgentHookManager::defaultBridgeExecutablePath();
    if (!QFileInfo::exists(bridgeExe))
    {
        setAgentHookActionStatus(
            QStringLiteral("未找到 QtDesktopPetBridge.exe，无法执行联动测试：%1").arg(QDir::toNativeSeparators(bridgeExe)),
            QStringLiteral("error"));
        return;
    }

    m_testAgentBridgeButton->setEnabled(false);
    setAgentHookActionStatus(QStringLiteral("正在向桌宠通道发送测试状态..."), QStringLiteral("info"));

    auto *process = new QProcess(this);
    m_agentBridgeTestProcess = process;
    QPointer<QProcess> procGuard(process);

    connect(process, &QProcess::finished, this, [this, procGuard](int exitCode, QProcess::ExitStatus exitStatus) {
        if (!procGuard || procGuard.data() != m_agentBridgeTestProcess)
        {
            return;
        }
        m_agentBridgeTestProcess = nullptr;
        procGuard->deleteLater();
        m_testAgentBridgeButton->setEnabled(true);

        if (exitStatus == QProcess::NormalExit && exitCode == 0)
        {
            QString ok = QStringLiteral("联动测试成功！若主窗口处于运行且未关闭联动总开关，桌宠脚底应已显示状态胶囊。");
            if (!m_enableAgentBridgeCheckBox->isChecked())
            {
                ok += QStringLiteral("（提示：当前界面未勾选“启用外部编程 Agent 本地状态联动”）");
            }
            setAgentHookActionStatus(ok, QStringLiteral("success"));
        }
        else
        {
            setAgentHookActionStatus(
                QStringLiteral("测试未能连上桌宠 IPC（退出码 %1）。请先开启外部 Agent 状态联动并保存设置，再重试。")
                    .arg(exitCode),
                QStringLiteral("error"));
        }
    });

    connect(process, &QProcess::errorOccurred, this, [this, procGuard](QProcess::ProcessError) {
        if (!procGuard || procGuard.data() != m_agentBridgeTestProcess)
        {
            return;
        }
        const QString errText = procGuard->errorString();
        m_agentBridgeTestProcess = nullptr;
        procGuard->deleteLater();
        m_testAgentBridgeButton->setEnabled(true);
        setAgentHookActionStatus(QStringLiteral("启动 QtDesktopPetBridge.exe 失败：%1").arg(errText), QStringLiteral("error"));
    });

    QTimer::singleShot(3000, this, [this, procGuard]() {
        if (procGuard && procGuard.data() == m_agentBridgeTestProcess && procGuard->state() != QProcess::NotRunning)
        {
            procGuard->kill();
        }
    });

    const QStringList args = {
        QStringLiteral("--source"), QStringLiteral("custom"),
        QStringLiteral("--session"), QStringLiteral("ui-selftest"),
        QStringLiteral("--workspace"), QStringLiteral("SettingsTest"),
        QStringLiteral("--state"), QStringLiteral("needs_input"),
        QStringLiteral("--summary"), QStringLiteral("设置页联动测试"),
        QStringLiteral("--timeout-ms"), QStringLiteral("1000"),
        QStringLiteral("--strict"),
    };
    process->start(bridgeExe, args);
}

void SettingsDialog::setAgentHookActionStatus(const QString &message, const QString &state)
{
    m_agentHookActionStatusLabel->setText(message);
    m_agentHookActionStatusLabel->setToolTip(message);
    m_agentHookActionStatusLabel->setProperty("status", state);
    m_agentHookActionStatusLabel->style()->unpolish(m_agentHookActionStatusLabel);
    m_agentHookActionStatusLabel->style()->polish(m_agentHookActionStatusLabel);
}
