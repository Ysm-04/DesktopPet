#pragma once

#include "CharacterPackage.h"
#include "IPetMediaBackend.h"

#include <QPixmap>
#include <QSize>
#include <QString>
#include <QtGlobal>

class AnimationClip
{
public:
    AnimationClip() = default;
    AnimationClip(const AnimationClip &other);
    AnimationClip &operator=(const AnimationClip &other);
    AnimationClip(AnimationClip &&) noexcept = default;
    AnimationClip &operator=(AnimationClip &&) noexcept = default;
    static AnimationClip fromState(const CharacterState &state, const QSize &targetSize, bool loadFrames = true);

    bool isValid() const;
    bool isLoaded() const;
    bool ensureLoaded();
    void unloadFrames();
    qint64 estimatedMemoryBytes() const;
    int loadedFrameCount() const;
    const QPixmap &frameAt(int index) const;
    int frameCount() const;
    int frameDurationMs(int index) const;
    bool prepareFrame(int index);
    QString errorString() const;
    bool ownsPlaybackClock() const;
    bool startPlayback(IPetMediaBackend::PlaybackCallbacks callbacks);
    void pausePlayback();
    void resumePlayback();
    void stopPlayback();
    std::function<void()> playbackStopper();

    QString stateName;
    int fps = 0;
    bool loop = true;
    QString nextState;

private:
    std::unique_ptr<IPetMediaBackend> m_backend;
};
