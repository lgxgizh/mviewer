// M47 Phase 2 — ImageViewer LOD-first display.
//
// For large sources the viewer no longer requires a full-resolution ImageFrame
// to display: a bounded display raster is requested from the source-backed
// SourceImage abstraction (core) and shown instead —
//   * zoomed out:  a viewport-appropriate LOD (decodeLod; NativeLod for JPEG),
//   * zoomed in:   a bounded region raster covering the visible source rect
//                  (decodeRegion; BoundedRasterRegion classification).
// The full-resolution frame (when it loads) remains the analysis/Inspector
// source and is NEVER the display precondition. Small sources keep the
// existing fast path untouched.
//
// Lifetime/cancellation mirrors the M46 contract: every request carries the
// viewer's request generation and the consumer lifetime token; results are
// marshalled to the UI thread through a QPointer guard and discarded when the
// generation/path no longer match or the viewer is gone. One in-flight
// request at a time (latest wins); view churn is debounced through a single-
// shot timer so pan/zoom bursts coalesce.
#include "imageviewer.h"

#include "core/image/SourceImage.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QFileInfo>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <cmath>

// Sources with at most this many pixels keep the existing full-frame fast
// path (no probe-driven display change). constexpr at global scope: they are
// file-local (internal linkage) and shared by the worker + the viewer members.
constexpr qint64 kLodThresholdPixels = 16 * 1000 * 1000; // 16 MP
// Bounds for a single display raster request (bounded memory by design).
constexpr int kMaxLodEdge = 4096;          // LOD longest edge (<= 48 MB RGB)
constexpr double kMaxRasterPixels = 8.0 * 1000 * 1000; // region raster cap
constexpr double kRasterOverscan = 1.25;   // request/keep margin around density
// Analysis-support full frame loads are only issued for sources whose RGB
// materialization fits comfortably under Qt's 256 MB allocation limit
// (60 MP * 3 B = 180 MB, leaving headroom for QImage copies). Larger sources
// skip the load — Phase 4 adds explicit analysis source materialization.
constexpr qint64 kAnalysisFeasiblePixels = 60 * 1000 * 1000; // 60 MP

// Defined at GLOBAL scope to match the forward declaration in imageviewer.h
// (ImageViewer::runRasterWorker references it from the header).
struct RasterRequest
{
    QString path;
    uint64_t generation = 0;
    bool fullLod = true; // true = decodeLod over the full source
    int maxEdge = 0;     // fullLod target
    int rx = 0, ry = 0, rw = 0, rh = 0; // region source rect
    int tw = 0, th = 0;                 // region raster target size
};

// The raster worker body: probe -> classify -> decodeLod/decodeRegion ->
// marshal. Private static member so it can reach marshalDisplayRaster while
// the submit lambda captures no raw `this`.
void ImageViewer::runRasterWorker(const RasterRequest &req, const TaskScheduler::TaskContext &ctx,
                                  const std::shared_ptr<QPointer<ImageViewer>> &guard)
{
    if (ctx.isCancelled())
        return; // superseded while queued
    auto source = mviewer::core::SourceImage::open(req.path.toStdString());
    if (!source)
    {
        ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, nullptr, QImage(),
                                          QRect(), QSize(), 1.0);
        return;
    }
    const QSize srcSize(source->metadata().width, source->metadata().height);
    if (static_cast<qint64>(srcSize.width()) * srcSize.height() <= kLodThresholdPixels)
    {
        // Small source: the existing full-frame fast path owns the display;
        // only propagate the source dims (keeps the worker contract uniform).
        ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, std::move(source),
                                          QImage(), QRect(), srcSize, 1.0);
        return;
    }
    if (ctx.isCancelled())
        return; // superseded during the probe
    ImageData pixels;
    QRect covered;
    double density = 1.0;
    if (req.fullLod)
    {
        pixels = source->decodeLod(req.maxEdge);
        covered = QRect(0, 0, srcSize.width(), srcSize.height());
        if (!pixels.isNull())
            density = static_cast<double>(srcSize.width()) / pixels.width;
    }
    else
    {
        pixels = source->decodeRegion(req.rx, req.ry, req.rw, req.rh, req.tw, req.th);
        covered = QRect(req.rx, req.ry, req.rw, req.rh);
        if (!pixels.isNull())
            density = static_cast<double>(req.rw) / pixels.width;
    }
    if (ctx.isCancelled())
        return; // superseded during the decode
    QImage raster;
    if (!pixels.isNull())
        raster = mvcore::toQImage(pixels);
    ImageViewer::marshalDisplayRaster(guard, req.path, req.generation, std::move(source),
                                      std::move(raster), covered, srcSize, density);
}

void ImageViewer::marshalDisplayRaster(const std::shared_ptr<QPointer<ImageViewer>> &guard,
                                       const QString &path, uint64_t generation,
                                       std::shared_ptr<mviewer::core::SourceImage> source,
                                       QImage image, QRect sourceRect, QSize sourceSize,
                                       double density)
{
    if (!qApp)
        return; // app teardown
    QMetaObject::invokeMethod(
        qApp,
        [guard, path, generation, source = std::move(source), image = std::move(image),
         sourceRect, sourceSize, density]() mutable
        {
            try
            {
                ImageViewer *viewer = guard->data();
                if (!viewer)
                    return; // viewer destroyed; never touch it
                viewer->applyDisplayRaster(path, generation, std::move(source),
                                           std::move(image), sourceRect, sourceSize, density);
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
    (void)path;
    (void)generation;
    // The raster path owns the verdict for this image until the first result
    // (probe-only for small sources, a raster for large ones, or a probe
    // failure) lands in applyDisplayRaster.
    m_largeSourcePending = true;
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
    if (m_displayUpgradeScheduled || !m_lodMode)
        return;
    m_displayUpgradeScheduled = true;
    QTimer::singleShot(0, this, &ImageViewer::requestDisplayRaster);
}

bool ImageViewer::displayNeedsUpgrade() const
{
    if (!m_lodMode || m_raster.image.isNull())
        return false; // upgrades only apply once the raster path is active
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
    QRect covered = m_raster.sourceRect;
    covered.adjust(-covered.width() / 8, -covered.height() / 8, covered.width() / 8,
                   covered.height() / 8);
    const QRect visible(vx, vy, vw, vh);
    if (!covered.contains(visible))
        return true;
    // Density: the raster must provide at least the view density (with
    // overscan), otherwise the display is upscaling a too-coarse raster.
    const double viewDensity = m_view.scale * std::max(1.0, devicePixelRatioF());
    const double rasterDensity = 1.0 / m_raster.density; // raster px per source px
    return rasterDensity < viewDensity / kRasterOverscan;
}

void ImageViewer::requestDisplayRaster()
{
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
    if (m_currentPath.isEmpty())
        return;
    const bool firstRequest = !m_lodMode && m_raster.image.isNull();
    const int sw = m_sourceImage ? m_sourceImage->metadata().width
                                 : m_raster.sourceSize.width();
    const int sh = m_sourceImage ? m_sourceImage->metadata().height
                                 : m_raster.sourceSize.height();

    cancelDisplayRequest();
    RasterRequest req;
    req.path = m_currentPath;
    req.generation = m_requestGen;

    if (firstRequest || sw <= 0 || sh <= 0)
    {
        // First display: request a viewport LOD sized for the widget (the
        // source dims are not known on the UI thread yet; the worker probes).
        const double dpr = std::max(1.0, devicePixelRatioF());
        req.fullLod = true;
        req.maxEdge = static_cast<int>(
            std::ceil(std::max(static_cast<double>(width()), static_cast<double>(height())) *
                      dpr * kRasterOverscan));
        req.maxEdge = std::max(64, std::min(req.maxEdge, kMaxLodEdge));
    }
    else
    {
        const double dpr = std::max(1.0, devicePixelRatioF());
        const double d = m_view.scale * dpr;
        if (d < 1.0)
        {
            // Zoomed out: viewport LOD over the full source.
            req.fullLod = true;
            const double longEdge = static_cast<double>(std::max(sw, sh));
            req.maxEdge = static_cast<int>(std::ceil(longEdge * d * kRasterOverscan));
            req.maxEdge = std::max(64, std::min(req.maxEdge, kMaxLodEdge));
        }
        else
        {
            // Zoomed in: bounded region raster covering the visible source
            // rect (inflated) at the viewport density, capped by kMaxRasterPixels.
            int vx = 0, vy = 0, vw = 0, vh = 0;
            m_view.visibleImageRect(sw, sh, vx, vy, vw, vh);
            if (vw <= 0 || vh <= 0)
                return;
            const int inflateX = std::max(1, vw / 8);
            const int inflateY = std::max(1, vh / 8);
            const int x0 = std::max(0, vx - inflateX);
            const int y0 = std::max(0, vy - inflateY);
            const int x1 = std::min(sw, vx + vw + inflateX);
            const int y1 = std::min(sh, vy + vh + inflateY);
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
        }
    }

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
            // scheduler (it would terminate the process).
            try
            {
                runRasterWorker(req, ctx, guard);
            }
            catch (...)
            {
                // An unexpected decoder exception must never escape into the
                // scheduler (it would terminate the process). The display
                // keeps the current raster; the next upgrade retries.
            }
        },
        {}, std::chrono::steady_clock::time_point::max(), [] {});
}

void ImageViewer::applyDisplayRaster(const QString &path, uint64_t generation,
                                     std::shared_ptr<mviewer::core::SourceImage> source,
                                     QImage image, QRect sourceRect, QSize sourceSize,
                                     double density)
{
    if (path != m_currentPath || generation != m_requestGen)
        return; // superseded (A -> B -> A) or stale request
    m_displayRequest.reset();
    m_displayUpgradeScheduled = false;
    m_largeSourcePending = false; // first raster-path verdict has landed
    // Capture source validity BEFORE the move into m_sourceImage: the
    // analysis-load verdict below must not observe the moved-from handle.
    const bool sourceValid = source != nullptr;
    if (source)
        m_sourceImage = std::move(source);
    const bool firstRaster = m_raster.image.isNull();

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

    m_raster = DisplayRaster{std::move(image), sourceRect, sourceSize, density};
    if (firstRaster)
    {
        m_lodMode = true;
        m_loading = false; // the display is usable now
        // Fit the full source into the widget (the provisional/full-frame
        // paths never ran their fit for this image).
        m_view.screenW = width();
        m_view.screenH = height();
        const FitPolicy fitPolicy = property("mviewerFullscreenRequested").toBool()
                                        ? FitPolicy::MaximizeClient
                                        : FitPolicy::Comfortable;
        m_view.fit(m_raster.sourceSize.width(), m_raster.sourceSize.height(), fitPolicy);
        m_fitMode = true;
        advanceViewportRevision();
        emitZoom();
        const QFileInfo info(m_currentPath);
        const QString position = m_currentIndex >= 0
                                     ? QString(" [%1/%2]").arg(m_currentIndex + 1)
                                                           .arg(m_fileList.size())
                                     : QString();
        setWindowTitle(QString("%1 (%2x%3)%4 - MViewer")
                           .arg(info.fileName())
                           .arg(m_raster.sourceSize.width())
                           .arg(m_raster.sourceSize.height())
                           .arg(position));
        emit displayReady(m_raster.sourceSize);
        // The first raster made this a LARGE source: decide the analysis load.
        runAnalysisLoadDecision(sourceValid, sourceSize);
    }
    update();
}

// ── Analysis-support full-frame load decision ───────────────────────────────
// Runs on the first raster-path verdict (probe-only small source, probe
// failure, or the first raster) and issues at most ONE load per image:
//   * probe failed / small source        -> issue the load (the fast path
//     owns small images; a failed probe surfaces through the load error);
//   * large, analysis-feasible           -> issue the load (analysis
//     consumers get the full frame);
//   * large, NOT feasible (e.g. 100 MP, > Qt's allocation limit) -> skip
//     (the load could never succeed; Phase 4 adds explicit materialization).
void ImageViewer::runAnalysisLoadDecision(bool sourceValid, const QSize &sourceSize)
{
    if (m_analysisLoadIssued)
        return;
    bool issueLoad = true;
    if (sourceValid && m_lodMode)
    {
        issueLoad = static_cast<qint64>(sourceSize.width()) * sourceSize.height() <=
                    kAnalysisFeasiblePixels;
    }
    if (issueLoad)
    {
        m_analysisLoadIssued = true;
        issueAnalysisLoad(m_currentPath, m_requestGen);
    }
    else
    {
        m_pendingAnalysisPreload.reset();
    }
}
void ImageViewer::drawDisplayRaster(QPainter &painter) const
{
    if (m_raster.image.isNull())
        return;
    const QRect &r = m_raster.sourceRect;
    int sx = 0, sy = 0, sw = 0, sh = 0;
    m_view.imageRectToScreen(r.x(), r.y(), r.width(), r.height(), sx, sy, sw, sh);
    if (sw <= 0 || sh <= 0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRect(sx, sy, sw, sh), m_raster.image);
    painter.restore();
}
