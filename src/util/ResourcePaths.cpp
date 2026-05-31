#include "ResourcePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace
{
QString cleanRelativePath(QString relativePath)
{
    relativePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (relativePath.startsWith(QLatin1Char('/')))
    {
        relativePath.remove(0, 1);
    }
    return QDir::cleanPath(relativePath);
}

QString appResourcesRootPath()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("resources")));
}

QString sourceResourcesRootPath()
{
    return QDir::cleanPath(QStringLiteral(PET_SOURCE_DIR) + QStringLiteral("/resources"));
}
}

QString ResourcePaths::resourcesRootPath()
{
    const QString appResources = appResourcesRootPath();
    if (QFileInfo::exists(appResources))
    {
        return appResources;
    }

    return sourceResourcesRootPath();
}

QString ResourcePaths::resourcePath(const QString &relativePath)
{
    const QString cleanedRelativePath = cleanRelativePath(relativePath);
    const QString appPath = QDir(appResourcesRootPath()).filePath(cleanedRelativePath);
    if (QFileInfo::exists(appPath))
    {
        return QDir::cleanPath(appPath);
    }

    return QDir::cleanPath(QDir(sourceResourcesRootPath()).filePath(cleanedRelativePath));
}

QString ResourcePaths::charactersRootPath()
{
    return resourcePath(QStringLiteral("characters"));
}

QString ResourcePaths::appIconPath()
{
    return resourcePath(QStringLiteral("icons/app_icon.ico"));
}

QString ResourcePaths::appIconSourcePngPath()
{
    return resourcePath(QStringLiteral("icons/app_icon_1024.png"));
}
