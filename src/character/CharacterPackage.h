#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

inline constexpr int CharacterPersonaMaxChars = 1200;
inline constexpr int CharacterDialogueMaxLineChars = 160;
inline constexpr int CharacterDialogueMaxLinesPerGroup = 32;

struct CharacterDialogues
{
    QStringList tap;
    QStringList headpat;
    QStringList dragDrop;
    QStringList greetMorning;
    QStringList greetAfternoon;
    QStringList greetEvening;
    QStringList greetLateNight;
    QStringList sedentary;
    QStringList lateNightCare;
    QStringList agentWorking;
    QStringList agentDone;
    QStringList agentNeedsInput;
    QStringList agentError;

    bool isEmpty() const;
    QStringList linesForGroup(const QString &groupName) const;
};

struct CharacterState
{
    QString name;
    QString mediaType = QStringLiteral("frames");
    QString relativePath;
    QStringList framePaths;
    QString mediaPath;
    QString videoLayout;
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

struct CharacterEffectSpec
{
    QStringList breathingStates;
    double amplitude = 0.008;
    int periodMs = 3600;
    double maxAngleDeg = 0.0;
    QString blinkState;
    QString closedFrame;
    int intervalMinMs = 4000;
    int intervalMaxMs = 8000;
    int holdMs = 120;
    bool statusBadge = false;

    bool enabled() const
    {
        return !breathingStates.isEmpty() || maxAngleDeg > 0 || !closedFrame.isEmpty() || statusBadge;
    }
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
    QString persona;
    CharacterBase base;
    CharacterBubble bubble;
    CharacterDialogues dialogues;
    CharacterEffectSpec effects;
    QMap<QString, CharacterState> states;

    bool hasState(const QString &stateName) const;
    const CharacterState *state(const QString &stateName) const;
};
