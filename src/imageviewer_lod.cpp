#include "imageviewer.h"
#include "imageviewer_displayfit.h"

#include "core/analysis/ImageOverlay.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageStats.h"
#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "display/DisplayColorContextProvider.h"

#include <QApplication>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cmath>
// Zoomed-in regions keep kRasterOverscan as a density margin. Sharp-owned
// zoomed-out fit requests the exact device-pixel edge (no overscan).
constexpr double kRasterOverscan = 1.25;
constexpr qint64 kAnalysisFeasiblePixels = 60 * 1000 * 1000; // 60 MP analysis cap

// Global: imageviewer.h forward-declares RasterRequest for runRasterWorker.
struct RasterRequest
{
    QString path;
    uint64_t generation = 0;
    uint64_t serial = 0; // display-request serial; stale pans are dropped
    mviewer::core::DisplayColorContext target = mviewer::core::DisplayColorContext::sRGB();
    bool fullLod = true;                // true = decodeLod over the full source
    int maxEdge = 0;                    // fullLod target
    int rx = 0, ry = 0, rw = 0, rh = 0; // region source rect
    int tw = 0, th = 0;                 // region raster target size
    bool sharpFit = false;              // zoomed out: Sharp to exact device px
    int exactW = 0;
    int exactH = 0;
};

namespace
{

QWindow *hostWindowHandle(const QWidget *widget)
{
    if (!widget)
        return nullptr;
    if (QWindow *own = widget->windowHandle())
        return own;
    const QWidget *host = widget->window();
    return host ? host->windowHandle() : nullptr;
}

// Zoomed in: visible source rect inflated by 1/8, at density `d`. False if empty.
bool fillRegionRasterRequest(RasterRequest &req, const QRect &visible, const QSize &source,
                             double d)
{
    if (visible.width() <= 0 || visible.height() <= 0)
        return false;
    const int inflateX = std::max(1, visible.width() / 8);
    const int inflateY = std::max(1, visible.height() / 8);
    const int x0 = std::max(0, visible.x() - inflateX);
    const int y0 = std::max(0, visible.y() - inflateY);
    const int x1 = std::min(source.width(), visible.x() + visible.width() + inflateX);
    const int y1 = std::min(source.height(), visible.y() + visible.height() + inflateY);
    req.fullLod = false;
    req.rx = x0;
    req.ry = y0;
    req.rw = x1 - x0;
    req.rh = y1 - y0;
    double tw = std::ceil(static_cast<double>(req.rw) * d * kRasterOverscan);
    double th = std::ceil(static_cast<double>(req.rh) * d * kRasterOverscan);
    const double total = tw * th;
    if (total > kMaxRasterPixels)
    {
        const double f = std::sqrt(kMaxRasterPixels / total);
        tw *= f;
        th *= f;
    }
    req.tw = std::max(1, static_cast<int>(tw));
    req.th = std::max(1, static_cast<int>(th));
    return true;
}

} // namespace

void ImageViewer::presentLoadFailure()
{
    m_provisionalPath.clear();
    m_provisionalImage = QImage();
    m_provisionalSourceSize = QSize();
    m_transitionImage = QImage();
    m_transitionSourceSize = QSize();
    m_loading = false;
    // Keep m_fitMode. Zoom lock treats a cleared fit flag as an explicit zoom
    // and would pin this scale onto the next image.
    setWindowTitle(tr("Unable to Load - %1 - MViewer").arg(QFileInfo(m_currentPath).fileName()));
    emit zoomChanged(-1); // status bar treats a negative percent as "no image"
    update();
    emit loadFailed(m_currentPath);
}

void ImageViewer::onDisplayScreenChanged()
{
    if (QWindow *handle = hostWindowHandle(this))
        setDisplayColorContext(DisplayColorContextProvider::forWindow(handle));
}

void ImageViewer::bindDisplayColorScreen()
{
    QWindow *handle = hostWindowHandle(this);
    if (!handle)
        return;
    connect(handle, &QWindow::screenChanged, this, &ImageViewer::onDisplayScreenChanged,
            Qt::UniqueConnection);
    setDisplayColorContext(DisplayColorContextProvider::forWindow(handle));
}

void ImageViewer::setDisplayColorContext(const mviewer::core::DisplayColorContext &target)
{
    if (target.cacheKey() == m_displayColorTarget.cacheKey())
        return;

    const bool wasLod = m_lodMode;
    m_displayColorTarget = target;
    // A target change invalidates presentation-only state. Source/analysis
    // state remains intact and queued results are rejected by cache identity.
    cancelDisplayRequest();
    ++m_displayRasterBrowseGeneration;
    cancelDisplayRasterPreloads();
    m_displayRasterWarm.clear();
    m_displayRasterWarmBytes = 0;
    m_raster = DisplayRaster{};
    m_lodMode = false;
    m_displayDegraded = false;

    ++m_imageGeneration;
    m_tileRequests.reset(m_imageGeneration);
    m_tileCache.clear();
    ++m_overlayGeneration;
    m_overlayRequests.reset(m_overlayGeneration);
    m_overlayCache.clear();
    m_gpu.clear();
    // A profile change can race the first small-source verdict. Restarting the
    // display request keeps that verdict as the owner of the foreground load
    // instead of cancelling the only trigger before it reaches the UI thread.
    if (!m_currentPath.isEmpty() && (!m_frame || wasLod))
        requestDisplayRaster();
    update();
}

// Probe -> classify -> decodeLod/decodeRegion -> optional Sharp fit -> marshal.
void ImageViewer::runRasterWorker(const RasterRequest &req, const TaskScheduler::TaskContext &ctx,
                                  const std::shared_ptr<QPointer<ImageViewer>> &guard)
{
    if (ctx.isCancelled())
        return; // superseded while queued
    std::shared_ptr<mviewer::core::SourceImage> source;
    bool failed = false;
    try
    {
        source = mviewer::core::SourceImage::open(req.path.toStdString());
    }
    catch (...)
    {
        failed = true; // terminal: must reach loadFailed, not a stuck pending state
    }
    // Cancelled is not a decode failure: do not degrade the replacement request.
    if (ctx.isCancelled())
        return;
    if (failed)
    {
        ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, std::move(source),
                                          QImage(), QRect(), QSize(), 1.0, req.target, true,
                                          req.serial);
        return;
    }
    if (!source)
    {
        // No SourceImage: the full-frame loader decides. Not loadFailed here.
        ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, nullptr, QImage(),
                                          QRect(), QSize(), 1.0, req.target, false, req.serial);
        return;
    }
    const mviewer::domain::ImageMetadata &meta = source->metadata();
    const QSize srcSize(meta.width, meta.height);
    if (!shouldUseDisplayLod(meta))
    {
        ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, std::move(source),
                                          QImage(), QRect(), srcSize, 1.0, req.target, false,
                                          req.serial);
        return;
    }
    if (ctx.isCancelled())
        return; // superseded during the probe
    mviewer::core::SourceImage::RasterResult r;
    QRect covered;
    double density = 1.0;
    bool decodeFailed = false;
    try
    {
        if (req.fullLod)
        {
            r = source->decodeLod(req.maxEdge);
            // LOD raster covers the full source in DISPLAYED coordinates.
            covered = QRect(0, 0, srcSize.width(), srcSize.height());
            if (r.ok && !r.pixels.isNull())
                density = static_cast<double>(srcSize.width()) / r.pixels.width;
        }
        else
        {
            // Visible rect is displayed (oriented). decodeRegion takes raw
            // pre-EXIF coordinates (M48 B1/B2).
            const int orientation = source->orientation();
            const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
                {req.rx, req.ry, req.rw, req.rh}, source->rawWidth(), source->rawHeight(),
                orientation);
            r = source->decodeRegion(raw, req.tw, req.th);
            // Report the covered area back in DISPLAYED coordinates for drawing.
            const mviewer::core::SourceRect coveredD = mviewer::core::rawRectToOriented(
                r.coveredRect, source->rawWidth(), source->rawHeight(), orientation);
            covered = QRect(coveredD.x, coveredD.y, coveredD.w, coveredD.h);
            // Source px per output px in displayed space (orientations 5-8 swap axes).
            if (r.ok && !r.pixels.isNull() && r.pixels.width > 0 && covered.width() > 0)
                density =
                    static_cast<double>(covered.width()) / static_cast<double>(r.pixels.width);
        }
    }
    catch (...)
    {
        decodeFailed = true; // keep any good raster; the viewer is told via loadFailed
    }
    if (ctx.isCancelled())
        return; // superseded during the decode
    decodeFailed = decodeFailed || !r.ok || r.pixels.isNull();
    QImage raster;
    const bool sharp = !decodeFailed && req.sharpFit && req.exactW > 0 && req.exactH > 0;
    if (sharp)
        raster = sharpFitDisplayImage(r.pixels, r.metadata, req.target, req.exactW, req.exactH,
                                      ctx.cancel.get());
    else if (!decodeFailed)
        raster = mvcore::toDisplayQImage(r.pixels, r.metadata, req.target);
    if (ctx.isCancelled())
        return;
    if (sharp)
    {
        if (raster.isNull())
            decodeFailed = true;
        else if (raster.width() > 0)
        {
            const int coveredW = covered.width() > 0 ? covered.width() : srcSize.width();
            if (coveredW > 0)
                density = static_cast<double>(coveredW) / static_cast<double>(raster.width());
        }
    }
    ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, std::move(source),
                                      std::move(raster), covered, srcSize, density, req.target,
                                      decodeFailed, req.serial);
}

void ImageViewer::marshalDisplayRaster(const std::shared_ptr<QPointer<ImageViewer>> &guard,
                                       const QString &path, uint64_t generation,
                                       std::shared_ptr<mviewer::core::SourceImage> source,
                                       QImage image, QRect sourceRect, QSize sourceSize,
                                       double density,
                                       const mviewer::core::DisplayColorContext &target,
                                       bool failed, uint64_t serial)
{
    if (!qApp)
        return; // app teardown
    QMetaObject::invokeMethod(
        qApp,
        [guard, path, generation, source = std::move(source), image = std::move(image), sourceRect,
         sourceSize, density, target, failed, serial]() mutable
        {
            try
            {
                ImageViewer *viewer = guard->data();
                if (!viewer)
                    return; // viewer destroyed; never touch it
                viewer->applyDisplayRaster(path, generation, std::move(source), std::move(image),
                                           sourceRect, sourceSize, density, target, failed, serial);
            }
            catch (...)
            {
                // A delivery-side exception must never escape into the event
                // loop; the display simply keeps its current state.
            }
        },
        Qt::QueuedConnection);
}

void ImageViewer::startLodDisplay(const QString &path, uint64_t generation)
{
    // The raster path owns the verdict for this image until the first result
    // (probe-only for small sources, a raster for large ones, or a probe
    // failure) lands in applyDisplayRaster.
    m_largeSourcePending = true;
    m_displayDegraded = false; // a new image resets any prior terminal failure
    if (auto warm = takeWarmDisplayRaster(path))
    {
        applyDisplayRaster(path, generation, std::move(warm->source), std::move(warm->image),
                           warm->sourceRect, warm->sourceSize, warm->density, warm->target, false,
                           m_displayRequestSerial);
        return;
    }
    if (m_promotedDisplayRasterPreload.handle)
    {
        auto promoted = std::move(m_promotedDisplayRasterPreload);
        m_promotedDisplayRasterPreload = DisplayRasterPreload{};
        if (promoted.state)
        {
            promoted.state->promotedGeneration.store(generation, std::memory_order_release);
            promoted.state->displaySerial.store(m_displayRequestSerial, std::memory_order_release);
        }
        m_displayRequest = std::move(promoted.handle);
        return;
    }
    // The first raster request is issued immediately; the worker probes the
    // source, decides small-vs-large, and either returns the probe-only result
    // (small: the fast path owns the display) or the first viewport LOD.
    requestDisplayRaster();
}

void ImageViewer::cancelDisplayRequest()
{
    TaskScheduler::cancel(m_displayRequest);
    m_displayRequest.reset();
    m_displayUpgradeScheduled = false;
}

void ImageViewer::scheduleDisplayUpgrade()
{
    if (m_displayUpgradeScheduled || !m_lodMode || m_displayDegraded ||
        property("mviewerClosing").toBool() || !displayNeedsUpgrade())
        return;
    m_displayUpgradeScheduled = true;
    QTimer::singleShot(48, this, &ImageViewer::requestDisplayRaster);
}

bool ImageViewer::displayNeedsUpgrade() const
{
    if (!m_lodMode || m_raster.image.isNull())
        return false; // upgrades only apply once the raster path is active
    if (m_displayDegraded)
        return false; // M48 Phase 2: a terminal decode failure stops the loop
    const int sw = m_raster.sourceSize.width();
    const int sh = m_raster.sourceSize.height();
    if (sw <= 0 || sh <= 0)
        return false;
    int vx = 0, vy = 0, vw = 0, vh = 0;
    m_view.visibleImageRect(sw, sh, vx, vy, vw, vh);
    if (vw <= 0 || vh <= 0)
        return false;
    // Coverage: the visible source rect must fit inside the covered rect with
    // a margin (so small pans do not re-request).
    if (!displayRasterCoversView(m_raster.sourceRect, QRect(vx, vy, vw, vh)))
        return true;
    // Zoomed out: upgrade until the raster is the Sharp device-pixel size.
    // Zoomed in keeps the overscan margin and is not Sharp-resampled.
    const double viewDensity = m_view.scale * std::max(1.0, devicePixelRatioF());
    if (viewDensity < 1.0)
    {
        int tw = 1;
        int th = 1;
        devicePixelsForSource(m_raster.sourceRect.width(), m_raster.sourceRect.height(),
                              viewDensity, tw, th);
        return m_raster.image.width() != tw || m_raster.image.height() != th;
    }
    const double rasterDensity = 1.0 / m_raster.density;
    return rasterDensity < viewDensity / kRasterOverscan;
}

void ImageViewer::requestDisplayRaster()
{
    // A queued singleShot may outlive the request that scheduled it. Once a
    // decode has reached the terminal degraded state, discard that callback as
    // well as future viewport-triggered upgrades until setImage() resets it.
    if (m_displayDegraded || property("mviewerClosing").toBool())
    {
        m_displayUpgradeScheduled = false;
        return;
    }
    try
    {
        requestDisplayRasterImpl();
    }
    catch (...)
    {
        // An unexpected request-side exception must never escape into the
        // event loop; the next view change retries.
        m_displayUpgradeScheduled = false;
    }
}

void ImageViewer::requestDisplayRasterImpl()
{
    m_displayUpgradeScheduled = false;
    if (m_currentPath.isEmpty() || m_displayDegraded)
        return;
    const bool firstRequest = !m_lodMode && m_raster.image.isNull();
    if (!firstRequest && !displayNeedsUpgrade())
        return;
    const int sw = m_sourceImage ? m_sourceImage->metadata().width : m_raster.sourceSize.width();
    const int sh = m_sourceImage ? m_sourceImage->metadata().height : m_raster.sourceSize.height();

    cancelDisplayRequest();
    RasterRequest req;
    req.path = m_currentPath;
    req.generation = m_requestGen;
    req.target = m_displayColorTarget;

    if (firstRequest || sw <= 0 || sh <= 0)
    {
        // First display: request a viewport LOD sized for the widget (the
        // source dims are not known on the UI thread yet; the worker probes).
        const double dpr = std::max(1.0, devicePixelRatioF());
        req.fullLod = true;
        req.maxEdge = static_cast<int>(
            std::ceil(std::max(static_cast<double>(width()), static_cast<double>(height())) * dpr *
                      kRasterOverscan));
        req.maxEdge = std::max(kMinLodEdge, std::min(req.maxEdge, kMaxLodEdge));
    }
    else
    {
        const double dpr = std::max(1.0, devicePixelRatioF());
        const double d = m_view.scale * dpr;
        if (d < 1.0)
        {
            req.fullLod = true;
            req.sharpFit = true;
            devicePixelsForSource(sw, sh, d, req.exactW, req.exactH);
            const int wantEdge = std::max(req.exactW, req.exactH);
            req.maxEdge = std::max(kMinLodEdge, std::min(wantEdge, kMaxLodEdge));
        }
        else
        {
            int vx = 0, vy = 0, vw = 0, vh = 0;
            m_view.visibleImageRect(sw, sh, vx, vy, vw, vh);
            if (!fillRegionRasterRequest(req, QRect(vx, vy, vw, vh), QSize(sw, sh), d))
                return;
        }
    }

    req.serial = m_displayRequestSerial + 1;
    m_displayRequestSerial = req.serial;
    auto guard = std::make_shared<QPointer<ImageViewer>>(this);
    // Display-warmth work (probe + LOD/region decode) runs on the Thumbnail
    // pool: it must never count as a foreground DecodePool submission (the
    // preload-promotion contract counts DecodePool submissions) and never
    // queue behind gated Background work (the stale-preload workflow gates
    // Background while the foreground decode must still deliver).
    m_displayRequest = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Thumbnail,
        [req, guard](const TaskScheduler::TaskContext &ctx)
        {
            // An unexpected decoder exception must never escape into the
            // scheduler (it would terminate the process). It is still a
            // terminal display failure unless this request was superseded.
            try
            {
                runRasterWorker(req, ctx, guard);
            }
            catch (...)
            {
                if (!ctx.isCancelled())
                {
                    try
                    {
                        marshalDisplayRaster(guard, req.path, req.generation, nullptr, QImage(),
                                             QRect(), QSize(), 1.0, req.target, true, req.serial);
                    }
                    catch (...)
                    {
                        // Keep the scheduler worker noexcept even if the
                        // terminal delivery itself cannot be allocated during
                        // teardown. The lifetime/generation guards still make
                        // a missing late delivery safe for the viewer.
                    }
                }
            }
        },
        {}, std::chrono::steady_clock::time_point::max(), [] {});
    if (!m_displayRequest)
    {
        // The worker never started, so the serial must not drop a delivery
        // that was already queued for the previous request.
        m_displayRequestSerial = req.serial - 1;
        // A rejected display probe must still reach the pre-M47 full-frame
        // terminal path. In particular, a paused ThumbnailPool must not leave
        // m_largeSourcePending/m_loading set forever.
        m_largeSourcePending = false;
        runAnalysisLoadDecision(false, QSize());
    }
}

void ImageViewer::applyDisplayRaster(const QString &path, uint64_t generation,
                                     std::shared_ptr<mviewer::core::SourceImage> source,
                                     QImage image, QRect sourceRect, QSize sourceSize,
                                     double density,
                                     const mviewer::core::DisplayColorContext &target, bool failed,
                                     uint64_t serial)
{
    if (path != m_currentPath || generation != m_requestGen ||
        target.cacheKey() != m_displayColorTarget.cacheKey())
        return; // superseded (A -> B -> A) or stale request
    // A pan issues a new serial. A worker that already queued this delivery
    // cannot be recalled; drop it before it replaces the raster on screen.
    // Do not clear m_displayRequest: that handle belongs to the newer request.
    if (serial != m_displayRequestSerial)
        return;
    if (m_fullRawLoading ||
        (m_frame && m_frame->metadata().rawSource == mviewer::domain::RawSource::FullDemosaic))
    {
        m_displayRequest.reset();
        m_displayUpgradeScheduled = false;
        m_largeSourcePending = false;
        return;
    }
    m_displayRequest.reset();
    m_displayUpgradeScheduled = false;
    m_largeSourcePending = false; // first raster-path verdict has landed
    // Capture source validity BEFORE the move into m_sourceImage: the
    // analysis-load verdict below must not observe the moved-from handle.
    const bool sourceValid = source != nullptr;
    if (source)
        m_sourceImage = std::move(source);
    const bool firstRaster = m_raster.image.isNull();

    if (failed)
    {
        m_transitionImage = QImage();
        m_transitionSourceSize = QSize();
        // M48 Phase 2 terminal: a probe/LOD/region decode threw or returned
        // nothing usable. Reach an observable failed state (loadFailed), keep
        // any current good raster, and mark the display DEGRADED so the
        // upgrade path stops requesting (no infinite loop, no stuck pending).
        m_displayDegraded = true;
        if (firstRaster)
        {
            m_largeSourcePending = false;
            presentLoadFailure();
            return;
        }
        emit loadFailed(m_currentPath);
        update();
        return;
    }

    if (image.isNull())
    {
        // No raster delivered: a probe-only small-source verdict, a probe
        // failure, or a failed raster request. If the raster path is already
        // active, keep the current raster (progressive display).
        if (firstRaster)
            runAnalysisLoadDecision(sourceValid, sourceSize);
        update();
        return;
    }

    if (!acceptDisplayRaster(image, sourceRect, density))
    {
        if (displayNeedsUpgrade())
            scheduleDisplayUpgrade();
        return;
    }

    m_displayDegraded = false; // a successful raster clears the degraded state
    m_raster = DisplayRaster{std::move(image), sourceRect, sourceSize, density};
    if (firstRaster)
    {
        m_transitionImage = QImage();
        m_transitionSourceSize = QSize();
        // The LOD raster replaces the provisional thumbnail. Leaving it set
        // kept isLoadingBusy() true and the busy ticker repainting forever.
        m_provisionalPath.clear();
        m_provisionalImage = QImage();
        m_provisionalSourceSize = QSize();
        m_lodMode = true;
        m_loading = false; // the display is usable now
        // Fit the full source into the widget (the provisional/full-frame
        // paths never ran their fit for this image).
        m_view.screenW = width();
        m_view.screenH = height();
        if (m_lockZoom && !m_fitMode && m_view.scale > 0.0)
        {
            const int sw = m_raster.sourceSize.width();
            const int sh = m_raster.sourceSize.height();
            m_view.offsetX = (m_view.screenW - sw * m_view.scale) / 2.0;
            m_view.offsetY = (m_view.screenH - sh * m_view.scale) / 2.0;
        }
        else
        {
            const FitPolicy fitPolicy = property("mviewerFullscreenRequested").toBool()
                                            ? FitPolicy::MaximizeClient
                                            : FitPolicy::Comfortable;
            m_view.fit(m_raster.sourceSize.width(), m_raster.sourceSize.height(), fitPolicy);
            m_fitMode = true;
        }
        advanceViewportRevision();
        emitZoom();
        const QFileInfo info(m_currentPath);
        const QString position =
            m_currentIndex >= 0 ? QString(" [%1/%2]").arg(m_currentIndex + 1).arg(m_fileList.size())
                                : QString();
        setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                           .arg(info.fileName())
                           .arg(m_raster.sourceSize.width())
                           .arg(m_raster.sourceSize.height())
                           .arg(position));
        emit displayReady(m_raster.sourceSize);
        // Still within the cap may defer; small and multi-frame still load.
        runAnalysisLoadDecision(sourceValid, sourceSize);
    }
    // Region upgrades change the pixels under a stationary cursor.
    resamplePixelUnderCursor();
    update();
}

void ImageViewer::ensureAnalysisLoad()
{
    if (m_analysisLoadIssued || (m_frame && m_frame->isValid()) || m_currentPath.isEmpty())
        return;
    if (m_largeSourcePending)
    {
        setProperty("mviewerAnalysisWanted", true);
        return;
    }
    const qint64 pixels = static_cast<qint64>(m_raster.sourceSize.width()) *
                          static_cast<qint64>(m_raster.sourceSize.height());
    // Over the cap: refuse, and drop a retained full preload so it cannot land.
    if (m_lodMode && pixels > kAnalysisFeasiblePixels)
        m_pendingAnalysisPreload.reset();
    else
        issueAnalysisLoad(m_currentPath, m_requestGen);
    m_analysisLoadIssued = true;
    setProperty("mviewerAnalysisWanted", false);
}

void ImageViewer::runAnalysisLoadDecision(bool sourceValid, const QSize &sourceSize)
{
    if (m_analysisLoadIssued)
        return;
    const bool lod = sourceValid && m_lodMode;
    const qint64 pixels = lod ? static_cast<qint64>(sourceSize.width()) * sourceSize.height() : 0;
    const bool multi =
        lod && m_sourceImage &&
        (m_sourceImage->metadata().animated || m_sourceImage->metadata().frameCount > 1);
    if (!lod || multi || property("mviewerAnalysisWanted").toBool() ||
        (lod && pixels > kAnalysisFeasiblePixels))
        ensureAnalysisLoad();
    else
        m_pendingAnalysisPreload.reset();
}

PixelRGBA ImageViewer::sampleAnalysisPixel(int ix, int iy) const
{
    if (m_frame && m_frame->isValid())
        return samplePixel(m_frame->pixels(), ix, iy);

    PixelRGBA px;
    if (ix < 0 || iy < 0 || m_raster.image.isNull())
        return px;
    const QRect &covered = m_raster.sourceRect;
    if (covered.width() <= 0 || covered.height() <= 0 || !covered.contains(ix, iy))
        return px;
    const int rw = m_raster.image.width();
    const int rh = m_raster.image.height();
    if (rw <= 0 || rh <= 0)
        return px;
    const int sx = static_cast<int>((static_cast<qint64>(ix - covered.x()) * rw) / covered.width());
    const int sy =
        static_cast<int>((static_cast<qint64>(iy - covered.y()) * rh) / covered.height());
    const int cx = (std::max)(0, (std::min)(rw - 1, sx));
    const int cy = (std::max)(0, (std::min)(rh - 1, sy));
    const QColor color = m_raster.image.pixelColor(cx, cy);
    px.r = static_cast<uint8_t>(color.red());
    px.g = static_cast<uint8_t>(color.green());
    px.b = static_cast<uint8_t>(color.blue());
    px.a = static_cast<uint8_t>(color.alpha());
    px.valid = true;
    return px;
}

bool ImageViewer::acceptDisplayRaster(const QImage &image, const QRect &covered,
                                      double density) const
{
    if (image.isNull())
        return false;
    int vx = 0;
    int vy = 0;
    int vw = 0;
    int vh = 0;
    m_view.visibleImageRect(m_raster.sourceSize.width(), m_raster.sourceSize.height(), vx, vy, vw,
                            vh);
    const QRect visible(vx, vy, vw, vh);
    const double viewDensity = m_view.scale * std::max(1.0, devicePixelRatioF());
    // A Sharp raster at the exact device size replaces a finer provisional
    // that QPainter would only downsample.
    if (viewDensity < 1.0 && displayRasterCoversView(covered, visible))
    {
        int tw = 1;
        int th = 1;
        devicePixelsForSource(covered.width(), covered.height(), viewDensity, tw, th);
        if (image.width() == tw && image.height() == th)
            return true;
    }
    return shouldAcceptDisplayRaster(!m_raster.image.isNull(), m_raster.sourceRect,
                                     m_raster.density, covered, density, visible);
}

#include "imageviewer_lod_overlay.inc"

bool ImageViewer::lodRasterUsesSmoothing(double viewScale, double devicePixelRatio)
{
    const double dpr = devicePixelRatio > 1.0 ? devicePixelRatio : 1.0;
    return !(viewScale * dpr >= 1.0);
}

bool ImageViewer::displayRasterCoversView(const QRect &covered, const QRect &visible)
{
    if (visible.width() <= 0 || visible.height() <= 0 || covered.width() <= 0 ||
        covered.height() <= 0)
        return false;
    QRect expanded = covered;
    expanded.adjust(-expanded.width() / 8, -expanded.height() / 8, expanded.width() / 8,
                    expanded.height() / 8);
    return expanded.contains(visible);
}

bool ImageViewer::shouldAcceptDisplayRaster(bool haveCurrent, const QRect &currentCovered,
                                            double currentDensity, const QRect &incomingCovered,
                                            double incomingDensity, const QRect &visible)
{
    if (!haveCurrent || !(incomingDensity > 0.0) || !(currentDensity > 0.0))
        return true;
    if (visible.width() <= 0 || visible.height() <= 0)
        return true;
    const bool incomingCovers = displayRasterCoversView(incomingCovered, visible);
    const bool currentCovers = displayRasterCoversView(currentCovered, visible);
    if (incomingCovers && !currentCovers)
        return true;
    if (!incomingCovers && currentCovers)
        return false;
    return incomingDensity <= currentDensity + 1e-6;
}
