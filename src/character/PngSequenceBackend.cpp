#include "PngSequenceBackend.h"
#include <utility>

PngSequenceBackend::PngSequenceBackend(QStringList paths, QSize targetSize)
    : m_paths(std::move(paths)), m_targetSize(targetSize)
{
}

std::unique_ptr<IPetMediaBackend> PngSequenceBackend::clone() const
{
    return std::make_unique<PngSequenceBackend>(*this);
}

bool PngSequenceBackend::hasSource() const { return !m_paths.isEmpty(); }

bool PngSequenceBackend::ensureLoaded()
{
    if (!m_frames.isEmpty()) return true;
    for (const QString &path : m_paths)
    {
        QPixmap frame(path);
        if (frame.isNull()) continue;
        if (m_targetSize.isValid())
            frame = frame.scaled(m_targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_frames.append(frame);
    }
    return !m_frames.isEmpty();
}

void PngSequenceBackend::unload()
{
    m_frames.clear();
    m_frames.squeeze();
}

int PngSequenceBackend::frameCount() const { return m_frames.size(); }
const QPixmap &PngSequenceBackend::frameAt(int index) const { return m_frames.at(index); }

int PngSequenceBackend::frameDurationMs(int, int effectiveFps) const
{
    // Includes PetWindow's low-power FPS override, applied after construction.
    return qMax(1, 1000 / qMax(1, effectiveFps));
}

qint64 PngSequenceBackend::estimatedMemoryBytes() const
{
    qint64 bytes = 0;
    for (const QPixmap &frame : m_frames)
        bytes += qint64(frame.width()) * frame.height() * frame.depth() / 8;
    return bytes;
}
