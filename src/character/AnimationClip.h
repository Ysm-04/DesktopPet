#pragma once

#include "CharacterPackage.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QVector>

class AnimationClip
{
public:
    static AnimationClip fromState(const CharacterState &state, const QSize &targetSize);

    bool isValid() const;
    const QPixmap &frameAt(int index) const;
    int frameCount() const;

    QString stateName;
    int fps = 0;
    bool loop = true;
    QString nextState;

private:
    QVector<QPixmap> m_frames;
};
