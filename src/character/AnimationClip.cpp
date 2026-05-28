#include "AnimationClip.h"

#include <QSize>

AnimationClip AnimationClip::fromState(const CharacterState &state, const QSize &targetSize)
{
    AnimationClip clip;
    clip.stateName = state.name;
    clip.fps = state.fps;
    clip.loop = state.loop;
    clip.nextState = state.nextState;

    for (const QString &framePath : state.framePaths)
    {
        QPixmap pixmap(framePath);
        if (pixmap.isNull())
        {
            continue;
        }

        if (targetSize.isValid())
        {
            pixmap = pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        clip.m_frames.append(pixmap);
    }

    return clip;
}

bool AnimationClip::isValid() const
{
    return fps > 0 && !m_frames.isEmpty();
}

const QPixmap &AnimationClip::frameAt(int index) const
{
    return m_frames.at(index);
}

int AnimationClip::frameCount() const
{
    return m_frames.size();
}
