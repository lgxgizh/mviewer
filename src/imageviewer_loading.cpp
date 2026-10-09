#include "imageviewer.h"

#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kDisplayWarmMaxEdge = 1024;
constexpr qint64 kDisplayWarmMaxBytes = 64LL * 1024 * 1024;
constexpr qint64 kDisplayWarmLodThresholdPixels = 16LL * 1000 * 1000;
constexpr size_t kDisplayWarmMaxEntries = 2;
} // namespace

void ImageViewer::setBrowseSequence(const QStringList &paths)
{
    if (paths != m_fileList)
    {
        ++m_displayRasterBrowseGeneration;
        cancelDisplayRasterPreloads();
        m_displayRasterWarm.clear();
        m_displayRasterWarmBytes = 0;
    }
    m_fileList = paths;
    m_currentIndex = indexInBrowseSequence(m_currentPath);

    if (m_currentPath.isEmpty())
        return;

    if (m_currentIndex >= 0)
    {
        if (m_lodMode || m_largeSourcePending)
            preloadDisplayRasterNeighbors(m_currentPath);
        else if (m_frame)
            preloadNeighbors(m_currentPath);
    }
    else
    {
        cancelPreloads();
        cancelDisplayRasterPreloads();
    }

    const QSize size = displaySize();
    if (!size.isValid())
        return;
    const QFileInfo info(m_currentPath);
    const QString position =
        m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                            : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName(), QString::number(size.width()),
                            QString::number(size.height()), position));
}

void ImageViewer::showBrowseFullscreen()
{
    setFullscreenRequested(true);
    raise();
    activateWindow();
    // showFullScreen() can preserve the previous gallery focus on some window
    // managers. The browse entry point must finish with the Viewer focused so
    // the first ESC always closes it and viewer-owned keys respond immediately.
    setFocus(Qt::OtherFocusReason);
    QApplication::setActiveWindow(this);
    QTimer::singleShot(0, this,
                       [this]
                       {
                           if (!isVisible())
                               return;
                           raise();
                           activateWindow();
                           setFocus(Qt::OtherFocusReason);
                           QApplication::setActiveWindow(this);
                       });
}

void ImageViewer::setImage(const QString &path)
{
    try
    {
        setImageImpl(path);
    }
    catch (...)
    {
        // An unexpected error must never escape into the Qt event loop; the
        // viewer stays in its current state and the host can retry.
    }
}

void ImageViewer::refreshSource(const QString &path)
{
    if (path.isEmpty() || !browsePathEquals(path, m_currentPath))
        return;
    // The source/cache invalidation is owned by the caller. Re-enter the
    // normal cancellable load path while retaining the user's zoom/pan.
    m_preserveViewOnReload = true;
    setImage(path);
    m_preserveViewOnReload = false;
}

void ImageViewer::renameBrowsePaths(const QStringList &oldPaths, const QStringList &newPaths)
{
    const int count = qMin(oldPaths.size(), newPaths.size());
    for (int i = 0; i < count; ++i)
    {
        const QString &oldPath = oldPaths.at(i);
        const QString &newPath = newPaths.at(i);
        for (QString &path : m_fileList)
            if (browsePathEquals(path, oldPath))
                path = newPath;
        if (browsePathEquals(m_currentPath, oldPath))
        {
            m_currentPath = newPath;
            m_provisionalPath = newPath;
        }
    }
    m_currentIndex = indexInBrowseSequence(m_currentPath);
    if (!m_currentPath.isEmpty())
    {
        const QString position =
            m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                : QString();
        setWindowTitle(
            QString("%1%2 - MViewer").arg(QFileInfo(m_currentPath).fileName(), position));
    }
}

void ImageViewer::setImageImpl(const QString &path)
{
    // Pixel Inspector lifecycle: invalidate synchronously on every new load so
    // the previous image's sample never lingers while the next decode runs —
    // including for empty/failing requests that never deliver a frame.
    clearPixelInfo();
    const QString previousPath = m_currentPath;
    const std::optional<Viewport> reloadView =
        m_preserveViewOnReload ? std::optional<Viewport>(m_view) : std::nullopt;
    const bool keepProvisional = !path.isEmpty() && browsePathEquals(path, m_provisionalPath) &&
                                 !m_provisionalImage.isNull();

    // Window-sized owned snapshot so the switch does not flash black. A
    // full-frame toQImageRef() alias dangles once this frame is released.
    if (!path.isEmpty() && !keepProvisional)
    {
        if (m_lodMode && !m_raster.image.isNull())
        {
            m_transitionImage = m_raster.image;
            m_transitionSourceSize = m_raster.sourceSize;
        }
        else if (m_frame && m_frame->isValid())
        {
            QImage img = mvcore::toQImageRef(m_frame->pixels());
            if (img.isNull())
                img = mvcore::toQImage(m_frame->pixels());
            m_transitionImage = ownedTransitionSnapshot(img);
            m_transitionSourceSize = QSize(m_frame->width(), m_frame->height());
        }
        else if (!m_provisionalImage.isNull())
        {
            m_transitionImage = m_provisionalImage;
            m_transitionSourceSize = m_provisionalSourceSize;
        }
    }
    else
    {
        m_transitionImage = QImage();
        m_transitionSourceSize = QSize();
    }

    m_currentPath = path;
    releaseColdMips(previousPath);
    m_currentIndex = indexInBrowseSequence(path);
    // Drop the prior foreground decode, then promote a neighbor preload for this
    // path. Every other preload is cancelled so superseded work does not run.
    cancelCurrentLoad();
    auto matchingPreload = takeMatchingPreload(path);
    auto matchingDisplayPreload = takeMatchingDisplayRasterPreload(path);
    const uint64_t gen = ++m_requestGen;
    beginImageGeneration();
    m_tileCache.clear();
    m_overlayCache.clear();
    m_frame.reset();
    m_sequence = {};
    m_frameIndex = 0;
    m_playback = {};
    m_tiles = TileGrid();
    m_hasHistogram = false;
    m_loading = !path.isEmpty();
    m_pendingView.reset();
    if (reloadView)
        m_pendingView = reloadView;
    // M47: reset the LOD-first display state for the new request. Any
    // in-flight raster worker is cancelled; its (bounded) completion is
    // discarded by the generation guard. The matching preload (if any) is
    // retained for the raster-path verdict's analysis load decision.
    cancelDisplayRequest();
    m_lodMode = false;
    m_largeSourcePending = false;
    m_analysisLoadIssued = false;
    m_sourceImage.reset();
    m_raster = DisplayRaster{};
    m_pendingAnalysisPreload = std::move(matchingPreload);
    m_promotedDisplayRasterPreload = std::move(matchingDisplayPreload);
    if (!keepProvisional)
    {
        m_provisionalPath.clear();
        m_provisionalImage = QImage();
        m_provisionalSourceSize = QSize();
    }
    if (context())
    {
        makeCurrent();
        m_gpu.clear();
        doneCurrent();
    }
    if (path.isEmpty())
    {
        m_pendingAnalysisPreload.reset();
        update();
        return;
    }
    // Close suppresses raster work so teardown cannot queue another decode.
    // The same viewer is shown again after Esc, so the next image must load.
    setProperty("mviewerClosing", false);
    // M47: the raster-path verdict decides the full-frame load. The probe
    // worker reports small-vs-large; small sources keep the existing fast
    // path, large feasible sources get the analysis-support full load, and
    // infeasible sources (> Qt's allocation limit) skip it until Phase 4's
    // explicit source materialization. The display never waits for it.
    startLodDisplay(path, gen);
}

// M47: issue the analysis-support full-frame load (promoting a matching
// neighbor preload when one was retained). Runs on the UI thread from the
// raster-path verdict; the load itself is async + cancellable + lifetime-safe
// exactly as the pre-M47 foreground load was.
void ImageViewer::issueAnalysisLoad(const QString &path, uint64_t generation)
{
    auto onLoaded = makeImageLoadCallback(path, generation);
    // Frame zero is an explicit request. Static-image preloads use the same
    // frame-aware cache key, but a stale promoted legacy handle must never
    // erase the requested frame identity.
    m_pendingAnalysisPreload.reset();
    m_foregroundRequest = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        path.toUtf8().toStdString(), 0, std::move(onLoaded), ImageRepository::kDefaultLoadOptions,
        m_lifetime);
}

ImageViewer::ImageLoadCallback ImageViewer::makeImageLoadCallback(const QString &path,
                                                                  uint64_t generation)
{
    // M46 strict lifetime contract: the completion lambda captures NO raw
    // `this` — only the QPointer guard (which is dereferenced exclusively on
    // the UI thread inside queueLoadedImage/queueImageLoadFailure), the path,
    // the generation and the shared lifetime token. The repository itself
    // already refuses to invoke this callback once the token is dead, so a
    // late completion is a no-op before any viewer-visible code runs.
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    return [path, generation, guard](const ImageLoadResult &result)
    {
        if (!guard)
            return;
        if (result.success())
            queueLoadedImage(path, generation, guard, result);
        else
            queueImageLoadFailure(path, generation, guard);
    };
}

void ImageViewer::queueImageLoadFailure(const QString &path, uint64_t generation,
                                        const ImageLoadGuard &guard)
{
    QMetaObject::invokeMethod(
        qApp,
        [path, generation, guard]()
        {
            ImageViewer *viewer = guard->data();
            if (!viewer || path != viewer->m_currentPath || generation != viewer->m_requestGen)
                return;
            viewer->m_foregroundRequest.reset();
            viewer->m_loading = false;
            viewer->m_hasHistogram = false;
            viewer->m_transitionImage = QImage();
            viewer->m_transitionSourceSize = QSize();
            // M47: while the raster-path verdict for a
            // large source is still pending, suppress the
            // analysis-support full-frame failure (the
            // 100 MP full decode fails fast, the LOD
            // arrives later — the display owns the
            // verdict).
            if (viewer->m_largeSourcePending)
            {
                viewer->update();
                return;
            }
            // M47: in LOD-first display the image IS on
            // screen (the raster path); only the
            // analysis-support full frame failed (e.g.
            // > Qt's allocation limit). Do not clobber
            // the successful display with a failure.
            if (viewer->m_lodMode && !viewer->m_raster.image.isNull())
            {
                viewer->update();
                return;
            }
            viewer->presentLoadFailure();
        });
}

void ImageViewer::queueLoadedImage(const QString &path, uint64_t generation,
                                   const ImageLoadGuard &guard, const ImageLoadResult &result)
{
    QMetaObject::invokeMethod(qApp,
                              [path, generation, guard, result]()
                              {
                                  ImageViewer *viewer = guard->data();
                                  if (!viewer || path != viewer->m_currentPath ||
                                      generation != viewer->m_requestGen)
                                      return;
                                  viewer->applyLoadedImage(path, result);
                                  viewer->scheduleLoadedRefit(path, generation, guard);
                              });
}

void ImageViewer::applyLoadedImage(const QString &path, const ImageLoadResult &result)
{
    m_foregroundRequest.reset();
    m_loading = false;
    m_transitionImage = QImage();
    m_transitionSourceSize = QSize();
    m_frame = result.frame;
    if (!m_frame || m_frame->pixels().isNull())
    {
        m_hasHistogram = false;
        // M47: a failing analysis-support full frame must not clobber a
        // successful LOD-first display (same guard as queueImageLoadFailure).
        if (m_lodMode && !m_raster.image.isNull())
        {
            update();
            return;
        }
        presentLoadFailure();
        return;
    }

    m_sequence = m_frame->sequenceInfo();
    m_frameIndex = m_frame->frameIndex();
    m_playback.configure(m_sequence);
    m_playback.setFrameInfo(
        {m_frameIndex,
         m_frame->metadata().frameDurationMs > 0 ? m_frame->metadata().frameDurationMs : 100,
         m_frame->width(), m_frame->height()});

    computeHistogram();
    const QFileInfo info(path);
    m_currentIndex = indexInBrowseSequence(path);
    m_tiles = TileGrid(m_frame->width(), m_frame->height(), 256);
    // M47: in LOD-first display the full frame is the analysis/Inspector
    // source only — the display keeps the bounded raster (no re-fit that would
    // drop the user's zoom, no UI-thread scaling of the full frame).
    if (m_lodMode)
    {
        preloadDisplayRasterNeighbors(path);
        update();
        emit imageReady(m_frame);
        return;
    }
    m_view.screenW = width();
    m_view.screenH = height();
    if (m_lockZoom && !m_fitMode && m_view.scale > 0.0)
    {
        const int sw = m_frame->width();
        const int sh = m_frame->height();
        m_view.offsetX = (m_view.screenW - sw * m_view.scale) / 2.0;
        m_view.offsetY = (m_view.screenH - sh * m_view.scale) / 2.0;
        advanceViewportRevision();
    }
    else
    {
        const FitPolicy fitPolicy = property("mviewerFullscreenRequested").toBool()
                                        ? FitPolicy::MaximizeClient
                                        : FitPolicy::Comfortable;
        m_view.fit(m_frame->width(), m_frame->height(), fitPolicy);
        m_fitMode = true;
    }
    const QString position =
        m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                            : QString();
    setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                       .arg(info.fileName())
                       .arg(m_frame->width())
                       .arg(m_frame->height())
                       .arg(position));
    if (m_sequence.animated)
        play();
    else
        updateFramePresentationStatus();
    applyPendingView();
    emitZoom();
    m_overlayCache.clear();
    clearLoadedGpu();
    preloadNeighbors(path);
    update();
    emit imageReady(m_frame);
}

void ImageViewer::applyPendingView()
{
    if (!m_pendingView)
        return;
    // A restored view transform is untrusted input (QSettings is user-editable):
    // a zero, negative or NaN scale divides to a degenerate transform and paints
    // nothing. Clamp to the range the interactive zoom path already enforces.
    constexpr double kMinRestoredScale = 0.05;
    constexpr double kMaxRestoredScale = 50.0;
    double restoredScale = m_pendingView->scale;
    if (!std::isfinite(restoredScale))
        restoredScale = 1.0;
    m_view.scale = std::clamp(restoredScale, kMinRestoredScale, kMaxRestoredScale);
    if (std::fabs(m_view.screenW - m_pendingView->screenW) < 2.0 &&
        std::fabs(m_view.screenH - m_pendingView->screenH) < 2.0)
    {
        m_view.offsetX = std::isfinite(m_pendingView->offsetX) ? m_pendingView->offsetX : 0.0;
        m_view.offsetY = std::isfinite(m_pendingView->offsetY) ? m_pendingView->offsetY : 0.0;
    }
    m_pendingView.reset();
    m_fitMode = false;
    emitZoom();
}

void ImageViewer::clearLoadedGpu()
{
    if (!context())
        return;
    makeCurrent();
    m_gpu.clear();
    doneCurrent();
}

void ImageViewer::scheduleLoadedRefit(const QString &path, uint64_t generation,
                                      const ImageLoadGuard &guard)
{
    QTimer::singleShot(
        0, this,
        [guard, path, generation]()
        {
            ImageViewer *viewer = guard->data();
            if (!viewer || viewer->m_currentPath != path || viewer->m_requestGen != generation)
                return;
            if (viewer->property("mviewerFullscreenRequested").toBool() && viewer->m_fitMode)
                viewer->fitToWidget();
            viewer->update();
            // Fit can move the pixel under a stationary cursor after setImage.
            viewer->resamplePixelUnderCursor();
        });
}

void ImageViewer::setViewTransform(const Viewport &v)
{
    // Store until the async load of the current image completes; the callback
    // in setImage() applies it once screen geometry is known.
    m_pendingView = v;
}

void ImageViewer::preloadNeighbors(const QString &path)
{
    if (m_currentIndex < 0)
        return;
    cancelPreloads();
    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size() || browsePathEquals(m_fileList.at(i), path))
            continue;
        auto h = mviewer::application::ImageLoadingService::instance().preloadAsync(
            m_fileList[i].toUtf8().toStdString(), m_lifetime);
        if (h)
            m_neighborPreloads.push_back({m_fileList[i], std::move(h)});
    }
}

void ImageViewer::preloadDisplayRasterNeighbors(const QString &path)
{
    if (m_currentIndex < 0 || path.isEmpty())
        return;
    cancelDisplayRasterPreloads();
    for (int delta = -1; delta <= 1; ++delta)
    {
        const int i = m_currentIndex + delta;
        if (i < 0 || i >= m_fileList.size() || browsePathEquals(m_fileList.at(i), path))
            continue;
        const QString neighbor = m_fileList[i];
        if (std::any_of(m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
                        [&](const DisplayRasterWarm &w) { return w.path == neighbor; }))
            continue;

        auto state = std::make_shared<DisplayRasterPreloadState>();
        auto guard = std::make_shared<QPointer<ImageViewer>>(this);
        const uint64_t browseGen = m_displayRasterBrowseGeneration;
        const auto target = m_displayColorTarget;
        auto handle = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Thumbnail,
            [neighbor, browseGen, state, guard, target](const TaskScheduler::TaskContext &ctx)
            {
                try
                {
                    runDisplayRasterPreload(neighbor, browseGen, state, target, ctx, guard);
                }
                catch (...)
                {
                    if (!ctx.isCancelled())
                    {
                        DisplayRasterPreloadResult res;
                        res.path = neighbor; res.browseGeneration = browseGen;
                        res.state = state; res.target = target; res.failed = true;
                        queueDisplayRasterPreloadResult(guard, std::move(res));
                    }
                }
            },
            {}, std::chrono::steady_clock::time_point::max(), [] {});
        if (handle)
            m_displayRasterPreloads.push_back({neighbor, std::move(state), std::move(handle)});
    }
}

void ImageViewer::cancelCurrentLoad()
{
    cancelFrameRequests();
    mviewer::application::ImageLoadingService::instance().cancelAsync(m_foregroundRequest);
}

void ImageViewer::cancelPreloads()
{
    for (auto &p : m_neighborPreloads)
        mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    m_neighborPreloads.clear();
}

void ImageViewer::cancelDisplayRasterPreloads()
{
    for (auto &preload : m_displayRasterPreloads)
        TaskScheduler::cancel(preload.handle);
    m_displayRasterPreloads.clear();
}

mviewer::application::ImageLoadingService::AsyncRequestHandle
ImageViewer::takeMatchingPreload(const QString &path)
{
    mviewer::application::ImageLoadingService::AsyncRequestHandle match;
    for (auto &p : m_neighborPreloads)
    {
        if (!match && browsePathEquals(p.path, path))
            match = std::move(p.handle);
        else mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    }
    m_neighborPreloads.clear();
    return match;
}

ImageViewer::DisplayRasterPreload ImageViewer::takeMatchingDisplayRasterPreload(const QString &path)
{
    DisplayRasterPreload match;
    for (auto &preload : m_displayRasterPreloads)
    {
        if (!match.handle && browsePathEquals(preload.path, path))
            match = std::move(preload);
        else TaskScheduler::cancel(preload.handle);
    }
    m_displayRasterPreloads.clear();
    return match;
}

std::optional<ImageViewer::DisplayRasterWarm>
ImageViewer::takeWarmDisplayRaster(const QString &path)
{
    const auto it = std::find_if(m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
                                 [&](const DisplayRasterWarm &warm)
                                 { return browsePathEquals(warm.path, path); });
    if (it == m_displayRasterWarm.end())
        return std::nullopt;
    DisplayRasterWarm warm = std::move(*it);
    m_displayRasterWarmBytes = std::max<qint64>(0, m_displayRasterWarmBytes - warm.bytes);
    m_displayRasterWarm.erase(it);
    ++m_displayRasterWarmHits;
    return warm;
}

bool ImageViewer::takeWarmDisplayForCompare(const QString &path, QImage *image, QSize *sourceSize,
                                            QRect *sourceRect)
{
    auto warm = takeWarmDisplayRaster(path);
    if (!warm || warm->image.isNull())
        return false;
    const QSize size = warm->sourceSize.isValid() ? warm->sourceSize : warm->image.size();
    if (image) *image = warm->image;
    if (sourceSize) *sourceSize = size;
    if (sourceRect) *sourceRect = warm->sourceRect.isValid() ? warm->sourceRect : QRect(QPoint(0, 0), size);
    return true;
}

void ImageViewer::enforceDisplayRasterWarmBudget()
{
    while (m_displayRasterWarm.size() > kDisplayWarmMaxEntries ||
           m_displayRasterWarmBytes > kDisplayWarmMaxBytes)
    {
        if (m_displayRasterWarm.empty())
            break;
        const auto oldest =
            std::min_element(m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
                             [](const DisplayRasterWarm &a, const DisplayRasterWarm &b)
                             { return a.lastUse < b.lastUse; });
        m_displayRasterWarmBytes = std::max<qint64>(0, m_displayRasterWarmBytes - oldest->bytes);
        m_displayRasterWarm.erase(oldest);
    }
}

void ImageViewer::storeWarmDisplayRaster(DisplayRasterPreloadResult result)
{
    if (result.image.isNull() || !result.source || result.path.isEmpty())
        return;
    const qint64 bytes = static_cast<qint64>(result.image.sizeInBytes());
    if (bytes <= 0 || bytes > kDisplayWarmMaxBytes)
        return;

    const auto old =
        std::find_if(m_displayRasterWarm.begin(), m_displayRasterWarm.end(),
                     [&](const DisplayRasterWarm &warm) { return warm.path == result.path; });
    if (old != m_displayRasterWarm.end())
    {
        m_displayRasterWarmBytes = std::max<qint64>(0, m_displayRasterWarmBytes - old->bytes);
        m_displayRasterWarm.erase(old);
    }
    m_displayRasterWarm.push_back({result.path, std::move(result.source), std::move(result.image),
                                   result.sourceRect, result.sourceSize, result.density,
                                   result.target, bytes, ++m_displayRasterWarmClock});
    m_displayRasterWarmBytes += bytes;
    enforceDisplayRasterWarmBudget();
}

void ImageViewer::runDisplayRasterPreload(const QString &path, uint64_t browseGeneration,
                                          const std::shared_ptr<DisplayRasterPreloadState> &state,
                                          const mviewer::core::DisplayColorContext &target,
                                          const TaskScheduler::TaskContext &ctx,
                                          const std::shared_ptr<QPointer<ImageViewer>> &guard)
{
    if (ctx.isCancelled())
        return;
    DisplayRasterPreloadResult result;
    result.path = path;
    result.browseGeneration = browseGeneration;
    result.state = state;
    result.target = target;
    try
    {
        result.source = mviewer::core::SourceImage::open(path.toStdString());
        if (!result.source)
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        result.sourceSize =
            QSize(result.source->metadata().width, result.source->metadata().height);
        const qint64 pixels =
            static_cast<qint64>(result.sourceSize.width()) * result.sourceSize.height();
        if (pixels <= kDisplayWarmLodThresholdPixels)
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        const auto raster = result.source->decodeLod(kDisplayWarmMaxEdge);
        if (!raster.ok || raster.pixels.isNull())
        {
            result.failed = true;
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        result.sourceRect = QRect(0, 0, result.sourceSize.width(), result.sourceSize.height());
        result.density = static_cast<double>(result.sourceSize.width()) / raster.pixels.width;
        result.image = mvcore::toDisplayQImage(raster.pixels, raster.metadata, target);
    }
    catch (...)
    {
        result.failed = true;
    }
    if (ctx.isCancelled())
        return;
    queueDisplayRasterPreloadResult(guard, std::move(result));
}

void ImageViewer::queueDisplayRasterPreloadResult(
    const std::shared_ptr<QPointer<ImageViewer>> &guard, DisplayRasterPreloadResult result)
{
    if (!qApp)
        return;
    QMetaObject::invokeMethod(
        qApp,
        [guard, res = std::move(result)]() mutable
        {
            if (guard && *guard)
                (*guard)->applyDisplayRasterPreloadResult(std::move(res));
        },
        Qt::QueuedConnection);
}

void ImageViewer::applyDisplayRasterPreloadResult(DisplayRasterPreloadResult result)
{
    if (result.state)
    {
        m_displayRasterPreloads.erase(std::remove_if(m_displayRasterPreloads.begin(),
                                                     m_displayRasterPreloads.end(),
                                                     [&](const DisplayRasterPreload &preload)
                                                     { return preload.state == result.state; }),
                                      m_displayRasterPreloads.end());
    }
    const uint64_t promotedGeneration =
        result.state ? result.state->promotedGeneration.load(std::memory_order_acquire) : 0;
    if (promotedGeneration != 0)
    {
        if (!browsePathEquals(result.path, m_currentPath) || promotedGeneration != m_requestGen ||
            result.target.cacheKey() != m_displayColorTarget.cacheKey())
            return;
        m_displayRequest.reset();
        m_promotedDisplayRasterPreload = DisplayRasterPreload{};
        if (result.failed || result.image.isNull())
        {
            // The neighbor result is best effort. Re-enter the normal probe
            // path if it was promoted before the worker discovered a failure.
            startLodDisplay(result.path, promotedGeneration);
            return;
        }
        applyDisplayRaster(result.path, promotedGeneration, std::move(result.source),
                           std::move(result.image), result.sourceRect, result.sourceSize,
                           result.density, result.target, false);
        return;
    }

    if (result.browseGeneration != m_displayRasterBrowseGeneration ||
        result.target.cacheKey() != m_displayColorTarget.cacheKey() || result.failed ||
        result.image.isNull())
        return;
    const bool stillListed = indexInBrowseSequence(result.path) >= 0;
    if (stillListed)
        storeWarmDisplayRaster(std::move(result));
}
