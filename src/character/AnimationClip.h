#pragma once

#include "CharacterPackage.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class AnimationClip
{
public:
    static AnimationClip fromState(const CharacterState &state, const QSize &targetSize, bool loadFrames = true);

    bool isValid() const;
    bool ensureLoaded();
    const QPixmap &frameAt(int index) const;
    int frameCount() const;

    QString stateName;
    int fps = 0;
    bool loop = true;
    QString nextState;

private:
    void loadFrames();

    QStringList m_framePaths;
    QSize m_targetSize;
    QVector<QPixmap> m_frames;
};
