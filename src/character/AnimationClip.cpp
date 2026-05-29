#include "AnimationClip.h"

#include <QSize>

AnimationClip AnimationClip::fromState(const CharacterState &state, const QSize &targetSize, bool loadFrames)
{
    AnimationClip clip;
    clip.stateName = state.name;
    clip.fps = state.fps;
    clip.loop = state.loop;
    clip.nextState = state.nextState;
    clip.m_framePaths = state.framePaths;
    clip.m_targetSize = targetSize;

    if (loadFrames)
    {
        clip.loadFrames();
    }

    return clip;
}

bool AnimationClip::isValid() const
{
    return fps > 0 && (!m_frames.isEmpty() || !m_framePaths.isEmpty());
}

bool AnimationClip::ensureLoaded()
{
    if (m_frames.isEmpty())
    {
        loadFrames();
    }

    return !m_frames.isEmpty();
}

void AnimationClip::loadFrames()
{
    if (!m_frames.isEmpty())
    {
        return;
    }

    for (const QString &framePath : m_framePaths)
    {
        QPixmap pixmap(framePath);
        if (pixmap.isNull())
        {
            continue;
        }

        if (m_targetSize.isValid())
        {
            pixmap = pixmap.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_frames.append(pixmap);
    }
}

const QPixmap &AnimationClip::frameAt(int index) const
{
    return m_frames.at(index);
}

int AnimationClip::frameCount() const
{
    return m_frames.size();
}
