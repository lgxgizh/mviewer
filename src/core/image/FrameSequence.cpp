#include "core/image/FrameSequence.h"

#include "core/async/CooperativeCancel.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/FrameSequenceDetail.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"

#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>

namespace mviewer::core
{

namespace
{

constexpr int kUnknownFrameCount = 1;
constexpr int kMinimumAnimationDelayMs = 10;
// Delays of 0–10 ms are the GIF/WebP "as fast as possible" sentinel. Browsers
// present them for 100 ms so playback stays watchable.
constexpr int kBrowserAnimationDelayMs = 100;
constexpr int kMaxTimingProbeFrames = 256;
// A crafted container can declare an absurd page/frame count, and every
// consumer uses the declared count as a loop bound, so it is clamped.
constexpr int kMaxFrameCount = 100000;
// Sequential animation walk bound. Page sources seek with jumpToImage and may
// use any index in [0, frameCount). Real animations stay far below this (60 s
// at 60 fps is 3600 frames); the bound keeps a session file with a wild frame
// index from decoding for minutes.
constexpr int kMaxSequentialFrameWalk = 4096;
// Compare is two panes; one slot used to make the second open abandon the
// first. A few readers cover playback plus the other pane.
constexpr int kMaxAnimationSessions = 4;
constexpr int kTimingProbeEdge = 1;

QString qpath(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

bool isTiff(const QString &suffix)
{
    return suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff");
}

FrameSequenceKind kindFor(const QImageReader &reader, const QString &suffix, int count)
{
    if (isTiff(suffix) && count > 1)
        return FrameSequenceKind::Pages;
    if (reader.supportsAnimation() && count > 1)
        return FrameSequenceKind::Animation;
    return FrameSequenceKind::Static;
}

int safeFrameCount(const QImageReader &reader)
{
    const int count = reader.imageCount();
    if (count <= 0)
        return kUnknownFrameCount;
    return std::min(count, kMaxFrameCount);
}

void fillMetadata(const std::string &path, QImageReader &reader, const QImage &image,
                  const FrameSequenceInfo &sequence, int frameIndex, int frameDelay,
                  mviewer::domain::ImageMetadata &meta)
{
    const QFileInfo info(qpath(path));
    meta.filePath = path;
    meta.fileName = info.fileName().toUtf8().toStdString();
    meta.fileSize = info.size();
    meta.modifiedEpochSec = info.lastModified().toSecsSinceEpoch();
    // QImageReader autoTransform has already produced the displayed geometry
    // for static/page sources.  Animation frames intentionally remain raw
    // (their container has no EXIF orientation contract), so image dimensions
    // are authoritative in both cases.
    meta.width = image.width();
    meta.height = image.height();
    const QImage::Format sourceFormat = reader.imageFormat();
    qtmetadata::applyReaderRasterMetadata(sourceFormat, image, meta);
    meta.format = QString::fromLatin1(reader.format()).toUpper().toStdString();
    if (meta.format.empty())
        meta.format = info.suffix().toUpper().toStdString();
    meta.orientation = qtmetadata::orientationFromTransform(reader.transformation());
    meta.frameCount = sequence.frameCount;
    meta.currentFrame = frameIndex;
    meta.animated = sequence.animated;
    meta.loopCount = sequence.loopCount;
    meta.durationMs = sequence.totalDurationMs;
    meta.frameDurationMs = frameDelay;
    meta.sequenceKind =
        sequence.kind == FrameSequenceKind::Pages
            ? "pages"
            : (sequence.kind == FrameSequenceKind::Animation ? "animation" : "static");

    // Some animation/page plugins do not carry the container ICC onto every
    // selected frame.  Probe one bounded pixel when needed so all sequence
    // frames expose the same source profile without materializing the source.
    if (!meta.hasIccProfile)
    {
        QImageReader profileReader(qpath(path));
        profileReader.setAutoTransform(sequence.kind != FrameSequenceKind::Animation);
        profileReader.setScaledSize(QSize(1, 1));
        QImage tiny;
        if (profileReader.read(&tiny))
            qtmetadata::applyColorMetadata(tiny, meta);
    }
}

int normalizedAnimationDelayMs(int delayMs)
{
    if (delayMs <= kMinimumAnimationDelayMs)
        return kBrowserAnimationDelayMs;
    return delayMs;
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

FrameSequenceInfo probeReader(QImageReader &reader, const QString &suffix)
{
    FrameSequenceInfo sequence;
    if (!reader.canRead())
        return sequence;

    sequence.valid = true;
    sequence.frameCount = safeFrameCount(reader);
    sequence.countKnown = reader.imageCount() > 0;
    sequence.kind = kindFor(reader, suffix, sequence.frameCount);
    sequence.animated = sequence.kind == FrameSequenceKind::Animation;
    sequence.loopCount = reader.supportsAnimation() ? reader.loopCount() : -1;

    // Duration probing is intentionally bounded. A 10,000-frame source still
    // gets an O(1) open/probe; frameInfo() supplies the current delay on demand.
    // Short animations sum delays without a native-size decode when the handler
    // can either jump without pixels or honor a 1x1 ScaledSize.
    if (sequence.animated && sequence.countKnown && sequence.frameCount <= kMaxTimingProbeFrames)
    {
        // Fresh reader: qgif/qwebp expose the first frame through read(), not
        // jumpToImage(0), and the caller's reader stays put for a later decode.
        int64_t total = 0;
        bool cancelled = false;
        bool summed =
            sumDelaysWithoutPixels(reader.fileName(), sequence.frameCount, total, cancelled);
        if (!summed && !cancelled)
        {
            summed =
                sumDelaysByRead(reader.fileName(), sequence.frameCount, true, total, cancelled);
        }
        if (!summed && !cancelled)
            sumDelaysByRead(reader.fileName(), sequence.frameCount, false, total, cancelled);
        if (cancelled)
            return sequence;
        sequence.totalDurationMs = total;
        sequence.durationKnown = true;
    }
    return sequence;
}

QImage scaleToMaxEdge(QImage image, int maxEdge)
{
    if (maxEdge <= 0 || image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        std::max(image.width(), image.height()) <= maxEdge)
        return image;
    const int maxDim = std::max(image.width(), image.height());
    const double ratio = static_cast<double>(maxEdge) / maxDim;
    return image.scaled(QSize((std::max)(1, static_cast<int>(std::round(image.width() * ratio))),
                              (std::max)(1, static_cast<int>(std::round(image.height() * ratio)))),
                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

struct SelectedFrame
{
    bool ok = false;
    QImage image;
    QSize sourceSize;
    int delayMs = 0;
    std::string error;
};

struct AnimationSession
{
    std::string path;
    qint64 size = -1;
    qint64 modifiedMs = -1;
    std::atomic<int> nextIndex{0};
    std::atomic<int> lastIndex{-1};
    int lastDelayMs = 0;
    QImage lastImage;
    std::atomic<bool> abandon{false};
    std::mutex mu;
    std::unique_ptr<QImageReader> reader;
};

std::mutex &animationMutex()
{
    // Process-lifetime: AnimationSession holds QImageReader, which must not be
    // destroyed after QCoreApplication (ADR 017). releaseFile() drops the
    // reader while the event loop is alive; the pointer storage is leaked.
    static auto *mutex = new std::mutex();
    return *mutex;
}

struct AnimationSlot
{
    std::shared_ptr<AnimationSession> session;
    uint64_t stamp = 0;
};

struct AnimationCache
{
    AnimationSlot slots[kMaxAnimationSessions];
    int size = 0;
    uint64_t clock = 0;
};

AnimationCache &animationCache()
{
    // Same lifetime as animationMutex(): the slot table must outlive
    // QCoreApplication. Readers themselves are dropped by releaseFile().
    static auto *cache = new AnimationCache();
    return *cache;
}

bool animationPathMatches(const AnimationSession &session, const std::string &path)
{
    if (session.path == path)
        return true;
    return QFileInfo(qpath(session.path)) == QFileInfo(qpath(path));
}

bool animationFileMatches(const AnimationSession &session, const std::string &path,
                          const QFileInfo &info)
{
    if (session.size != info.size() ||
        session.modifiedMs != info.lastModified().toMSecsSinceEpoch())
        return false;
    return animationPathMatches(session, path);
}

std::shared_ptr<AnimationSession> openAnimationSession(const std::string &path,
                                                       const QFileInfo &info)
{
    auto session = std::make_shared<AnimationSession>();
    session->path = path;
    session->size = info.size();
    session->modifiedMs = info.lastModified().toMSecsSinceEpoch();
    session->reader = std::make_unique<QImageReader>(qpath(path));
    session->reader->setAutoTransform(false);
    return session;
}

bool animationNeedsRewind(const AnimationSession &session, int frameIndex)
{
    const int next = session.nextIndex.load();
    const int last = session.lastIndex.load();
    return next > frameIndex && last != frameIndex;
}

int animationSlotIndex(const AnimationCache &cache, const std::string &path)
{
    for (int i = 0; i < cache.size; ++i)
    {
        if (cache.slots[i].session && animationPathMatches(*cache.slots[i].session, path))
            return i;
    }
    return -1;
}

int leastRecentAnimationSlot(const AnimationCache &cache)
{
    int oldest = 0;
    for (int i = 1; i < cache.size; ++i)
    {
        if (cache.slots[i].stamp < cache.slots[oldest].stamp)
            oldest = i;
    }
    return oldest;
}

std::shared_ptr<AnimationSession> prepareAnimationSession(const std::string &path, int frameIndex)
{
    const QFileInfo info(qpath(path));
    std::lock_guard<std::mutex> lock(animationMutex());
    auto &cache = animationCache();
    const int existing = animationSlotIndex(cache, path);
    if (existing >= 0)
    {
        AnimationSlot &slot = cache.slots[existing];
        const bool keep = slot.session && !slot.session->abandon.load() &&
                          animationFileMatches(*slot.session, path, info) &&
                          !animationNeedsRewind(*slot.session, frameIndex);
        if (!keep)
        {
            // Rewind, replace, or a changed file. Another path's reader stays.
            if (slot.session)
                slot.session->abandon.store(true);
            slot.session = openAnimationSession(path, info);
        }
        slot.stamp = ++cache.clock;
        return slot.session;
    }

    auto session = openAnimationSession(path, info);
    int index = cache.size;
    if (cache.size < kMaxAnimationSessions)
    {
        ++cache.size;
    }
    else
    {
        index = leastRecentAnimationSlot(cache);
        if (cache.slots[index].session)
            cache.slots[index].session->abandon.store(true);
    }
    cache.slots[index].session = std::move(session);
    cache.slots[index].stamp = ++cache.clock;
    return cache.slots[index].session;
}

bool copyCachedAnimationFrame(const AnimationSession &session, int frameIndex, int maxEdge,
                              SelectedFrame &selected)
{
    if (session.lastIndex.load() != frameIndex || session.lastImage.isNull())
        return false;
    selected.ok = true;
    selected.sourceSize = session.lastImage.size();
    selected.delayMs = session.lastDelayMs;
    selected.image = scaleToMaxEdge(session.lastImage, maxEdge);
    return true;
}

int delayAfterAnimationRead(QImageReader &reader, int before)
{
    const int after = reader.nextImageDelay();
    const int raw = after > 0 ? after : before;
    return normalizedAnimationDelayMs(raw);
}

bool storeAnimationFrame(AnimationSession &session, QImage candidate, int delayMs, int frameIndex,
                         int maxEdge, SelectedFrame &selected)
{
    const int index = session.nextIndex.load();
    session.lastDelayMs = delayMs;
    session.lastImage = candidate;
    session.lastIndex.store(index);
    session.nextIndex.store(index + 1);
    if (index != frameIndex)
        return false;
    selected.ok = true;
    selected.sourceSize = candidate.size();
    selected.delayMs = delayMs;
    selected.image = scaleToMaxEdge(std::move(candidate), maxEdge);
    return true;
}

SelectedFrame finishAnimationRead(AnimationSession &session, int frameIndex, int maxEdge)
{
    SelectedFrame selected;
    if (session.abandon.load())
    {
        selected.error = "frame decode cancelled";
        return selected;
    }
    if (copyCachedAnimationFrame(session, frameIndex, maxEdge, selected))
        return selected;
    if (session.nextIndex.load() > frameIndex)
    {
        selected.error = "image reader cannot select frame";
        return selected;
    }

    QImageReader &reader = *session.reader;
    while (session.nextIndex.load() <= frameIndex)
    {
        if (session.abandon.load() || cooperativeCancelRequested())
        {
            selected.error = "frame decode cancelled";
            return selected;
        }
        const int before = reader.nextImageDelay();
        QImage candidate = reader.read();
        if (candidate.isNull())
        {
            session.abandon.store(true);
            selected.error = reader.errorString().toStdString();
            if (selected.error.empty())
                selected.error = "image reader cannot select frame";
            return selected;
        }
        const int delayMs = delayAfterAnimationRead(reader, before);
        if (storeAnimationFrame(session, std::move(candidate), delayMs, frameIndex, maxEdge,
                                selected))
            return selected;
    }
    if (!selected.ok)
        selected.error = "image reader cannot select frame";
    return selected;
}

SelectedFrame readAnimationFrame(const std::string &path, int frameIndex, int maxEdge)
{
    // One rewind attempt: a cursor that has already passed this index cannot
    // seek backward, so the next prepare opens a fresh reader at frame 0.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const std::shared_ptr<AnimationSession> session = prepareAnimationSession(path, frameIndex);
        if (!session || !session->reader)
        {
            SelectedFrame failed;
            failed.error = "image reader cannot select frame";
            return failed;
        }
        std::lock_guard<std::mutex> sessionLock(session->mu);
        SelectedFrame selected = finishAnimationRead(*session, frameIndex, maxEdge);
        const bool rewind = !selected.ok && session->nextIndex.load() > frameIndex &&
                            session->lastIndex.load() != frameIndex && !session->abandon.load();
        if (!rewind)
            return selected;
        session->abandon.store(true);
    }
    SelectedFrame failed;
    failed.error = "image reader cannot select frame";
    return failed;
}

SelectedFrame selectFrame(const std::string &path, const FrameSequenceInfo &sequence,
                          int frameIndex, int maxEdge)
{
    SelectedFrame selected;
    // The index can come from a restored session file. The walk cap applies
    // only to sequential animation reads. Pages seek with jumpToImage and may
    // be any index in [0, frameCount); decodeImpl already rejected the rest.
    if (frameIndex < 0 ||
        (sequence.kind == FrameSequenceKind::Animation && frameIndex > kMaxSequentialFrameWalk))
    {
        selected.error = "frame index out of range";
        return selected;
    }
    QImageReader reader(qpath(path));
    reader.setAutoTransform(sequence.kind != FrameSequenceKind::Animation);

    if (sequence.kind == FrameSequenceKind::Animation)
    {
        // qgif and qwebp only walk forward. Keep one reader per file so frame
        // N+1 does not decode 0..N again; releaseFile() drops it on navigate.
        selected = readAnimationFrame(path, frameIndex, maxEdge);
    }
    else if (frameIndex == 0)
    {
        selected.sourceSize = reader.size();
        const int before = reader.nextImageDelay();
        const int maxDim = std::max(selected.sourceSize.width(), selected.sourceSize.height());
        if (maxEdge > 0 && selected.sourceSize.isValid() && maxDim > maxEdge && maxDim > 0)
        {
            const double ratio = static_cast<double>(maxEdge) / maxDim;
            reader.setScaledSize(QSize(
                std::max(1, static_cast<int>(std::round(selected.sourceSize.width() * ratio))),
                std::max(1, static_cast<int>(std::round(selected.sourceSize.height() * ratio)))));
        }
        selected.image = reader.read();
        if (selected.sourceSize.isEmpty())
            selected.sourceSize = selected.image.size();
        const int after = reader.nextImageDelay();
        selected.delayMs = after > 0 ? after : before;
        selected.ok = !selected.image.isNull();
    }
    else if (reader.jumpToImage(frameIndex))
    {
        selected.sourceSize = reader.size();
        const int before = reader.nextImageDelay();
        const int maxDim = std::max(selected.sourceSize.width(), selected.sourceSize.height());
        if (maxEdge > 0 && selected.sourceSize.isValid() && maxDim > maxEdge && maxDim > 0)
        {
            const double ratio = static_cast<double>(maxEdge) / maxDim;
            reader.setScaledSize(QSize(
                std::max(1, static_cast<int>(std::round(selected.sourceSize.width() * ratio))),
                std::max(1, static_cast<int>(std::round(selected.sourceSize.height() * ratio)))));
        }
        selected.image = reader.read();
        if (selected.sourceSize.isEmpty())
            selected.sourceSize = selected.image.size();
        const int after = reader.nextImageDelay();
        selected.delayMs = after > 0 ? after : before;
        selected.ok = !selected.image.isNull();
    }
    else
    {
        // Some Qt image handlers implement sequential animation reads but
        // intentionally return false from jumpToImage.  Walk only as far as
        // the requested frame on a fresh reader in that case. Pages that
        // can seek never reach here; this loop is the capped walk.
        if (frameIndex > kMaxSequentialFrameWalk)
        {
            selected.error = "frame index out of range";
            return selected;
        }
        QImageReader sequential(qpath(path));
        sequential.setAutoTransform(false);
        for (int i = 0; i <= frameIndex; ++i)
        {
            if (cooperativeCancelRequested())
            {
                selected.error = "frame decode cancelled";
                return selected;
            }
            QSize candidateSize;
            if (sequence.kind != FrameSequenceKind::Animation)
                candidateSize = sequential.size();
            const int before = sequential.nextImageDelay();
            const int maxDim = std::max(candidateSize.width(), candidateSize.height());
            if (i == frameIndex && sequence.kind != FrameSequenceKind::Animation && maxEdge > 0 &&
                candidateSize.isValid() && maxDim > maxEdge && maxDim > 0)
            {
                const double ratio = static_cast<double>(maxEdge) / maxDim;
                sequential.setScaledSize(QSize(
                    std::max(1, static_cast<int>(std::round(candidateSize.width() * ratio))),
                    std::max(1, static_cast<int>(std::round(candidateSize.height() * ratio)))));
            }
            const QImage candidate = sequential.read();
            if (candidate.isNull())
                break;
            if (i == frameIndex)
            {
                selected.sourceSize = candidateSize.isEmpty() ? candidate.size() : candidateSize;
                selected.image = scaleToMaxEdge(candidate, maxEdge);
                const int after = sequential.nextImageDelay();
                selected.delayMs = after > 0 ? after : before;
                selected.ok = true;
            }
        }
    }

    // Animation failures already name the cause (cancel, rewind, reader).
    // The unused reader opened above has not been read, so its error string
    // must not replace that cause.
    if (!selected.ok && selected.error.empty())
    {
        selected.error = reader.errorString().toStdString();
        if (selected.error.empty())
            selected.error = "image reader cannot select frame";
    }
    return selected;
}

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

FrameDecodeResult decodeImpl(const std::string &path, int frameIndex, int maxEdge,
                             const FrameSequenceInfo *known)
{
    FrameDecodeResult result;
    if (known && known->valid)
    {
        result.sequence = *known;
    }
    else
    {
        const QString suffix = QFileInfo(qpath(path)).suffix().toLower();
        QImageReader probe(qpath(path));
        probe.setAutoTransform(true);
        result.sequence = probeReader(probe, suffix);
        if (!result.sequence.valid)
        {
            result.error = probe.errorString().toStdString();
            if (result.error.empty())
                result.error = "image reader cannot read source";
            return result;
        }
    }

    if (frameIndex < 0 || frameIndex >= result.sequence.frameCount)
    {
        result.error = "frame index out of range";
        return result;
    }

    const SelectedFrame selected = selectFrame(path, result.sequence, frameIndex, maxEdge);
    if (!selected.ok)
    {
        result.error = selected.error;
        return result;
    }

    if (result.sequence.kind == FrameSequenceKind::Animation)
        result.frame = {frameIndex, normalizedAnimationDelayMs(selected.delayMs),
                        selected.sourceSize.width(), selected.sourceSize.height()};
    else
        result.frame = {frameIndex, selected.delayMs, selected.sourceSize.width(),
                        selected.sourceSize.height()};

    result.frame.width =
        selected.sourceSize.width() > 0 ? selected.sourceSize.width() : selected.image.width();
    result.frame.height =
        selected.sourceSize.height() > 0 ? selected.sourceSize.height() : selected.image.height();
    QImageReader metadataReader(qpath(path));
    metadataReader.setAutoTransform(result.sequence.kind != FrameSequenceKind::Animation);
    fillMetadata(path, metadataReader, selected.image, result.sequence, frameIndex,
                 result.frame.durationMs, result.metadata);
    // Detach frames returned by qgif/qwebp before converting their indexed or
    // alpha-backed storage into the core RGB buffer.
    result.pixels = mvcore::fromQImage(selected.image.copy());
    result.identity.fileRevision = MetadataReader::key(path);
    result.identity.frameIndex = frameIndex;
    result.identity.decodeVariant = std::max(0, maxEdge);
    result.ok = !result.pixels.isNull();
    if (!result.ok)
        result.error = "frame conversion failed";
    return result;
}

} // namespace

std::string FrameIdentity::cacheKey() const
{
    return fileRevision + "|frame=" + std::to_string(frameIndex) +
           "|variant=" + std::to_string(decodeVariant);
}

FrameSequenceInfo FrameSequenceReader::probe(const std::string &path)
{
    try
    {
        QImageReader reader(qpath(path));
        reader.setAutoTransform(true);
        return probeReader(reader, QFileInfo(qpath(path)).suffix().toLower());
    }
    catch (...)
    {
        return {};
    }
}

FrameDecodeResult FrameSequenceReader::decode(const std::string &path, int frameIndex, int maxEdge)
{
    try
    {
        return decodeImpl(path, frameIndex, maxEdge, nullptr);
    }
    catch (...)
    {
        FrameDecodeResult result;
        result.error = "unexpected frame decoder failure";
        return result;
    }
}

FrameDecodeResult decodeKnownSequence(const std::string &path, const FrameSequenceInfo &sequence,
                                      int frameIndex, int maxEdge)
{
    try
    {
        if (!sequence.valid)
            return decodeImpl(path, frameIndex, maxEdge, nullptr);
        return decodeImpl(path, frameIndex, maxEdge, &sequence);
    }
    catch (...)
    {
        FrameDecodeResult result;
        result.error = "unexpected frame decoder failure";
        return result;
    }
}

FrameInfo FrameSequenceReader::frameInfo(const std::string &path, int frameIndex)
{
    try
    {
        QImageReader probe(qpath(path));
        probe.setAutoTransform(true);
        const auto sequence = probeReader(probe, QFileInfo(qpath(path)).suffix().toLower());
        if (!sequence.valid || frameIndex < 0 || frameIndex >= sequence.frameCount)
            return {};

        if (sequence.kind != FrameSequenceKind::Animation)
            return stillFrameInfo(path, frameIndex);

        QImageReader reader(qpath(path));
        reader.setAutoTransform(true);

        QSize size;
        int delay = 0;
        if (frameIndex == 0 || reader.jumpToImage(frameIndex))
        {
            const int before = reader.nextImageDelay();
            const QImage image = reader.read();
            if (image.isNull())
                return {};
            size = image.size();
            const int after = reader.nextImageDelay();
            delay = after > 0 ? after : before;
        }
        else
        {
            QImageReader sequential(qpath(path));
            sequential.setAutoTransform(false);
            for (int i = 0; i <= frameIndex; ++i)
            {
                const int before = sequential.nextImageDelay();
                const QImage image = sequential.read();
                if (image.isNull())
                    return {};
                if (i == frameIndex)
                {
                    size = image.size();
                    const int after = sequential.nextImageDelay();
                    delay = after > 0 ? after : before;
                }
            }
        }
        delay = normalizedAnimationDelayMs(delay);
        return {frameIndex, delay, size.width(), size.height()};
    }
    catch (...)
    {
        return {};
    }
}

bool FrameSequenceReader::isSequencePath(const std::string &path)
{
    const auto info = probe(path);
    return info.valid && info.frameCount > 1;
}

void FrameSequenceReader::releaseFile(const std::string &path)
{
    std::shared_ptr<AnimationSession> dropped[kMaxAnimationSessions];
    int droppedCount = 0;
    {
        std::lock_guard<std::mutex> lock(animationMutex());
        auto &cache = animationCache();
        if (path.empty())
        {
            for (int i = 0; i < cache.size; ++i)
            {
                if (cache.slots[i].session)
                    dropped[droppedCount++] = std::move(cache.slots[i].session);
            }
            cache.size = 0;
        }
        else
        {
            const int index = animationSlotIndex(cache, path);
            if (index < 0)
                return;
            if (cache.slots[index].session)
                dropped[droppedCount++] = std::move(cache.slots[index].session);
            for (int i = index + 1; i < cache.size; ++i)
                cache.slots[i - 1] = std::move(cache.slots[i]);
            if (cache.size > 0)
            {
                cache.slots[cache.size - 1].session.reset();
                cache.slots[cache.size - 1].stamp = 0;
                --cache.size;
            }
        }
    }
    for (int i = 0; i < droppedCount; ++i)
    {
        if (dropped[i])
            dropped[i]->abandon.store(true);
    }
}

} // namespace mviewer::core
