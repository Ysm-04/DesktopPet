#include "FrameAnimator.h"
#include <QElapsedTimer>
#include <utility>

FrameAnimator::FrameAnimator()
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, [this]() { advanceFrame(); });
}
FrameAnimator::~FrameAnimator() { stop(); }

void FrameAnimator::setFrameChangedCallback(FrameChangedCallback callback)
{
    m_frameChangedCallback = std::move(callback);
}

void FrameAnimator::setClipFinishedCallback(ClipFinishedCallback callback)
{
    m_clipFinishedCallback = std::move(callback);
}

void FrameAnimator::setPlaybackErrorCallback(PlaybackErrorCallback callback)
{
    m_playbackErrorCallback = std::move(callback);
}

void FrameAnimator::play(AnimationClip *clip)
{
    stop();
    // Lazy loading belongs to AnimationClip's caller, on the GUI thread.
    if (!clip || !clip->isValid() || !clip->isLoaded()) return;
    m_clip = clip;
    if (clip->ownsPlaybackClock())
    {
        m_stopPlayback = clip->playbackStopper();
        const quint64 generation = m_generation;
        IPetMediaBackend::PlaybackCallbacks callbacks;
        // Defer consumers out of decoder callbacks: consumers can unload clips,
        // replace character packages or select a fallback and destroy a player.
        callbacks.frame = [this, generation](const QPixmap &frame) {
            QTimer::singleShot(0, &m_timer, [this, generation, frame] {
                if (m_generation != generation || !m_clip || m_paused) return;
                const auto callback = m_frameChangedCallback;
                if (callback) callback(frame);
            });
        };
        callbacks.finished = [this, generation] {
            QTimer::singleShot(0, &m_timer, [this, generation] {
                finishAsync(generation, false);
            });
        };
        callbacks.error = [this, generation](const QString &error) {
            QTimer::singleShot(0, &m_timer, [this, generation, error] {
                finishAsync(generation, true, error);
            });
        };
        if (!clip->startPlayback(std::move(callbacks)))
        {
            const QString state = clip->stateName, error = clip->errorString();
            const auto callback = m_playbackErrorCallback;
            stop();
            if (callback) callback(state, error);
        }
        return;
    }
    publishFrame();
}

void FrameAnimator::pause()
{
    if (!m_clip || m_paused) return;
    if (m_clip->ownsPlaybackClock())
    {
        m_paused = true;
        m_clip->pausePlayback();
        return;
    }
    if (m_timer.isActive()) m_remainingMs = qMax(1, m_timer.remainingTime());
    m_timer.stop();
    m_paused = true;
    ++m_generation;
}

void FrameAnimator::resume()
{
    if (!m_clip || !m_clip->isValid() || !m_clip->isLoaded() || !m_paused) return;
    m_paused = false;
    if (m_clip->ownsPlaybackClock())
    {
        if (m_asyncTerminalPending)
        {
            const quint64 generation = m_generation;
            const bool failed = m_asyncFailed;
            const QString error = m_asyncError;
            QTimer::singleShot(0, &m_timer, [this, generation, failed, error] { finishAsync(generation, failed, error); });
        }
        else m_clip->resumePlayback();
        return;
    }
    ++m_generation;
    m_timer.start(m_remainingMs);
}

void FrameAnimator::stop()
{
    m_timer.stop();
    const auto stopPlayback = std::move(m_stopPlayback);
    m_stopPlayback = {};
    m_clip = nullptr;
    m_frameIndex = 0;
    m_paused = false;
    m_remainingMs = 1;
    m_asyncTerminalPending = false;
    m_asyncFailed = false;
    m_asyncError.clear();
    ++m_generation;
    if (stopPlayback) stopPlayback();
}

bool FrameAnimator::isPlaying() const
{
    return m_clip && !m_paused && (m_clip->ownsPlaybackClock() || m_timer.isActive());
}
QString FrameAnimator::currentStateName() const { return m_clip ? m_clip->stateName : QString(); }

void FrameAnimator::finishAsync(quint64 generation, bool failed, const QString &error)
{
    if (m_generation != generation || !m_clip) return;
    if (m_paused)
    {
        m_asyncTerminalPending = true; m_asyncFailed = failed; m_asyncError = error;
        return;
    }
    const QString state = m_clip->stateName, next = m_clip->nextState;
    const auto finished = m_clipFinishedCallback;
    const auto failure = m_playbackErrorCallback;
    stop();
    if (failed) { if (failure) failure(state, error); }
    else if (finished) finished(next);
}

void FrameAnimator::publishFrame()
{
    const quint64 generation = m_generation;
    QElapsedTimer frameWork;
    frameWork.start();
    if (!m_clip->prepareFrame(m_frameIndex))
    {
        const QString state = m_clip->stateName;
        const QString error = m_clip->errorString();
        const auto callback = m_playbackErrorCallback;
        stop();
        if (callback) callback(state, error);
        return;
    }
    m_remainingMs = qMax(1, m_clip->frameDurationMs(m_frameIndex));
    // Callbacks may stop playback, replace clips or unload the old cache.
    const QPixmap frame = m_clip->frameAt(m_frameIndex);
    const auto callback = m_frameChangedCallback;
    if (callback) callback(frame);
    if (m_generation == generation && m_clip && !m_paused)
    {
        // Decode/render work belongs to this frame's time slot, not an extra
        // delay appended on every frame. Slow frames are bounded at 1ms.
        m_remainingMs = qMax(1, m_remainingMs - int(frameWork.elapsed()));
        m_timer.start(m_remainingMs);
    }
}

void FrameAnimator::advanceFrame()
{
    if (!m_clip || !m_clip->isValid() || !m_clip->isLoaded())
    {
        stop();
        return;
    }
    if (m_frameIndex + 1 >= m_clip->frameCount())
    {
        if (m_clip->loop) m_frameIndex = 0;
        else
        {
            const QString next = m_clip->nextState;
            const auto callback = m_clipFinishedCallback;
            stop();
            if (callback) callback(next);
            return;
        }
    }
    else ++m_frameIndex;
    publishFrame();
}
