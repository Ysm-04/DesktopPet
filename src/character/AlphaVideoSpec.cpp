#include "AlphaVideoSpec.h"
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

void AlphaVideoSpec::configureRuntime()
{
    // Qt 6.5.3's FFmpeg hardware surface pool failed on the real 512px sample.
    // Default to software BEFORE the first decoder initializes its static list.
    // This affects this process only and preserves explicit diagnostic overrides.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArray(","));
}

namespace
{
quint32 u32(const QByteArray &b, qsizetype p) { return qFromBigEndian<quint32>(b.constData() + p); }
quint64 u64(const QByteArray &b, qsizetype p) { return qFromBigEndian<quint64>(b.constData() + p); }
quint16 u16(const QByteArray &b, qsizetype p) { return qFromBigEndian<quint16>(b.constData() + p); }
struct Box { QByteArray type, data; };
bool boxes(const QByteArray &data, QList<Box> *out)
{
    qsizetype offset = 0;
    while (offset < data.size())
    {
        if (data.size() - offset < 8 || out->size() >= 20000) return false;
        quint64 size = u32(data, offset);
        qsizetype header = 8;
        if (size == 1)
        {
            if (data.size() - offset < 16) return false;
            size = u64(data, offset + 8); header = 16;
        }
        if (size == 0) size = quint64(data.size() - offset);
        if (size < quint64(header) || size > quint64(data.size() - offset)) return false;
        out->append({data.mid(offset + 4, 4), data.mid(offset + header, qsizetype(size) - header)});
        offset += qsizetype(size);
    }
    return true;
}
QByteArray child(const QByteArray &data, const QByteArray &type)
{
    QList<Box> children;
    if (!boxes(data, &children)) return {};
    QByteArray result;
    for (const auto &box : children)
        if (box.type == type)
        {
            if (!result.isEmpty()) return {}; // No ambiguous duplicate metadata.
            result = box.data;
        }
    return result;
}
bool reject(QString *error, const QString &text)
{
    if (error) *error = text;
    return false;
}
}

bool AlphaVideoSpec::inspectFile(const QString &path, const QString &layout,
                                   AlphaVideoInfo *info, QString *error)
{
    if (error) error->clear();
    if (layout != QStringLiteral("left-right") && layout != QStringLiteral("top-bottom"))
        return reject(error, QStringLiteral("alpha_video.layout 必须为 left-right 或 top-bottom。"));
    const QFileInfo fileInfo(path);
    if (!fileInfo.isFile() || fileInfo.isSymLink() || fileInfo.suffix().compare("mp4", Qt::CaseInsensitive) != 0
        || fileInfo.size() < 32 || fileInfo.size() > 128 * 1024 * 1024)
        return reject(error, QStringLiteral("透明视频必须为 128MiB 以内的本地 .mp4 文件。"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return reject(error, QStringLiteral("无法读取透明视频。"));
    QByteArray moov;
    bool ftyp = false, mdat = false;
    QList<QPair<quint64, quint64>> mediaRanges;
    for (int count = 0; file.pos() < file.size(); ++count)
    {
        if (count >= 10000) return reject(error, QStringLiteral("MP4 顶层块过多。"));
        const quint64 position = quint64(file.pos());
        const QByteArray header = file.read(8);
        if (header.size() != 8) return reject(error, QStringLiteral("MP4 块头截断。"));
        quint64 size = u32(header, 0);
        quint64 headerSize = 8;
        if (size == 1)
        {
            const QByteArray large = file.read(8);
            if (large.size() != 8) return reject(error, QStringLiteral("MP4 大块头截断。"));
            size = u64(large, 0); headerSize = 16;
        }
        if (size == 0) size = quint64(file.size()) - position;
        if (size < headerSize || size > quint64(file.size()) - position)
            return reject(error, QStringLiteral("MP4 块大小或文件长度错误。"));
        const QByteArray type = header.mid(4, 4);
        if (type == "ftyp") ftyp = size >= 16;
        if (type == "mdat")
        {
            mdat = true;
            mediaRanges.append({position + headerSize, position + size});
        }
        if (type == "moof") return reject(error, QStringLiteral("暂不支持分片 MP4。"));
        if (type == "moov")
        {
            if (!moov.isEmpty() || size > 4 * 1024 * 1024)
                return reject(error, QStringLiteral("MP4 元数据重复或超过 4MiB。"));
            moov = file.read(qint64(size - headerSize));
            if (quint64(moov.size()) != size - headerSize) return reject(error, QStringLiteral("MP4 元数据截断。"));
        }
        if (!file.seek(qint64(position + size))) return reject(error, QStringLiteral("MP4 读取失败。"));
    }
    QList<Box> tracks;
    if (!ftyp || !mdat || !boxes(moov, &tracks) || !child(moov, "mvex").isEmpty())
        return reject(error, QStringLiteral("需要含完整元数据的普通 MP4 文件。"));
    AlphaVideoInfo parsed;
    int videos = 0;
    for (const auto &track : tracks)
    {
        if (track.type != "trak") continue;
        const auto mdia = child(track.data, "mdia");
        const auto minf = child(mdia, "minf");
        const auto dref = child(child(minf, "dinf"), "dref");
        // Never let an imported container refer the decoder to external files.
        QList<Box> references;
        if (dref.size() < 8 || u32(dref, 4) != 1 || !boxes(dref.mid(8), &references)
            || references.size() != 1 || references[0].type != "url "
            || references[0].data != QByteArray::fromHex("00000001"))
            return reject(error, QStringLiteral("MP4 必须自包含，禁止外部媒体引用。"));
        const auto handler = child(mdia, "hdlr");
        if (handler.size() < 12 || handler.mid(8, 4) != "vide") continue;
        if (++videos != 1) return reject(error, QStringLiteral("只支持单视频轨 MP4。"));
        const auto tkhd = child(track.data, "tkhd");
        const int matrix = !tkhd.isEmpty() && tkhd[0] == 1 ? 52 : 40;
        if (tkhd.size() < matrix + 36
            || tkhd.mid(matrix, 36) != QByteArray::fromHex("000100000000000000000000000000000001000000000000000000000000000040000000"))
            return reject(error, QStringLiteral("透明视频不支持旋转或变换轨道矩阵。"));
        const auto mdhd = child(mdia, "mdhd");
        if (mdhd.size() < 20) return reject(error, QStringLiteral("视频时间元数据缺失。"));
        const bool longTime = mdhd[0] == 1;
        if ((mdhd[0] != 0 && !longTime) || (longTime && mdhd.size() < 32))
            return reject(error, QStringLiteral("视频时间元数据错误。"));
        const quint32 scale = u32(mdhd, longTime ? 20 : 12);
        const quint64 duration = longTime ? u64(mdhd, 24) : u32(mdhd, 16);
        if (scale == 0 || duration == 0 || duration > quint64(scale) * 300)
            return reject(error, QStringLiteral("视频时长必须在 0–300 秒之间。"));
        parsed.durationMs = qint64((duration * 1000 + scale - 1) / scale);
        const auto stbl = child(minf, "stbl");
        const auto stsd = child(stbl, "stsd");
        QList<Box> entries;
        if (stsd.size() < 8 || u32(stsd, 4) != 1 || !boxes(stsd.mid(8), &entries)
            || entries.size() != 1 || entries[0].type != "avc1" || entries[0].data.size() < 78)
            return reject(error, QStringLiteral("首版透明视频只支持 H.264 / avc1 编码。"));
        const auto sample = entries[0].data;
        parsed.packedSize = QSize(u16(sample, 24), u16(sample, 26));
        const int w = parsed.packedSize.width(), h = parsed.packedSize.height();
        if (w < 2 || h < 2 || w > 2048 || h > 2048 || w % 2 || h % 2
            || (layout == "left-right" ? w % 4 : h % 4))
            return reject(error, QStringLiteral("视频宽高须为偶数且不超过 2048，拼接轴须为 4 的倍数。"));
        if (child(sample.mid(78), "avcC").isEmpty())
            return reject(error, QStringLiteral("H.264 配置缺失。"));
        const auto stts = child(stbl, "stts");
        if (stts.size() < 8 || u32(stts, 4) == 0 || u32(stts, 4) > 18000
            || quint64(stts.size()) != 8 + quint64(u32(stts, 4)) * 8)
            return reject(error, QStringLiteral("视频时序表错误。"));
        quint64 samples = 0, ticks = 0;
        for (int p = 8; p < stts.size(); p += 8)
        {
            const quint32 n = u32(stts, p), delta = u32(stts, p + 4);
            if (!n || !delta || quint64(delta) * 60 < scale || delta > scale)
                return reject(error, QStringLiteral("视频帧间隔范围为 1–60fps。"));
            samples += n; ticks += quint64(n) * delta;
            if (samples > 18000 || ticks > quint64(scale) * 300)
                return reject(error, QStringLiteral("视频最多 18000 帧或 300 秒。"));
        }
        if (ticks != duration) return reject(error, QStringLiteral("视频时长与帧时序不一致。"));
        parsed.sampleCount = int(samples);
        const auto sizes = child(stbl, "stsz");
        if (sizes.size() < 12 || u32(sizes, 8) != samples
            || (u32(sizes, 4) != 0 && sizes.size() != 12)
            || (u32(sizes, 4) == 0 && quint64(sizes.size()) != 12 + samples * 4))
            return reject(error, QStringLiteral("视频采样表不完整。"));
        auto offsets = child(stbl, "stco");
        int stride = 4;
        if (offsets.isEmpty()) { offsets = child(stbl, "co64"); stride = 8; }
        if (offsets.size() < 8 || !u32(offsets, 4)
            || quint64(offsets.size()) != 8 + quint64(u32(offsets, 4)) * stride)
            return reject(error, QStringLiteral("视频块偏移表错误。"));
        const auto chunks = child(stbl, "stsc");
        const quint32 chunkCount = u32(offsets, 4);
        if (chunks.size() < 20 || u32(chunks, 4) == 0
            || quint64(chunks.size()) != 8 + quint64(u32(chunks, 4)) * 12 || u32(chunks, 8) != 1)
            return reject(error, QStringLiteral("视频采样到块映射不完整。"));
        quint64 sampleIndex = 0;
        int mapping = 8;
        for (int p = 8; p < chunks.size(); p += 12)
        {
            if (!u32(chunks, p) || u32(chunks, p) > chunkCount || !u32(chunks, p + 4)
                || u32(chunks, p + 8) != 1 || (p > 8 && u32(chunks, p) <= u32(chunks, p - 12)))
                return reject(error, QStringLiteral("视频采样到块映射错误。"));
        }
        for (int p = 8, chunk = 1; p < offsets.size(); p += stride, ++chunk)
        {
            const quint64 offset = stride == 4 ? u32(offsets, p) : u64(offsets, p);
            while (mapping + 12 < chunks.size() && u32(chunks, mapping + 12) <= quint32(chunk)) mapping += 12;
            const quint32 perChunk = u32(chunks, mapping + 4);
            if (sampleIndex + perChunk > samples)
                return reject(error, QStringLiteral("视频块包含的采样数越界。"));
            quint64 length = 0;
            for (quint32 s = 0; s < perChunk; ++s, ++sampleIndex)
            {
                const quint32 size = u32(sizes, 4) ? u32(sizes, 4) : u32(sizes, 12 + qsizetype(sampleIndex) * 4);
                if (!size) return reject(error, QStringLiteral("视频含零长度采样。"));
                length += size;
            }
            bool contained = false;
            for (const auto &range : mediaRanges)
                if (offset >= range.first && offset < range.second && length <= range.second - offset) contained = true;
            if (!contained) return reject(error, QStringLiteral("视频块的完整采样范围超出媒体数据。"));
        }
        if (sampleIndex != samples) return reject(error, QStringLiteral("视频采样映射数量不一致。"));
    }
    if (videos != 1) return reject(error, QStringLiteral("MP4 缺少可用的视频轨。"));
    if (info) *info = parsed;
    return true;
}
