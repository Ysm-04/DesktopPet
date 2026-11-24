#pragma once
#include <QSize>
#include <QString>

struct AlphaVideoInfo
{
    QSize packedSize;
    qint64 durationMs = 0;
    int sampleCount = 0;
};

// Header/container validation only. Actual codec availability and frame content
// are checked during asynchronous playback and by the release runtime probe.
namespace AlphaVideoSpec
{
void configureRuntime();
bool inspectFile(const QString &path, const QString &layout, AlphaVideoInfo *info, QString *error);
}
