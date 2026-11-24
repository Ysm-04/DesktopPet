#include "WebpAnimationBackend.h"

#include <QFile>
#include <QFileInfo>
#include <utility>

WebpAnimationBackend::WebpAnimationBackend(QString path, QSize targetSize)
    : m_path(std::move(path)), m_targetSize(targetSize) {}

std::unique_ptr<IPetMediaBackend> WebpAnimationBackend::clone() const
{
    auto copy = std::make_unique<WebpAnimationBackend>(m_path, m_targetSize);
    if (m_reader && copy->ensureLoaded()) copy->prepareFrame(m_index);
    return copy;
}

bool WebpAnimationBackend::hasSource() const { return !m_path.isEmpty(); }
bool WebpAnimationBackend::ensureLoaded() { return m_reader && !m_frame.isNull() ? true : open(); }

void WebpAnimationBackend::unload()
{
    m_reader.reset();
    m_frame = {};
    m_canvasSize = {};
    m_count = 0;
    m_index = -1;
    m_decodedDurationMs = 0;
    m_error.clear();
}

bool WebpAnimationBackend::fail(const QString &error)
{
    unload();
    m_error = error;
    return false;
}

bool WebpAnimationBackend::open()
{
    unload();
    const QFileInfo file(m_path);
    if (!file.isFile() || file.size() <= 0 || file.size() > MaxFileBytes)
        return fail(QStringLiteral("WebP 文件不存在、为空或超过 32 MiB。"));
    QFile signature(m_path);
    if (!signature.open(QIODevice::ReadOnly)) return fail(QStringLiteral("无法读取 WebP 文件。"));
    const QByteArray header = signature.read(32);
    if (header.size() < 25 || !header.startsWith("RIFF") || header.mid(8, 4) != "WEBP")
        return fail(QStringLiteral("资源不是 WebP 文件，不能用其他格式替代。"));
    const auto *bytes = reinterpret_cast<const unsigned char *>(header.constData() + 4);
    const quint64 riffSize = quint64(bytes[0]) | quint64(bytes[1]) << 8 | quint64(bytes[2]) << 16 | quint64(bytes[3]) << 24;
    if (riffSize + 8 != quint64(file.size())) return fail(QStringLiteral("WebP RIFF 长度损坏或含额外数据。"));
    // Bound canvas before Qt scans animation metadata and allocates its compositor.
    const auto *h = reinterpret_cast<const unsigned char *>(header.constData());
    const auto u24 = [](const unsigned char *p) { return int(p[0]) | int(p[1]) << 8 | int(p[2]) << 16; };
    QSize declared;
    if (header.mid(12, 4) == "VP8X" && header.size() >= 30)
        declared = QSize(1 + u24(h + 24), 1 + u24(h + 27));
    else if (header.mid(12, 4) == "VP8L" && h[20] == 0x2f)
        declared = QSize(1 + ((int(h[22]) & 0x3f) << 8 | h[21]),
                         1 + ((int(h[24]) & 0xf) << 10 | int(h[23]) << 2 | int(h[22]) >> 6));
    else if (header.mid(12, 4) == "VP8 " && header.size() >= 30 && header.mid(23, 3) == QByteArray("\x9d\x01\x2a", 3))
        declared = QSize((h[26] | int(h[27]) << 8) & 0x3fff, (h[28] | int(h[29]) << 8) & 0x3fff);
    if (!declared.isValid() || declared.width() > MaxSide || declared.height() > MaxSide)
        return fail(QStringLiteral("WebP 画布头部损坏或超过 2048 像素。"));
    if (!QImageReader::supportedImageFormats().contains("webp"))
        return fail(QStringLiteral("缺少可用的 Qt WebP 插件（imageformats/qwebp）。请完整安装发布包。"));
    m_reader = std::make_unique<QImageReader>(m_path, "webp");
    m_reader->setAutoDetectImageFormat(false);
    m_reader->setDecideFormatFromContent(false);
    if (!m_reader->canRead()) return fail(QStringLiteral("WebP 解码器无法读取资源：") + m_reader->errorString());
    m_canvasSize = m_reader->size();
    if (!m_canvasSize.isValid() || m_canvasSize.width() > MaxSide || m_canvasSize.height() > MaxSide)
        return fail(QStringLiteral("WebP 画布必须在 1–2048 像素范围内。"));
    m_count = m_reader->imageCount();
    if (m_count < 1 || m_count > MaxFrames)
        return fail(QStringLiteral("WebP 帧数必须在 1–3000 范围内。"));
    return readNext();
}

bool WebpAnimationBackend::readNext()
{
    QImage image = m_reader->read();
    if (image.isNull() || image.size() != m_canvasSize)
        return fail(QStringLiteral("WebP 帧解码失败：") + m_reader->errorString());
    // Zero-delay frames must not spin a busy loop; positive container timing is preserved.
    const int delay = m_reader->nextImageDelay();
    if (delay > 60000) return fail(QStringLiteral("WebP 单帧持续时间超过 60 秒。"));
    m_delayMs = delay > 0 ? delay : (m_reader->supportsAnimation() ? 10 : 1000);
    m_decodedDurationMs += m_delayMs;
    if (m_decodedDurationMs > 300000) return fail(QStringLiteral("WebP 总时长超过 5 分钟。"));
    if (m_targetSize.isValid())
        image = image.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_frame = QPixmap::fromImage(image);
    ++m_index;
    return true;
}

bool WebpAnimationBackend::prepareFrame(int index)
{
    if (index < 0 || !ensureLoaded() || index >= m_count) return false;
    if (index < m_index && !open()) return false; // Reopen resets decoder canvas at loop/restart.
    while (m_index < index)
        if (!readNext()) return false;
    return true;
}

int WebpAnimationBackend::frameCount() const { return m_count; }
int WebpAnimationBackend::loadedFrameCount() const { return m_frame.isNull() ? 0 : 1; }
const QPixmap &WebpAnimationBackend::frameAt(int index) const
{
    Q_ASSERT(index == m_index);
    return m_frame;
}
int WebpAnimationBackend::frameDurationMs(int index, int) const
{
    Q_ASSERT(index == m_index);
    return m_delayMs;
}
qint64 WebpAnimationBackend::estimatedMemoryBytes() const
{
    if (!m_reader) return 0;
    // Conservative cache accounting: compressed input, decoder canvas + scratch,
    // and the scaled display pixmap. Actual decoder/allocator peak is measured separately.
    return QFileInfo(m_path).size() + qint64(m_canvasSize.width()) * m_canvasSize.height() * 8
        + qint64(m_frame.width()) * m_frame.height() * m_frame.depth() / 8;
}
QString WebpAnimationBackend::errorString() const { return m_error; }

bool WebpAnimationBackend::validateFile(const QString &path, QString *error)
{
    WebpAnimationBackend backend(path, {});
    if (!backend.ensureLoaded())
    {
        if (error) *error = backend.errorString();
        return false;
    }
    const int count = backend.frameCount();
    qint64 duration = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!backend.prepareFrame(i))
        {
            if (error) *error = backend.errorString();
            return false;
        }
        duration += backend.frameDurationMs(i, 0);
        if (duration > 300000)
        {
            if (error) *error = QStringLiteral("WebP 总时长超过 5 分钟。");
            return false;
        }
    }
    return true;
}
