#include "FrameAnimator.h"

#include <QtGlobal>

#include <utility>

FrameAnimator::FrameAnimator()
{
    QObject::connect(&m_timer, &QTimer::timeout, [this]() {
        advanceFrame();
    });
}

void FrameAnimator::setFrameChangedCallback(FrameChangedCallback callback)
{
    m_frameChangedCallback = std::move(callback);
}

void FrameAnimator::setClipFinishedCallback(ClipFinishedCallback callback)
{
    m_clipFinishedCallback = std::move(callback);
}

void FrameAnimator::play(const AnimationClip *clip)
{
    if (clip == nullptr || !clip->isValid())
    {
        stop();
        return;
    }

    m_timer.stop();
    m_clip = clip;
    m_frameIndex = 0;
    m_paused = false;

    if (m_frameChangedCallback)
    {
        m_frameChangedCallback(m_clip->frameAt(m_frameIndex));
    }

    const int intervalMs = qMax(1, 1000 / m_clip->fps);
    m_timer.start(intervalMs);
}

void FrameAnimator::pause()
{
    if (m_clip == nullptr)
    {
        return;
    }

    m_timer.stop();
    m_paused = true;
}

void FrameAnimator::resume()
{
    if (m_clip == nullptr || !m_clip->isValid() || !m_paused)
    {
        return;
    }

    const int intervalMs = qMax(1, 1000 / m_clip->fps);
    m_timer.start(intervalMs);
    m_paused = false;
}

void FrameAnimator::stop()
{
    m_timer.stop();
    m_clip = nullptr;
    m_frameIndex = 0;
    m_paused = false;
}

bool FrameAnimator::isPlaying() const
{
    return m_timer.isActive() && m_clip != nullptr;
}

bool FrameAnimator::isPaused() const
{
    return m_paused && m_clip != nullptr;
}

QString FrameAnimator::currentStateName() const
{
    if (m_clip == nullptr)
    {
        return {};
    }

    return m_clip->stateName;
}

void FrameAnimator::advanceFrame()
{
    if (m_clip == nullptr || !m_clip->isValid())
    {
        stop();
        return;
    }

    if (m_frameIndex + 1 >= m_clip->frameCount())
    {
        if (m_clip->loop)
        {
            m_frameIndex = 0;
        }
        else
        {
            const QString nextState = m_clip->nextState;
            stop();
            if (m_clipFinishedCallback)
            {
                m_clipFinishedCallback(nextState);
            }
            return;
        }
    }
    else
    {
        ++m_frameIndex;
    }

    if (m_frameChangedCallback)
    {
        m_frameChangedCallback(m_clip->frameAt(m_frameIndex));
    }
}
