#pragma once

#include "IPetMediaBackend.h"
#include <QImageReader>
#include <QSize>

class WebpAnimationBackend final : public IPetMediaBackend
{
public:
    static constexpr qint64 MaxFileBytes = 32 * 1024 * 1024;
    static constexpr int MaxSide = 2048;
    static constexpr int MaxFrames = 3000;
    WebpAnimationBackend(QString path, QSize targetSize);
    std::unique_ptr<IPetMediaBackend> clone() const override;
    bool hasSource() const override;
    bool ensureLoaded() override;
    void unload() override;
    int frameCount() const override;
    int loadedFrameCount() const override;
    bool prepareFrame(int index) override;
    const QPixmap &frameAt(int index) const override;
    int frameDurationMs(int index, int effectiveFps) const override;
    qint64 estimatedMemoryBytes() const override;
    QString errorString() const override;
    static bool validateFile(const QString &path, QString *error);

private:
    bool open();
    bool readNext();
    bool fail(const QString &error);
    QString m_path;
    QSize m_targetSize;
    std::unique_ptr<QImageReader> m_reader;
    QPixmap m_frame;
    QSize m_canvasSize;
    int m_count = 0;
    int m_index = -1;
    int m_delayMs = 100;
    qint64 m_decodedDurationMs = 0;
    QString m_error;
};
