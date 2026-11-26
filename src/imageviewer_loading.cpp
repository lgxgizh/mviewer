#include "imageviewer.h"
#include "imageviewer_displayfit.h"
#include "imageviewer_queuedecode.h"

#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{
// Fallback LOD edge when the widget is unknown or the view is not zoomed out.
constexpr int kDisplayWarmMaxEdge = 1024;

void dropAnalysisPreload(mviewer::application::ImageLoadingService::AsyncRequestHandle &pending)
{
    // Dropping the shared_ptr alone leaves the request running (the worker holds
    // its own reference), so a discarded preload used to decode for nothing.
    if (pending)
        mviewer::application::ImageLoadingService::instance().cancelAsync(pending);
}

constexpr qint64 kDisplayWarmMaxBytes = 64LL * 1024 * 1024;
constexpr qint64 kFullPreloadMaxPixels = 48LL * 1000 * 1000;
constexpr size_t kDisplayWarmMaxEntries = 2;
} // namespace

void ImageViewer::setImage(const QString &path)
{
    try
    {
        setImageImpl(path);
    }
    catch (...)
    {
        // Keep a load error inside this function; the viewer stays as it is.
    }
}

void ImageViewer::refreshSource(const QString &path)
{
    if (path.isEmpty() || !browsePathEquals(path, m_currentPath))
        return;
    // Re-enter the cancellable load and keep the user's zoom and pan.
    m_preserveViewOnReload = true;
    setImage(path);
    m_preserveViewOnReload = false;
}

void ImageViewer::scheduleStandInExpiry(uint64_t gen)
{
    // Drop a cross-path stand-in that outlives kStandInGraceMs.
    QTimer::singleShot(kStandInGraceMs, this,
                       [this, gen]()
                       {
                           if (gen != m_requestGen || !m_loading || hasDisplayImage() ||
                               !m_provisionalImage.isNull() || m_transitionImage.isNull())
                               return;
                           m_transitionImage = QImage();
                           m_transitionSourceSize = QSize();
                           update();
                       });
}

void ImageViewer::setImageImpl(const QString &path)
{
    // Drop the previous pixel sample before the next load, including failures.
    clearPixelInfo();
    resetSelectionForNewImage(path);
    const QString previousPath = m_currentPath;
    const std::optional<Viewport> reloadView =
        m_preserveViewOnReload ? std::optional<Viewport>(m_view) : std::nullopt;
    const bool keepProvisional = !path.isEmpty() && browsePathEquals(path, m_provisionalPath) &&
                                 !m_provisionalImage.isNull();

    prepareTransitionImage(path, keepProvisional);

    // Cross-path stand-in: the previous photo may bridge only a brief gap.
    const bool crossPathStandIn =
        !m_transitionImage.isNull() && !browsePathEquals(path, previousPath);
    m_currentPath = path;
    releaseColdMips(previousPath);
    m_currentIndex = indexInBrowseSequence(path);
    // Drop the prior decode and promote this path's neighbor preload.
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
    if (crossPathStandIn)
        scheduleStandInExpiry(gen);
    m_pendingView.reset();
    if (reloadView)
        m_pendingView = reloadView;
    // Reset LOD state. A matching preload is kept for the analysis decision.
    cancelDisplayRequest();
    m_lodMode = false;
    m_largeSourcePending = false;
    m_analysisLoadIssued = false;
    setProperty("mviewerAnalysisWanted", false);
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
        m_gpu.clear();
    if (path.isEmpty())
    {
        dropAnalysisPreload(m_pendingAnalysisPreload);
        update();
        return;
    }
    // Esc clears the closing flag so the next image can load.
    setProperty("mviewerClosing", false);
    // The raster verdict owns the full-frame decision.
    startLodDisplay(path, gen);
}

void ImageViewer::issueAnalysisLoad(const QString &path, uint64_t generation)
{
    auto onLoaded = makeImageLoadCallback(path, generation);
    // Frame 0 is explicit. Do not let a stale preload erase that frame id.
    dropAnalysisPreload(m_pendingAnalysisPreload);
    m_foregroundRequest = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        path.toUtf8().toStdString(), 0, std::move(onLoaded), ImageRepository::kDefaultLoadOptions,
        m_lifetime);
}

ImageViewer::ImageLoadCallback ImageViewer::makeImageLoadCallback(const QString &path,
                                                                  uint64_t generation)
{
    // No raw this. A dead lifetime token never reaches viewer-visible code.
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
    if (!qApp)
        return; // teardown: no event loop left to deliver into
    QMetaObject::invokeMethod(qApp,
                              [path, generation, guard]()
                              {
                                  ImageViewer *viewer = guard->data();
                                  if (!viewer || path != viewer->m_currentPath ||
                                      generation != viewer->m_requestGen)
                                      return;
                                  viewer->m_foregroundRequest.reset();
                                  viewer->m_loading = false;
                                  viewer->m_hasHistogram = false;
                                  viewer->m_transitionImage = QImage();
                                  viewer->m_transitionSourceSize = QSize();
                                  // The raster path still owns a pending large-source verdict.
                                  if (viewer->m_largeSourcePending)
                                  {
                                      viewer->update();
                                      return;
                                  }
                                  // Keep an on-screen LOD raster if the analysis frame failed.
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
    if (!qApp)
        return; // teardown: no event loop left to deliver into
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
    const bool fullNow =
        m_frame && m_frame->metadata().rawSource == mviewer::domain::RawSource::FullDemosaic;
    const bool fullIn = result.frame && result.frame->metadata().rawSource ==
                                            mviewer::domain::RawSource::FullDemosaic;
    if (m_fullRawLoading || (fullNow && !fullIn))
    {
        m_foregroundRequest.reset();
        return;
    }
    m_foregroundRequest.reset();
    m_loading = false;
    m_transitionImage = QImage();
    m_transitionSourceSize = QSize();
    m_frame = result.frame;
    if (!m_frame || m_frame->pixels().isNull())
    {
        m_hasHistogram = false;
        // A failed analysis frame must not replace an LOD raster.
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
    // LOD display keeps its raster; the full frame is analysis-only.
    if (m_lodMode)
    {
        preloadDisplayRasterNeighbors(path);
        update();
        emit imageReady(m_frame);
        recommitLockedSelection();
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
    recommitLockedSelection();
}

void ImageViewer::resetSelectionForNewImage(const QString &path)
{
    // Keep a screen ROI across images only while zoom is locked. Otherwise
    // drop it so the panel does not report the previous image's region.
    if (browsePathEquals(path, m_currentPath))
        return;
    const bool real = hasRealSelection();
    if (m_lockZoom && real)
    {
        // Snapshot the screen box; recommitLockedSelection projects it back.
        if (!m_selectionImage.isEmpty())
        {
            int sx = 0;
            int sy = 0;
            int sw = 0;
            int sh = 0;
            m_view.imageRectToScreen(m_selectionImage.x(), m_selectionImage.y(),
                                     m_selectionImage.width(), m_selectionImage.height(), sx, sy,
                                     sw, sh);
            m_lockedScreenRoi = (sw > 0 && sh > 0) ? QRect(sx, sy, sw, sh) : QRect();
        }
        else
        {
            const QRect widget = QRect(m_selStart, m_selEnd).normalized();
            m_lockedScreenRoi = (widget.width() > 5 && widget.height() > 5) ? widget : QRect();
        }
        m_selecting = false;
        m_selStart = m_selEnd = QPoint();
        return;
    }
    m_selecting = false;
    const bool had = !m_selectionImage.isEmpty() || m_selStart != m_selEnd;
    m_selStart = m_selEnd = QPoint();
    m_selectionImage = QRect();
    m_lockedScreenRoi = QRect();
    cancelRoiStats();
    if (had)
    {
        update();
        emit selectionChanged(QRect());
    }
}

void ImageViewer::recommitLockedSelection()
{
    if (!m_lockZoom || m_selecting)
        return;
    if (!m_lockedScreenRoi.isEmpty())
    {
        const QRect screen = m_lockedScreenRoi;
        m_lockedScreenRoi = QRect();
        publishImageSelection(imageRectFromScreen(screen));
        return;
    }
    // Same-path reload returns before the snapshot. Re-publish the image rect.
    if (!m_selectionImage.isEmpty())
        publishImageSelection(m_selectionImage);
}

void ImageViewer::applyPendingView()
{
    if (!m_pendingView)
        return;
    // Restored scale is untrusted. Clamp it to the interactive zoom range.
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
    if (context())
        m_gpu.clear(); // GlOwnerContext restores the caller's context
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
    // Apply once the async load has screen geometry.
    m_pendingView = v;
}

void ImageViewer::preloadNeighbors(const QString &path)
{
    if (m_currentIndex < 0)
        return;
    cancelPreloads();
    // Optional full prev/next decode; default is display-raster only.
    if (QSettings().value(QStringLiteral("viewer/preloadFullNeighbors"), false).toBool())
    {
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
    // cancelDisplayRasterPreloads() does not touch m_neighborPreloads.
    preloadDisplayRasterNeighbors(path);
}

void ImageViewer::cancelCurrentLoad()
{
    stopFullRawDecode(false);
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
    {
        ++m_displayRasterRetain.cancels;
        TaskScheduler::cancel(preload.handle);
    }
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
        else
            mviewer::application::ImageLoadingService::instance().cancelAsync(p.handle);
    }
    m_neighborPreloads.clear();
    return match;
}

ImageViewer::DisplayRasterPreload ImageViewer::takeMatchingDisplayRasterPreload(const QString &path)
{
    DisplayRasterPreload match;
    for (auto &preload : m_displayRasterPreloads)
    {
        const bool usable =
            !match.handle && browsePathEquals(preload.path, path) && preload.handle &&
            !preload.handle->isCancelled() &&
            !(preload.state && preload.state->delivered.load(std::memory_order_acquire));
        if (usable)
            match = std::move(preload);
        else
            TaskScheduler::cancel(preload.handle);
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
    if (image)
        *image = warm->image;
    if (sourceSize)
        *sourceSize = size;
    if (sourceRect)
        *sourceRect = warm->sourceRect.isValid() ? warm->sourceRect : QRect(QPoint(0, 0), size);
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
        ++m_displayRasterRetain.evictions;
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
    QueueDecodeSpan preloadQueue(QueueDecodeRole::NeighborPreload, path, ctx);
    if (ctx.isCancelled())
    {
        const uint64_t promoted =
            state ? state->promotedGeneration.load(std::memory_order_acquire) : 0;
        if (promoted == 0)
            return;
        // Already promoted into the current image: a silent cancel would leave
        // startLodDisplay waiting on a handle that never delivers.
        DisplayRasterPreloadResult cancelled;
        cancelled.path = path;
        cancelled.browseGeneration = browseGeneration;
        cancelled.state = state;
        cancelled.target = target;
        cancelled.failed = true;
        queueDisplayRasterPreloadResult(guard, std::move(cancelled));
        return;
    }
    DisplayRasterPreloadResult result;
    result.path = path;
    result.browseGeneration = browseGeneration;
    result.state = state;
    result.target = target;
    try
    {
        preloadQueue.beginDecode();
        result.source = mviewer::core::SourceImage::open(path.toStdString());
        if (!result.source)
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        result.sourceSize =
            QSize(result.source->metadata().width, result.source->metadata().height);
        const int sw = result.sourceSize.width();
        const int sh = result.sourceSize.height();
        // Same cut as runRasterWorker, so a mid-size warm raster can be promoted.
        if (!shouldUseDisplayLod(result.source->metadata()))
        {
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        WarmDisplayLodRequest warmReq;
        warmReq.sourceW = sw;
        warmReq.sourceH = sh;
        warmReq.fallbackEdge = kDisplayWarmMaxEdge;
        if (state)
        {
            warmReq.widgetW = state->widgetW;
            warmReq.widgetH = state->widgetH;
            warmReq.dpr = state->dpr;
            warmReq.lockedScale = state->lockedScale;
            warmReq.maximizeFit = state->maximizeFit;
        }
        const WarmDisplayLodPlan plan = planWarmDisplayLod(warmReq);
        QueueDecodeSpan::closePhase();
        const auto raster = result.source->decodeLod(plan.maxEdge);
        QueueDecodeSpan::closePhase();
        if (!raster.ok || raster.pixels.isNull())
        {
            result.failed = true;
            queueDisplayRasterPreloadResult(guard, std::move(result));
            return;
        }
        if (plan.sharp)
        {
            result.image = sharpFitDisplayImage(raster.pixels, raster.metadata, target, plan.exactW,
                                                plan.exactH, ctx.cancel.get());
        }
        else
        {
            result.image = mvcore::toDisplayQImage(raster.pixels, raster.metadata, target);
        }
        if (result.image.isNull() || result.image.width() <= 0)
            result.failed = true;
        else
        {
            result.sourceRect = QRect(0, 0, sw, sh);
            result.density = static_cast<double>(sw) / result.image.width();
        }
    }
    catch (...)
    {
        result.failed = true;
    }
    if (ctx.isCancelled())
    {
        const uint64_t promoted =
            state ? state->promotedGeneration.load(std::memory_order_acquire) : 0;
        if (promoted == 0)
            return;
        if (!result.failed && result.image.isNull())
            result.failed = true;
    }
    queueDisplayRasterPreloadResult(guard, std::move(result));
}

void ImageViewer::queueDisplayRasterPreloadResult(
    const std::shared_ptr<QPointer<ImageViewer>> &guard, DisplayRasterPreloadResult result)
{
    QueueDecodeSpan::finishCurrent();
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

void ImageViewer::applyPromotedDisplayRaster(DisplayRasterPreloadResult result, uint64_t gen)
{
    // Validate before touching the in-flight request. Resetting first lets an
    // older promoted callback (B, or the first A of A-B-A) orphan the newer
    // cancel handle, and a same-generation stale serial then probes again.
    const uint64_t serial =
        result.state ? result.state->displaySerial.load(std::memory_order_acquire) : 0;
    const bool pathOk = browsePathEquals(result.path, m_currentPath);
    const bool genOk = gen == m_requestGen;
    const bool targetOk = result.target.cacheKey() == m_displayColorTarget.cacheKey();
    if (!pathOk || !genOk || !targetOk)
        return;

    const bool serialOk = serial != 0 && serial == m_displayRequestSerial;
    // startLodDisplay moves the preload into m_displayRequest, so an empty
    // promoted slot is still the current request when its serial matches.
    // Serial 0 was never stamped. Adopt it only when no other request owns
    // the slot. Copying the live serial onto it would accept an old result.
    const bool unpromotedCurrent = serial == 0 && !m_displayRequest;
    if (!serialOk && !unpromotedCurrent)
        return;

    m_displayRequest.reset();
    m_promotedDisplayRasterPreload = DisplayRasterPreload{};
    if (result.failed || result.image.isNull())
    {
        // #380: cancelled, already-delivered, or empty current preload.
        startLodDisplay(result.path, gen);
        return;
    }

    const uint64_t applySerial = serial != 0 ? serial : ++m_displayRequestSerial;
    applyDisplayRaster(result.path, gen, std::move(result.source), std::move(result.image),
                       result.sourceRect, result.sourceSize, result.density, result.target, false,
                       applySerial);
    // A current raster can still be dropped (full-raw guard). Re-probe instead
    // of leaving the viewer pending with no image and no request (#380).
    if (m_raster.image.isNull() && !m_displayRequest)
        startLodDisplay(result.path, gen);
}

void ImageViewer::applyDisplayRasterPreloadResult(DisplayRasterPreloadResult result)
{
    if (result.state)
    {
        result.state->delivered.store(true, std::memory_order_release);
        m_displayRasterPreloads.erase(std::remove_if(m_displayRasterPreloads.begin(),
                                                     m_displayRasterPreloads.end(),
                                                     [&](const DisplayRasterPreload &preload)
                                                     { return preload.state == result.state; }),
                                      m_displayRasterPreloads.end());
    }
    const uint64_t promotedGeneration =
        result.state ? result.state->promotedGeneration.load(std::memory_order_acquire) : 0;
    const bool adopted = result.state && (promotedGeneration != 0 ||
                                          m_promotedDisplayRasterPreload.state == result.state);
    const bool waitingForThisPath = m_largeSourcePending &&
                                    browsePathEquals(result.path, m_currentPath) && !m_lodMode &&
                                    m_raster.image.isNull();
    if (adopted || waitingForThisPath)
    {
        const uint64_t gen = promotedGeneration != 0 ? promotedGeneration : m_requestGen;
        applyPromotedDisplayRaster(std::move(result), gen);
        return;
    }

    // Large neighbors (> 48 MP) are warmed as a bounded display raster only; review P2-4.
    const qint64 pixels =
        static_cast<qint64>(result.sourceSize.width()) * result.sourceSize.height();
    if (pixels > kFullPreloadMaxPixels)
    {
        for (auto it = m_neighborPreloads.begin(); it != m_neighborPreloads.end();)
        {
            if (browsePathEquals(it->path, result.path))
            {
                mviewer::application::ImageLoadingService::instance().cancelAsync(it->handle);
                it = m_neighborPreloads.erase(it);
            }
            else
                ++it;
        }
    }

    if (result.browseGeneration != m_displayRasterBrowseGeneration ||
        result.target.cacheKey() != m_displayColorTarget.cacheKey() || result.failed ||
        result.image.isNull())
        return;
    const bool stillListed = indexInBrowseSequence(result.path) >= 0;
    if (stillListed)
        storeWarmDisplayRaster(std::move(result));
}
