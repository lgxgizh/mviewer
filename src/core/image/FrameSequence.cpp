#include "core/image/FrameSequence.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"

#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <atomic>
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
// Frames decoded to reach one requested animation frame. Real animations stay
// far below this (60 s at 60 fps is 3600 frames); the bound keeps a session
// file with a wild frame index from decoding for minutes.
constexpr int kMaxSequentialFrameWalk = 4096;

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
    qtmetadata::applyReaderRasterMetadata(reader, image, meta);
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
    if (sequence.animated && sequence.countKnown && sequence.frameCount <= kMaxTimingProbeFrames)
    {
        // qgif/qwebp expose the first frame through read(), not
        // jumpToImage(0).  Probe on a fresh reader so the caller's reader
        // remains positioned for the subsequent decode.
        QImageReader timing(reader.fileName());
        timing.setAutoTransform(true);
        int64_t total = 0;
        for (int i = 0; i < sequence.frameCount; ++i)
        {
            const int before = timing.nextImageDelay();
            const QImage image = timing.read();
            if (image.isNull())
                break;
            const int after = timing.nextImageDelay();
            const int delay = after > 0 ? after : before;
            total += normalizedAnimationDelayMs(delay);
        }
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

std::mutex g_animationMutex;
std::shared_ptr<AnimationSession> g_animationSession;

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

std::shared_ptr<AnimationSession> prepareAnimationSession(const std::string &path, int frameIndex)
{
    const QFileInfo info(qpath(path));
    std::lock_guard<std::mutex> lock(g_animationMutex);
    const bool keep = g_animationSession && !g_animationSession->abandon.load() &&
                      animationFileMatches(*g_animationSession, path, info) &&
                      !animationNeedsRewind(*g_animationSession, frameIndex);
    if (!keep)
    {
        if (g_animationSession)
            g_animationSession->abandon.store(true);
        g_animationSession = openAnimationSession(path, info);
    }
    return g_animationSession;
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
        if (session.abandon.load())
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
    // The index can come from a restored session file, so bound it before it
    // becomes a loop count (the sequential fallbacks below decode up to it).
    if (frameIndex < 0 || frameIndex > kMaxSequentialFrameWalk)
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
        // the requested frame on a fresh reader in that case.
        QImageReader sequential(qpath(path));
        sequential.setAutoTransform(false);
        for (int i = 0; i <= frameIndex; ++i)
        {
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

FrameDecodeResult decodeImpl(const std::string &path, int frameIndex, int maxEdge)
{
    FrameDecodeResult result;
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
        return decodeImpl(path, frameIndex, maxEdge);
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

        QImageReader reader(qpath(path));
        reader.setAutoTransform(true);

        QSize size;
        int delay = 0;
        const bool animation = sequence.kind == FrameSequenceKind::Animation;
        if (frameIndex == 0 || reader.jumpToImage(frameIndex))
        {
            if (!animation)
                size = reader.size();
            const int before = reader.nextImageDelay();
            const QImage image = reader.read();
            if (image.isNull())
                return {};
            if (size.isEmpty())
                size = image.size();
            const int after = reader.nextImageDelay();
            delay = after > 0 ? after : before;
            if (size.isEmpty())
                size = image.size();
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
        if (sequence.kind != FrameSequenceKind::Animation)
            delay = 0;
        else
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
    std::shared_ptr<AnimationSession> current;
    {
        std::lock_guard<std::mutex> lock(g_animationMutex);
        if (!g_animationSession)
            return;
        if (!path.empty() && !animationPathMatches(*g_animationSession, path))
            return;
        current = std::move(g_animationSession);
    }
    if (current)
        current->abandon.store(true);
}

} // namespace mviewer::core
