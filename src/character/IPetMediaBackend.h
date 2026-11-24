#pragma once

#include <QPixmap>
#include <QString>
#include <QtGlobal>
#include <memory>
#include <functional>

// Owns decoded media. PNG/WebP use FrameAnimator's clock; asynchronous video
// owns its clock. FrameAnimator coordinates the state/loop and lifecycle policy.
// Clones keep AnimationClip's mutable cache independent after a value copy.
class IPetMediaBackend
{
public:
    struct PlaybackCallbacks
    {
        std::function<void(const QPixmap &)> frame;
        std::function<void()> finished;
        std::function<void(const QString &)> error;
    };
    virtual ~IPetMediaBackend() = default;
    virtual std::unique_ptr<IPetMediaBackend> clone() const = 0;
    virtual bool hasSource() const = 0;
    virtual bool ensureLoaded() = 0;
    virtual void unload() = 0;
    virtual int frameCount() const = 0;
    virtual int loadedFrameCount() const { return frameCount(); }
    virtual bool prepareFrame(int index) = 0;
    virtual QString errorString() const { return {}; }
    virtual const QPixmap &frameAt(int index) const = 0;
    virtual int frameDurationMs(int index, int effectiveFps) const = 0;
    virtual qint64 estimatedMemoryBytes() const = 0;
    virtual bool ownsPlaybackClock() const { return false; }
    virtual bool startPlayback(bool, PlaybackCallbacks) { return false; }
    virtual void pausePlayback() {}
    virtual void resumePlayback() {}
    virtual void stopPlayback() {}
    // A weak lifetime guard lets the animator stop async playback even when a
    // caller has already destroyed/replaced the clip value.
    virtual std::function<void()> playbackStopper() { return {}; }
};
