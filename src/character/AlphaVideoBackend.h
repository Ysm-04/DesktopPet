#pragma once

#include "IPetMediaBackend.h"
#include "AlphaVideoSpec.h"
#include <QObject>
#include <QImage>
#include <QSize>
#include <QTimer>
#include <memory>

class QMediaPlayer;
class QVideoSink;

// A local, self-contained H.264 MP4 containing RGB + straight-alpha grayscale.
// Video owns its clock; no blocking event loop is used in normal playback.
class AlphaVideoBackend : public QObject, public IPetMediaBackend
{
public:
    AlphaVideoBackend(QString path, QString layout, QSize targetSize);
    ~AlphaVideoBackend() override;
    static bool inspectFile(const QString &path, const QString &layout,
                            AlphaVideoInfo *info, QString *error);
    static QImage compose(const QImage &packed, const QString &layout);
    std::unique_ptr<IPetMediaBackend> clone() const override;
    bool hasSource() const override;
    bool ensureLoaded() override;
    void unload() override;
    int frameCount() const override { return m_loaded ? 1 : 0; }
    int loadedFrameCount() const override { return m_loaded ? 1 : 0; }
    bool prepareFrame(int) override { return false; }
    QString errorString() const override { return m_error; }
    const QPixmap &frameAt(int) const override { return m_frame; }
    int frameDurationMs(int, int) const override { return 1; }
    qint64 estimatedMemoryBytes() const override;
    bool ownsPlaybackClock() const override { return true; }
    bool startPlayback(bool loop, PlaybackCallbacks callbacks) override;
    void pausePlayback() override;
    void resumePlayback() override;
    void stopPlayback() override;
    std::function<void()> playbackStopper() override;
    const AlphaVideoInfo &info() const { return m_info; }

private:
    void fail(const QString &message);
    QString m_path;
    QString m_layout;
    QSize m_targetSize;
    AlphaVideoInfo m_info;
    QString m_error;
    QPixmap m_frame;
    std::unique_ptr<QVideoSink> m_sink;
    std::unique_ptr<QMediaPlayer> m_player;
    QTimer m_watchdog;
    PlaybackCallbacks m_callbacks;
    quint64 m_epoch = 0;
    int m_frames = 0;
    bool m_loaded = false;
    bool m_running = false;
    bool m_paused = false;
};
