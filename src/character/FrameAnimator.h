#pragma once

#include "AnimationClip.h"

#include <QTimer>

#include <functional>

class FrameAnimator
{
public:
    using FrameChangedCallback = std::function<void(const QPixmap &)>;
    using ClipFinishedCallback = std::function<void(const QString &)>;

    FrameAnimator();

    void setFrameChangedCallback(FrameChangedCallback callback);
    void setClipFinishedCallback(ClipFinishedCallback callback);

    void play(const AnimationClip *clip);
    void pause();
    void resume();
    void stop();
    bool isPlaying() const;
    bool isPaused() const;
    QString currentStateName() const;

private:
    void advanceFrame();

    QTimer m_timer;
    const AnimationClip *m_clip = nullptr;
    int m_frameIndex = 0;
    bool m_paused = false;
    FrameChangedCallback m_frameChangedCallback;
    ClipFinishedCallback m_clipFinishedCallback;
};
