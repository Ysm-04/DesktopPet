#include "AnimationClip.h"
#include "PngSequenceBackend.h"
#include "WebpAnimationBackend.h"
#include "AlphaVideoBackend.h"
#include <utility>

AnimationClip::AnimationClip(const AnimationClip &other)
    : stateName(other.stateName), fps(other.fps), loop(other.loop), nextState(other.nextState),
      m_backend(other.m_backend ? other.m_backend->clone() : nullptr)
{
}

AnimationClip &AnimationClip::operator=(const AnimationClip &other)
{
    if (this != &other)
    {
        AnimationClip copy(other);
        *this = std::move(copy);
    }
    return *this;
}

AnimationClip AnimationClip::fromState(const CharacterState &state, const QSize &targetSize, bool loadFrames)
{
    AnimationClip clip;
    clip.stateName = state.name;
    clip.fps = state.fps;
    clip.loop = state.loop;
    clip.nextState = state.nextState;
    if (state.mediaType == QStringLiteral("frames"))
        clip.m_backend = std::make_unique<PngSequenceBackend>(state.framePaths, targetSize);
    else if (state.mediaType == QStringLiteral("webp"))
    {
        clip.fps = 1; // Validity metadata only; WebP uses container delays, never fixed FPS.
        clip.m_backend = std::make_unique<WebpAnimationBackend>(state.mediaPath, targetSize);
    }
    else if (state.mediaType == QStringLiteral("alpha_video"))
    {
        clip.fps = 1; // Video owns its clock, including in low-power mode.
        clip.m_backend = std::make_unique<AlphaVideoBackend>(state.mediaPath, state.videoLayout, targetSize);
    }
    if (loadFrames) clip.ensureLoaded();
    return clip;
}

bool AnimationClip::isValid() const { return fps > 0 && m_backend && m_backend->hasSource(); }
bool AnimationClip::isLoaded() const { return loadedFrameCount() > 0; }
bool AnimationClip::ensureLoaded() { return isValid() && m_backend->ensureLoaded(); }
void AnimationClip::unloadFrames() { if (m_backend) m_backend->unload(); }
qint64 AnimationClip::estimatedMemoryBytes() const { return m_backend ? m_backend->estimatedMemoryBytes() : 0; }
int AnimationClip::loadedFrameCount() const { return m_backend ? m_backend->loadedFrameCount() : 0; }
int AnimationClip::frameCount() const { return m_backend ? m_backend->frameCount() : 0; }
const QPixmap &AnimationClip::frameAt(int index) const { return m_backend->frameAt(index); }
int AnimationClip::frameDurationMs(int index) const
{
    return m_backend ? m_backend->frameDurationMs(index, fps) : 1;
}
bool AnimationClip::prepareFrame(int index) { return m_backend && m_backend->prepareFrame(index); }
QString AnimationClip::errorString() const { return m_backend ? m_backend->errorString() : QString(); }
bool AnimationClip::ownsPlaybackClock() const { return m_backend && m_backend->ownsPlaybackClock(); }
bool AnimationClip::startPlayback(IPetMediaBackend::PlaybackCallbacks callbacks)
{
    return m_backend && m_backend->startPlayback(loop, std::move(callbacks));
}
void AnimationClip::pausePlayback() { if (m_backend) m_backend->pausePlayback(); }
void AnimationClip::resumePlayback() { if (m_backend) m_backend->resumePlayback(); }
void AnimationClip::stopPlayback() { if (m_backend) m_backend->stopPlayback(); }
std::function<void()> AnimationClip::playbackStopper() { return m_backend ? m_backend->playbackStopper() : std::function<void()>{}; }
