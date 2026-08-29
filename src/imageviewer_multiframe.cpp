#include "imageviewer.h"

#include "application/ImageLoadingService.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace
{

constexpr int kPlaybackTimerIntervalMs = 10;
constexpr int kPrefetchCount = 2;
constexpr int kPrefetchMaxEdge = 512;

QString sequenceLabel(const mviewer::core::FrameSequenceInfo &sequence)
{
    return sequence.kind == mviewer::core::FrameSequenceKind::Pages ? QStringLiteral("Page")
                                                                      : QStringLiteral("Frame");
}

} // namespace

void ImageViewer::ensurePlaybackTimer()
{
    if (m_playbackTimer)
        return;
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setTimerType(Qt::PreciseTimer);
    m_playbackTimer->setInterval(kPlaybackTimerIntervalMs);
    connect(m_playbackTimer, &QTimer::timeout, this, &ImageViewer::onPlaybackTick);
}

void ImageViewer::play()
{
    if (!m_sequence.animated || m_sequence.frameCount <= 1)
        return;
    ensurePlaybackTimer();
    if (!m_playback.playing())
    {
        m_playback.start();
        emit playbackStateChanged(true);
    }
    m_playbackTimer->start();
    updateFramePresentationStatus();
    emit frameChanged(m_frameIndex, frameCount(), true);
}

void ImageViewer::pause()
{
    if (!m_playback.playing())
        return;
    m_playback.pause();
    if (m_playbackTimer)
        m_playbackTimer->stop();
    emit playbackStateChanged(false);
    updateFramePresentationStatus();
    emit frameChanged(m_frameIndex, frameCount(), false);
}

void ImageViewer::restart()
{
    if (frameCount() <= 1)
        return;
    const bool shouldPlay = m_sequence.animated;
    m_playback.setCurrentFrame(0);
    setFrameIndex(0);
    if (shouldPlay)
        play();
    else
        pause();
}

void ImageViewer::previousFrame()
{
    if (frameCount() <= 1)
        return;
    const int next = m_sequence.animated
                         ? (m_frameIndex + frameCount() - 1) % frameCount()
                         : std::max(0, m_frameIndex - 1);
    setFrameIndex(next);
}

void ImageViewer::nextFrame()
{
    if (frameCount() <= 1)
        return;
    const int next = m_sequence.animated ? (m_frameIndex + 1) % frameCount()
                                         : std::min(frameCount() - 1, m_frameIndex + 1);
    setFrameIndex(next);
}

void ImageViewer::setFrameIndex(int index)
{
    if (frameCount() <= 1 || m_currentPath.isEmpty())
        return;
    const int target = std::clamp(index, 0, frameCount() - 1);
    m_playback.setCurrentFrame(target);
    requestFrame(target);
}

void ImageViewer::requestFrame(int index)
{
    if (m_currentPath.isEmpty() || frameCount() <= 1)
        return;
    const int target = std::clamp(index, 0, frameCount() - 1);
    if (target == m_frameIndex && m_requestedFrame < 0)
    {
        updateFramePresentationStatus();
        emit frameChanged(m_frameIndex, frameCount(), isPlaying());
        return;
    }

    mviewer::application::ImageLoadingService::instance().cancelAsync(m_frameRequest);
    const uint64_t generation = ++m_frameGeneration;
    m_requestedFrame = target;
    const QString path = m_currentPath;
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    ImageLoadOptions options;
    options.useDiskCache = true;
    options.generateHistogram = true;
    options.frameMaxEdge = 0;
    m_frameRequest = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        path.toUtf8().toStdString(), target,
        [guard, path, target, generation](const ImageLoadResult &result)
        {
            if (!qApp || !guard)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, path, target, generation, result]()
                {
                    ImageViewer *viewer = guard->data();
                    if (!viewer || viewer->m_currentPath != path ||
                        viewer->m_frameGeneration != generation)
                        return;
                    viewer->applyLoadedFrame(result, target, generation);
                },
                Qt::QueuedConnection);
        },
        options, m_lifetime);
}

void ImageViewer::applyLoadedFrame(const ImageLoadResult &result, int requestedFrame,
                                   uint64_t generation)
{
    if (generation != m_frameGeneration || requestedFrame != m_requestedFrame)
        return;
    m_frameRequest.reset();
    m_requestedFrame = -1;
    if (!result.success() || !result.frame || result.frame->pixels().isNull())
    {
        if (m_playback.playing())
            pause();
        setWindowTitle(QStringLiteral("%1 %2 %3失败 - %4 - MViewer")
                           .arg(sequenceLabel(m_sequence))
                           .arg(requestedFrame + 1)
                           .arg(sequenceLabel(m_sequence) == QStringLiteral("Page")
                                    ? QStringLiteral("页")
                                    : QStringLiteral("帧"))
                           .arg(QFileInfo(m_currentPath).fileName()));
        emit loadFailed(m_currentPath);
        return;
    }

    // A frame/page change is an image-generation boundary for every derived
    // surface. Cancel old tile/overlay/ROI work before publishing the new
    // frame; the viewport itself deliberately remains unchanged.
    beginImageGeneration();
    m_frame = result.frame;
    m_sequence = m_frame->sequenceInfo();
    m_frameIndex = m_frame->frameIndex();
    m_playback.setFrameInfo({m_frameIndex, m_frame->metadata().frameDurationMs > 0
                                                  ? m_frame->metadata().frameDurationMs
                                                  : 100,
                             m_frame->width(), m_frame->height()});
    computeHistogram();
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    m_overlayCache.clear();
    clearLoadedGpu();
    updateFramePresentationStatus();
    update();
    emit imageReady(m_frame);
    emit frameChanged(m_frameIndex, frameCount(), isPlaying());
    prefetchFrames(m_frameIndex);
}

void ImageViewer::onPlaybackTick()
{
    if (!m_playback.playing())
        return;
    const auto decision = m_playback.tick();
    if (decision.due)
        requestFrame(decision.frameIndex);
}

void ImageViewer::cancelFrameRequests()
{
    ++m_frameGeneration;
    m_requestedFrame = -1;
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_frameRequest);
    for (auto &request : m_framePrefetchRequests)
        mviewer::application::ImageLoadingService::instance().cancelAsync(request);
    m_framePrefetchRequests.clear();
    if (m_playbackTimer)
        m_playbackTimer->stop();
}

void ImageViewer::prefetchFrames(int currentIndex)
{
    for (auto &request : m_framePrefetchRequests)
        mviewer::application::ImageLoadingService::instance().cancelAsync(request);
    m_framePrefetchRequests.clear();
    if (m_currentPath.isEmpty() || frameCount() <= 1)
        return;

    ImageLoadOptions options;
    options.useDiskCache = true;
    options.generateHistogram = false;
    options.frameMaxEdge = kPrefetchMaxEdge;
    const QString path = m_currentPath;
    for (int offset = 1; offset <= kPrefetchCount; ++offset)
    {
        int target = currentIndex + offset;
        if (m_sequence.animated)
            target %= frameCount();
        else if (target >= frameCount())
            break;
        if (target == m_requestedFrame)
            continue;
        auto request = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
            path.toUtf8().toStdString(), target, [](const ImageLoadResult &) {}, options,
            m_lifetime);
        if (request)
            m_framePrefetchRequests.push_back(std::move(request));
    }
}

void ImageViewer::updateFramePresentationStatus()
{
    if (!isMultiFrame() || m_currentPath.isEmpty())
    {
        m_frameStatusText.clear();
        setAccessibleDescription({});
        return;
    }
    const QFileInfo info(m_currentPath);
    const QSize size = displaySize();
    const QString position = m_currentIndex >= 0
                                 ? QStringLiteral(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                 : QString();
    const QString state = isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
    m_frameStatusText = QStringLiteral("%1 %2/%3")
                            .arg(sequenceLabel(m_sequence))
                            .arg(m_frameIndex + 1)
                            .arg(frameCount());
    if (m_sequence.animated)
        m_frameStatusText += QStringLiteral(" · %1").arg(state);
    const QString shortcuts =
        m_sequence.animated
            ? QStringLiteral("Space: Play/Pause; comma/period: Previous/Next frame")
            : QStringLiteral("Comma/period: Previous/Next page");
    setAccessibleDescription(QStringLiteral("%1. %2").arg(m_frameStatusText, shortcuts));
    setWindowTitle(QStringLiteral("%1 (%2x%3)%4 · %5 - MViewer")
                       .arg(info.fileName())
                       .arg(size.width())
                       .arg(size.height())
                       .arg(position)
                       .arg(m_frameStatusText));
}

bool ImageViewer::handleFrameKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers != Qt::NoModifier && modifiers != Qt::KeypadModifier)
        return false;
    if (key == Qt::Key_Comma)
        previousFrame();
    else if (key == Qt::Key_Period)
        nextFrame();
    else if (key == Qt::Key_Space && m_sequence.animated)
    {
        if (isPlaying())
            pause();
        else
            play();
    }
    else
        return false;
    return true;
}
