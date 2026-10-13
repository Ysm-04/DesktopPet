#pragma once

#include <QString>

class ResourcePaths
{
public:
    static QString resourcePath(const QString &relativePath);
    static QString charactersRootPath();
    static QString reminderSoundsRootPath();
    static QString appIconPath();
};
