// Frame-sequence probe helpers split from FrameSequence.cpp: delay
// normalization, the bounded animation duration probe, and header-only page
// sizes. Internal to the core image layer (see FrameSequenceDetail.h).
#include "core/async/CooperativeCancel.h"
#include "core/image/FrameSequenceDetail.h"
#include "core/image/QtMetadataSemantics.h"

#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QString>

#include <cstdint>

namespace mviewer::core
{

namespace
{

constexpr int kMinimumAnimationDelayMs = 10;
// Delays of 0–10 ms are the GIF/WebP "as fast as possible" sentinel. Browsers
// present them for 100 ms so playback stays watchable.
constexpr int kBrowserAnimationDelayMs = 100;
constexpr int kTimingProbeEdge = 1;

QString qpath(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

// Plugins publish the frame delay either before the advance or after it. A
// non-positive "after" keeps "before" so a 0 ms sentinel that follows a short
// frame is not replaced by that short frame.
int delayAcrossAdvance(int before, int after)
{
    const int delay = after > 0 ? after : before;
    return normalizedAnimationDelayMs(delay);
}

// True when every frame delay was visible without read(). qgif and qwebp do
// not override jumpToNextImage() (the base handler returns false) and only
// fill nextImageDelay() inside read(); they take the scaled-read fallback.
bool sumDelaysWithoutPixels(const QString &fileName, int frameCount, int64_t &total,
                            bool &cancelled)
{
    cancelled = false;
    total = 0;
    if (fileName.isEmpty() || frameCount <= 0)
        return false;
    QImageReader timing(fileName);
    timing.setAutoTransform(true);
    for (int i = 0; i < frameCount; ++i)
    {
        if (cooperativeCancelRequested())
        {
            cancelled = true;
            return false;
        }
        const int before = timing.nextImageDelay();
        const int number = timing.currentImageNumber();
        if (i + 1 < frameCount)
        {
            if (!timing.jumpToNextImage() || timing.currentImageNumber() == number)
                return false;
        }
        const int after = timing.nextImageDelay();
        if (before <= 0 && after <= 0)
            return false;
        total += delayAcrossAdvance(before, after);
    }
    return true;
}

// scale == true asks for a 1x1 decode. Handlers that honor ScaledSize skip the
// native raster; others still report the container delay. A null image returns
// false so the caller can retry once at native size (historical probe).
bool sumDelaysByRead(const QString &fileName, int frameCount, bool scale, int64_t &total,
                     bool &cancelled)
{
    cancelled = false;
    total = 0;
    QImageReader timing(fileName);
    timing.setAutoTransform(true);
    if (scale)
        timing.setScaledSize(QSize(kTimingProbeEdge, kTimingProbeEdge));
    for (int i = 0; i < frameCount; ++i)
    {
        if (cooperativeCancelRequested())
        {
            cancelled = true;
            return false;
        }
        const int before = timing.nextImageDelay();
        const QImage image = timing.read();
        if (image.isNull())
            return false;
        const int after = timing.nextImageDelay();
        total += delayAcrossAdvance(before, after);
    }
    return true;
}

} // namespace

int normalizedAnimationDelayMs(int delayMs)
{
    if (delayMs <= kMinimumAnimationDelayMs)
        return kBrowserAnimationDelayMs;
    return delayMs;
}

bool probeAnimationDurationMs(const std::string &path, int frameCount, int64_t &total)
{
    const QString fileName = qpath(path);
    // Fresh reader: qgif/qwebp expose the first frame through read(), not
    // jumpToImage(0), and the caller's reader stays put for a later decode.
    // Handlers that jump without pixels or honor a 1x1 ScaledSize skip the
    // native-size decode; the last pass is the historical native read.
    total = 0;
    bool cancelled = false;
    bool summed = sumDelaysWithoutPixels(fileName, frameCount, total, cancelled);
    if (!summed && !cancelled)
        summed = sumDelaysByRead(fileName, frameCount, true, total, cancelled);
    if (!summed && !cancelled)
        sumDelaysByRead(fileName, frameCount, false, total, cancelled);
    return !cancelled;
}

namespace
{

QSize sizeWithOrientation(QImageReader &reader)
{
    // size() is the stored size. read() with autoTransform displays the
    // transposed geometry, so a header-only answer has to swap too.
    QSize size = reader.size();
    if (qtmetadata::transformSwapsDimensions(reader.transformation()))
        size.transpose();
    return size;
}

bool fillEmptySize(QImageReader &reader, QSize &size)
{
    if (!size.isEmpty())
        return true;
    const QImage image = reader.read();
    if (image.isNull())
        return false;
    size = image.size();
    return true;
}

} // namespace

FrameInfo stillFrameInfo(const std::string &path, int frameIndex)
{
    QImageReader reader(qpath(path));
    reader.setAutoTransform(true);
    QSize size;
    if (frameIndex == 0 || reader.jumpToImage(frameIndex))
    {
        size = sizeWithOrientation(reader);
        if (!fillEmptySize(reader, size))
            return {};
    }
    else
    {
        QImageReader sequential(qpath(path));
        sequential.setAutoTransform(true);
        for (int i = 0; i <= frameIndex; ++i)
        {
            if (cooperativeCancelRequested())
                return {};
            if (i == frameIndex)
            {
                size = sizeWithOrientation(sequential);
                if (!fillEmptySize(sequential, size))
                    return {};
                break;
            }
            if (sequential.jumpToNextImage())
                continue;
            sequential.setScaledSize(QSize(kTimingProbeEdge, kTimingProbeEdge));
            const bool advanced = !sequential.read().isNull();
            sequential.setScaledSize(QSize());
            if (!advanced)
                return {};
        }
    }
    if (size.isEmpty())
        return {};
    return {frameIndex, 0, size.width(), size.height()};
}

} // namespace mviewer::core
