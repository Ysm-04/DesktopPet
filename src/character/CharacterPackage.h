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

struct CharacterBase
{
    int width = 320;
    int height = 320;
    double scale = 1.0;
    double anchorX = 0.5;
    double anchorY = 0.0;
};

struct CharacterBubble
{
    double offsetX = 0.0;
    double offsetY = -8.0;
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
    CharacterBase base;
    CharacterBubble bubble;
    QMap<QString, CharacterState> states;

    bool hasState(const QString &stateName) const;
    const CharacterState *state(const QString &stateName) const;
};
