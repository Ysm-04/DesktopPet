#pragma once

#include "IPetMediaBackend.h"
#include <QSize>
#include <QStringList>
#include <QVector>

class PngSequenceBackend final : public IPetMediaBackend
{
public:
    PngSequenceBackend(QStringList paths, QSize targetSize);
    std::unique_ptr<IPetMediaBackend> clone() const override;
    bool hasSource() const override;
    bool ensureLoaded() override;
    void unload() override;
    int frameCount() const override;
    bool prepareFrame(int index) override { return index >= 0 && index < m_frames.size(); }
    const QPixmap &frameAt(int index) const override;
    int frameDurationMs(int index, int effectiveFps) const override;
    qint64 estimatedMemoryBytes() const override;

private:
    QStringList m_paths;
    QSize m_targetSize;
    QVector<QPixmap> m_frames;
};
