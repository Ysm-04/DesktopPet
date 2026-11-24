#pragma once

#include "CharacterPackage.h"

#include <QMap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QtGlobal>

inline constexpr double PetHeadZoneRatio = 0.36;
inline constexpr qint64 PetTouchCooldownMs = 1800;
inline constexpr qint64 PetDragDropCooldownMs = 6000;
inline constexpr int PetDragFeedbackMinDistancePx = 24;

enum class CharacterDialogueScene
{
    Tap,
    Headpat,
    DragDrop,
    GreetMorning,
    GreetAfternoon,
    GreetEvening,
    GreetLateNight,
    Sedentary,
    LateNightCare,
    AgentWorking,
    AgentDone,
    AgentNeedsInput,
    AgentError
};

enum class PetTouchZone
{
    None,
    Head,
    Body
};

class CharacterDialogueProvider
{
public:
    static QString sceneKey(CharacterDialogueScene scene);
    static CharacterDialogueScene greetingSceneForHour(int hour);
    static bool isLateNightHour(int hour);
    static PetTouchZone classifyTouchZone(
        const QRect &visibleRect,
        const QPoint &point,
        double headRatio = PetHeadZoneRatio);
    static bool isCooldownElapsed(qint64 lastTriggerMs, qint64 nowMs, qint64 cooldownMs);
    static QStringList builtInFallbackLines(CharacterDialogueScene scene);
    static QStringList effectiveLines(const CharacterDialogues &packageDialogues, CharacterDialogueScene scene);

    QString pickLine(
        const CharacterDialogues &packageDialogues,
        CharacterDialogueScene scene,
        int randomSeedOverride = -1);
    void resetHistory();

private:
    QMap<QString, QString> m_lastLineByScene;
};
