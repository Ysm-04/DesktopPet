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

bool AnimationClip::isLoaded() const
{
    return !m_frames.isEmpty();
}

bool AnimationClip::ensureLoaded()
{
    if (m_frames.isEmpty())
    {
        loadFrames();
    }

    return !m_frames.isEmpty();
}

void AnimationClip::unloadFrames()
{
    m_frames.clear();
    m_frames.squeeze();
}

qint64 AnimationClip::estimatedMemoryBytes() const
{
    qint64 totalBytes = 0;
    for (const QPixmap &frame : m_frames)
    {
        totalBytes += static_cast<qint64>(frame.width())
            * static_cast<qint64>(frame.height())
            * static_cast<qint64>(frame.depth())
            / 8;
    }

    return totalBytes;
}

int AnimationClip::loadedFrameCount() const
{
    return m_frames.size();
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
