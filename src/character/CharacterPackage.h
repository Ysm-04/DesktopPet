#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

struct CharacterState
{
    QString name;
    QString relativePath;
    QStringList framePaths;
    int fps = 0;
    bool loop = true;
    QString nextState;
};

class CharacterPackage
{
public:
    int schemaVersion = 0;
    QString id;
    QString displayName;
    QString author;
    QString version;
    QString packagePath;
    QString previewPath;
    QString defaultState;
    QMap<QString, CharacterState> states;

    bool hasState(const QString &stateName) const;
    const CharacterState *state(const QString &stateName) const;
};

