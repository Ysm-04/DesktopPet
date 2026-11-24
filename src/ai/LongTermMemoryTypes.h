#pragma once

#include <QDateTime>
#include <QString>

enum class LongTermMemoryCategory
{
    Profile,
    Preference,
    Project,
    Fact,
};

enum class LongTermMemorySource
{
    Manual,
    Explicit,
    Auto,
};

struct LongTermMemoryItem
{
    QString id;
    QString content;
    LongTermMemoryCategory category = LongTermMemoryCategory::Fact;
    LongTermMemorySource source = LongTermMemorySource::Manual;
    QDateTime createdAt;
    QDateTime updatedAt;
};

inline QString longTermMemoryCategoryToString(LongTermMemoryCategory category)
{
    switch (category)
    {
    case LongTermMemoryCategory::Profile:
        return QStringLiteral("profile");
    case LongTermMemoryCategory::Preference:
        return QStringLiteral("preference");
    case LongTermMemoryCategory::Project:
        return QStringLiteral("project");
    case LongTermMemoryCategory::Fact:
        return QStringLiteral("fact");
    }

    return QStringLiteral("fact");
}

inline LongTermMemoryCategory longTermMemoryCategoryFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("profile"))
    {
        return LongTermMemoryCategory::Profile;
    }
    if (normalized == QStringLiteral("preference"))
    {
        return LongTermMemoryCategory::Preference;
    }
    if (normalized == QStringLiteral("project"))
    {
        return LongTermMemoryCategory::Project;
    }
    return LongTermMemoryCategory::Fact;
}

inline QString longTermMemoryCategoryDisplayName(LongTermMemoryCategory category)
{
    switch (category)
    {
    case LongTermMemoryCategory::Profile:
        return QStringLiteral("个人画像");
    case LongTermMemoryCategory::Preference:
        return QStringLiteral("偏好习惯");
    case LongTermMemoryCategory::Project:
        return QStringLiteral("项目背景");
    case LongTermMemoryCategory::Fact:
        return QStringLiteral("重要事实");
    }

    return QStringLiteral("重要事实");
}

inline QString longTermMemorySourceToString(LongTermMemorySource source)
{
    switch (source)
    {
    case LongTermMemorySource::Manual:
        return QStringLiteral("manual");
    case LongTermMemorySource::Explicit:
        return QStringLiteral("explicit");
    case LongTermMemorySource::Auto:
        return QStringLiteral("auto");
    }

    return QStringLiteral("manual");
}

inline LongTermMemorySource longTermMemorySourceFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("explicit"))
    {
        return LongTermMemorySource::Explicit;
    }
    if (normalized == QStringLiteral("auto"))
    {
        return LongTermMemorySource::Auto;
    }
    return LongTermMemorySource::Manual;
}

inline QString longTermMemorySourceDisplayName(LongTermMemorySource source)
{
    switch (source)
    {
    case LongTermMemorySource::Manual:
        return QStringLiteral("手动");
    case LongTermMemorySource::Explicit:
        return QStringLiteral("指令");
    case LongTermMemorySource::Auto:
        return QStringLiteral("自动");
    }

    return QStringLiteral("手动");
}
