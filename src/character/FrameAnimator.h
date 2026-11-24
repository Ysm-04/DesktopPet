#pragma once

#include "AnimationClip.h"

#include <QTimer>

#include <functional>

class FrameAnimator
{
public:
    using FrameChangedCallback = std::function<void(const QPixmap &)>;
    using ClipFinishedCallback = std::function<void(const QString &)>;
    using PlaybackErrorCallback = std::function<void(const QString &, const QString &)>;

    FrameAnimator();
    ~FrameAnimator();

    void setFrameChangedCallback(FrameChangedCallback callback);
    void setClipFinishedCallback(ClipFinishedCallback callback);
    void setPlaybackErrorCallback(PlaybackErrorCallback callback);

    void play(AnimationClip *clip);
    void pause();
    void resume();
    void stop();
    bool isPlaying() const;
    QString currentStateName() const;

private:
    void advanceFrame();
    void publishFrame();
    void finishAsync(quint64 generation, bool failed, const QString &error = {});

    QTimer m_timer;
    AnimationClip *m_clip = nullptr;
    int m_frameIndex = 0;
    bool m_paused = false;
    int m_remainingMs = 1;
    quint64 m_generation = 0;
    bool m_asyncTerminalPending = false;
    bool m_asyncFailed = false;
    QString m_asyncError;
    std::function<void()> m_stopPlayback;
    FrameChangedCallback m_frameChangedCallback;
    ClipFinishedCallback m_clipFinishedCallback;
    PlaybackErrorCallback m_playbackErrorCallback;
};
