#include "AlphaVideoBackend.h"
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QPointer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtEndian>
#include <utility>

bool AlphaVideoBackend::inspectFile(const QString &path, const QString &layout,
                                   AlphaVideoInfo *info, QString *error)
{
    return AlphaVideoSpec::inspectFile(path, layout, info, error);
}

QImage AlphaVideoBackend::compose(const QImage &packed, const QString &layout)
{
    if (packed.isNull() || (layout != "left-right" && layout != "top-bottom")) return {};
    const bool horizontal = layout == "left-right";
    if ((horizontal ? packed.width() : packed.height()) % 2) return {};
    const QSize size(horizontal ? packed.width() / 2 : packed.width(),
                     horizontal ? packed.height() : packed.height() / 2);
    const QImage input = packed.convertToFormat(QImage::Format_RGB32);
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y)
    {
        const auto *rgb = reinterpret_cast<const QRgb *>(input.constScanLine(y));
        const auto *mask = reinterpret_cast<const QRgb *>(input.constScanLine(horizontal ? y : y + size.height()));
        auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
        {
            const QRgb color = rgb[x];
            const int alpha = qGray(mask[horizontal ? x + size.width() : x]);
            output[x] = qPremultiply(qRgba(qRed(color), qGreen(color), qBlue(color), alpha));
        }
    }
    return result;
}

AlphaVideoBackend::AlphaVideoBackend(QString path, QString layout, QSize targetSize)
    : m_path(std::move(path)), m_layout(std::move(layout)), m_targetSize(targetSize)
{
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(10000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] { fail(QStringLiteral("视频加载或出帧超过 10 秒。")); });
}
AlphaVideoBackend::~AlphaVideoBackend() { stopPlayback(); }
std::unique_ptr<IPetMediaBackend> AlphaVideoBackend::clone() const
{
    auto copy = std::make_unique<AlphaVideoBackend>(m_path, m_layout, m_targetSize);
    copy->m_loaded = m_loaded;
    copy->m_info = m_info;
    copy->m_frame = m_frame;
    return copy;
}
bool AlphaVideoBackend::hasSource() const { return !m_path.isEmpty(); }
bool AlphaVideoBackend::ensureLoaded()
{
    if (m_loaded) return true;
    m_loaded = inspectFile(m_path, m_layout, &m_info, &m_error);
    return m_loaded;
}
void AlphaVideoBackend::unload() { stopPlayback(); m_loaded = false; m_frame = {}; }
qint64 AlphaVideoBackend::estimatedMemoryBytes() const
{
    // QMediaPlayer's decoder/GPU allocations are not included in this estimate.
    return qint64(m_frame.width()) * m_frame.height() * 4;
}
void AlphaVideoBackend::stopPlayback()
{
    ++m_epoch;
    m_running = m_paused = false;
    m_callbacks = {};
    m_watchdog.stop();
    m_player.reset();
    m_sink.reset();
}
std::function<void()> AlphaVideoBackend::playbackStopper()
{
    const QPointer<AlphaVideoBackend> guard(this);
    return [guard] { if (guard) guard->stopPlayback(); };
}
void AlphaVideoBackend::fail(const QString &message)
{
    if (!m_running) return;
    m_error = message;
    m_running = false;
    m_watchdog.stop();
    if (m_player) m_player->stop();
    const auto callback = m_callbacks.error;
    if (callback) callback(message);
}
bool AlphaVideoBackend::startPlayback(bool loop, PlaybackCallbacks callbacks)
{
    stopPlayback();
    AlphaVideoSpec::configureRuntime();
    // Recheck files modified after import; never decode an unbounded new canvas.
    m_loaded = false;
    if (!ensureLoaded()) return false;
    m_callbacks = std::move(callbacks);
    m_frames = 0; m_running = true; m_error.clear();
    const quint64 epoch = m_epoch;
    m_sink = std::make_unique<QVideoSink>();
    m_player = std::make_unique<QMediaPlayer>();
    if (!m_player->isAvailable()) { m_error = QStringLiteral("缺少可用 Qt 视频后端。"); stopPlayback(); return false; }
    m_player->setVideoSink(m_sink.get());
    // No QAudioOutput is constructed. Embedded sound never plays.
    m_player->setLoops(loop ? QMediaPlayer::Infinite : QMediaPlayer::Once);
    connect(m_player.get(), &QMediaPlayer::errorOccurred, this, [this, epoch](auto, const QString &) {
        if (epoch == m_epoch) fail(QStringLiteral("透明视频解码失败，请检查文件和随包视频依赖。"));
    });
    connect(m_player.get(), &QMediaPlayer::durationChanged, this, [this, epoch](qint64 duration) {
        if (epoch == m_epoch && duration > 300000) fail(QStringLiteral("实际视频时长超过 300 秒。"));
    });
    connect(m_player.get(), &QMediaPlayer::mediaStatusChanged, this, [this, epoch](QMediaPlayer::MediaStatus status) {
        if (epoch != m_epoch || !m_running) return;
        if (status == QMediaPlayer::InvalidMedia) { fail(QStringLiteral("视频内容或编码不可用。")); return; }
        if (status != QMediaPlayer::EndOfMedia) return;
        if (!m_frames) { fail(QStringLiteral("视频未解码出有效帧。")); return; }
        m_running = false; m_watchdog.stop();
        const auto callback = m_callbacks.finished;
        if (callback) callback();
    });
    connect(m_sink.get(), &QVideoSink::videoFrameChanged, this, [this, epoch](const QVideoFrame &video) {
        if (epoch != m_epoch || !m_running || m_paused || !video.isValid()) return;
        if (video.size() != m_info.packedSize || video.rotationAngle() != QVideoFrame::Rotation0 || video.mirrored())
        { fail(QStringLiteral("实际视频尺寸或方向与校验结果不一致。")); return; }
        QImage image = compose(video.toImage(), m_layout);
        if (image.isNull()) { fail(QStringLiteral("视频帧无法读取或透明合成失败。")); return; }
        if (m_targetSize.isValid()) image = image.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_frame = QPixmap::fromImage(image);
        ++m_frames; m_watchdog.start();
        const auto callback = m_callbacks.frame;
        if (callback) callback(m_frame);
    });
    m_watchdog.start();
    m_player->setSource(QUrl::fromLocalFile(m_path));
    m_player->play();
    return true;
}
void AlphaVideoBackend::pausePlayback()
{
    if (!m_running || m_paused) return;
    m_paused = true; m_watchdog.stop();
    m_player->pause();
}
void AlphaVideoBackend::resumePlayback()
{
    if (!m_running || !m_paused) return;
    m_paused = false; m_watchdog.start();
    m_player->play();
}
