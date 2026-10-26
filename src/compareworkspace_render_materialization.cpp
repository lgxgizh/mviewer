#include "compareworkspace_display_pyramid.h"
#include "compareworkspace_p.h"

#include "Theme.h"

#include "core/analysis/PixelInspector.h"
#include "core/image/DisplayMip.h"
#include "core/image/SourceImage.h"
#include "core/render/DisplayResample.h"
#include "display/DisplayColorContextProvider.h"

#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cmath>

#include "compareworkspace_caption.h"
#include "compareworkspace_pane_header.h"

namespace
{
// Prefer CacheManager mip chain (path or repository makeKey) before re-decode /
// re-scale from full. Returns null ImageData on miss.
ImageData tryCacheBestMip(const std::string &path, int maxEdge)
{
    return mviewer::core::tryBestMip(path, maxEdge);
}

// getBestMip prefers the largest level <= maxEdge, so once lod 0 has left the
// cache a 4096 request can come back as the 2048 mip. A sharp raster that
// small at deep zoom is accepted as final and the pane stays soft for good.
// Dense enough = within 90% of what the request (capped at the source) needs.
bool mipDenseEnough(const ImageData &mip, int wantEdge, int sourceEdge)
{
    if (mip.isNull() || wantEdge <= 0)
        return false;
    const int need = sourceEdge > 0 ? std::min(wantEdge, sourceEdge) : wantEdge;
    return static_cast<long long>(std::max(mip.width, mip.height)) * 10 >=
           static_cast<long long>(need) * 9;
}

QRect coveredRectForRequest(const QSize &sourceDims,
                            const mviewer::ui::CompareDisplayRequest &request)
{
    const QRect full(QPoint(0, 0), sourceDims);
    if (!request.region || !request.sourceRect.isValid())
        return full;
    return request.sourceRect.intersected(full);
}

ImageData resolveSharpRegion(const ImageData &src,
                             const mviewer::ui::CompareDisplayRequest &request,
                             const std::atomic<bool> *cancel)
{
    thread_local mviewer::core::DisplayResampleScratch scratch;
    mviewer::core::DisplayResampleRequest resample;
    resample.source.x = request.sourceRect.x();
    resample.source.y = request.sourceRect.y();
    resample.source.width = request.sourceRect.width();
    resample.source.height = request.sourceRect.height();
    resample.targetWidth = request.target.width();
    resample.targetHeight = request.target.height();
    if (request.preview)
        resample.quality = mviewer::core::DisplayResampleQuality::Preview;
    return mviewer::core::resampleDisplay(src, resample, &scratch, cancel);
}

ImageData resolveLodFromCachedFull(const ImageData &src,
                                   const mviewer::ui::CompareDisplayRequest &request,
                                   const std::string &pathKey, const std::atomic<bool> *cancel)
{
    if (!request.target.isValid())
        return ImageData{};
    if (request.region && request.sourceRect.isValid())
        return resolveSharpRegion(src, request, cancel);
    const int wantEdge = std::max(request.target.width(), request.target.height());
    if (!pathKey.empty())
    {
        ImageData best = mviewer::core::ensureAndBestMip(pathKey, src, wantEdge);
        // The full frame is in hand: a sharp pass never settles for a coarser
        // cached mip (cheap and preview passes are followed by a sharp one).
        const bool needsDense = !request.provisional && !request.preview;
        if (needsDense && !best.isNull() &&
            !mipDenseEnough(best, wantEdge, std::max(src.width, src.height)))
            best = ImageData{};
        if (!best.isNull())
        {
            if (best.width > request.target.width() || best.height > request.target.height())
            {
                return RenderEngine::scaleBoundedStatic(
                    best, RenderSize{request.target.width(), request.target.height()});
            }
            return best;
        }
    }
    return RenderEngine::scaleBoundedStatic(
        src, RenderSize{request.target.width(), request.target.height()});
}

HistogramWidget *createPaneHistogramOverlay(QWidget *cellWidget, int index, bool visible)
{
    auto *hframe = new QFrame(cellWidget);
    hframe->setObjectName(QString("paneHistogramFrame%1").arg(index));
    hframe->setAttribute(Qt::WA_TranslucentBackground);
    hframe->setStyleSheet("background-color: rgba(0,0,0,90); border-radius:4px;");
    hframe->setVisible(visible);
    auto *layout = new QVBoxLayout(hframe);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *histogram = new HistogramWidget(hframe);
    histogram->setObjectName(QString("paneHistogram%1").arg(index));
    histogram->setOverlayStyle(true);
    layout->addWidget(histogram);
    hframe->raise();
    return histogram;
}

ComparePaneCaption *createPaneCaption(QWidget *cellWidget, int index, const ImageFrame *img)
{
    auto *caption = new ComparePaneCaption(cellWidget);
    caption->setObjectName(QString("paneCaption%1").arg(index));
    caption->setAlignment(Qt::AlignCenter);
    QFont capFont = caption->font();
    capFont.setPointSize(12);
    capFont.setBold(true);
    caption->setFont(capFont);
    caption->setStyleSheet(
        QStringLiteral("QLabel{background:%1;color:%2;padding:4px 8px;}")
            .arg(mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::Bg1),
                 mviewer::ui::Theme::themeColor(mviewer::ui::ThemeRole::TextPrimary)));
    caption->setMinimumWidth(0);
    caption->setMinimumHeight(28);
    caption->setVisible(false);
    if (img)
        caption->setFullText(QString::fromUtf8(img->metadata().fileName.data(),
                                               static_cast<int>(img->metadata().fileName.size())));
    return caption;
}
} // namespace
namespace
{

QWindow *compareHostWindow(const QWidget *widget)
{
    if (!widget)
        return nullptr;
    if (QWindow *own = widget->windowHandle())
        return own;
    // CompareWorkspace is a child of the compare dialog, so the monitor
    // profile lives on the dialog window, not on this widget.
    const QWidget *host = widget->window();
    return host ? host->windowHandle() : nullptr;
}

} // namespace

void CompareWorkspace::onDisplayScreenChanged()
{
    if (QWindow *handle = compareHostWindow(this))
        setDisplayColorContext(DisplayColorContextProvider::forWindow(handle));
}

void CompareWorkspace::bindDisplayColorScreen()
{
    QWindow *handle = compareHostWindow(this);
    if (!handle)
        return;
    connect(handle, &QWindow::screenChanged, this, &CompareWorkspace::onDisplayScreenChanged,
            Qt::UniqueConnection);
    setDisplayColorContext(DisplayColorContextProvider::forWindow(handle));
}

void CompareWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const Qt::WindowStates state = window() ? window()->windowState() : windowState();
    if (state.testFlag(Qt::WindowFullScreen) || state.testFlag(Qt::WindowMaximized))
        applyCompareSafeInsets();
    bindDisplayColorScreen();
    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    const bool normalGrid =
        m_pageStack && m_compareGridPage && m_pageStack->currentWidget() == m_compareGridPage;
    if (blinkActive)
    {
        // The active cell stretches across the grid. Refit that geometry but
        // keep the shared zoom ratio — fitAll() would snap it back to 1.
        reapplyFitPreservingRatio();
    }
    else if (normalGrid)
    {
        // Coalesce ordinary-grid resize bursts through the same post-layout
        // path used by mode transitions. It refits Fit-state panes to the new
        // geometry while preserving an intentional shared relative zoom ratio.
        schedulePostLayoutFit();
    }
    else
    {
        // Canvas and loading-page resize behavior remains the legacy immediate
        // fit path; the scheduled grid callback skips those hidden panes.
        fitAll();
    }
    positionCellHists();
    if (blinkActive || !normalGrid)
        scheduleDisplayLodRefresh();
    positionROIHud();
}

void CompareWorkspace::rebuildCells()
{
    endTemporaryCompare();
    // The terminal refreshAllDiffOverlays() at the end of this function covers
    // the whole rebuild (including the ROI re-applied below), so suppress the
    // refresh that applySelectionToAll() would otherwise schedule.
    m_rebuildingCells = true;
    // Two-image Blink detaches the inactive pane so the active one can span the
    // grid. Reattach every tracked pane before draining the layout, otherwise a
    // rebuild triggered by layout/navigation would leave that pane orphaned.
    for (RawImageView *view : m_cellViews)
    {
        QWidget *pane = view ? view->parentWidget() : nullptr;
        if (pane && m_layout->indexOf(pane) < 0)
            m_layout->addWidget(pane);
    }

    QLayoutItem *item;
    while ((item = m_layout->takeAt(0)))
    {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_cellLabels.clear();
    m_cellViews.clear();
    m_cellHists.clear();
    m_paneHistCache.clear();

    // Reset stale stretch factors from the previous layout. m_layout is reused
    // across rebuilds, and QGridLayout keeps row/column stretch even after the
    // widgets are removed — so switching e.g. 2x4 -> 1x2 would leave the empty
    // row/columns still claiming half of the viewport.
    for (int r = 0; r < m_layout->rowCount(); ++r)
        m_layout->setRowStretch(r, 0);
    for (int c = 0; c < m_layout->columnCount(); ++c)
        m_layout->setColumnStretch(c, 0);

    const int n = m_engine.imageCount();
    // Drop a stale focus lock when the image set shrank.
    if (m_focusIndex >= n)
    {
        m_focusIndex = -1;
        if (m_focusBtn)
            m_focusBtn->setChecked(false);
        if (m_focusLabel)
            m_focusLabel->setText(tr("Reference: —"));
    }
    buildCompareCells(n, m_engine.layout().cols);

    // M3: a layout switch / swap / preset / blink-stop destroys and recreates every
    // cell view, which would silently drop the ROI the user drew. Re-apply the last
    // selection so the red box survives grid re-layouts (applySelectionToAll mirrors
    // it across all cells, exactly as when it was first drawn).
    if (m_lastSelection.width > 0)
        applySelectionToAll(m_lastSelection);

    // Rebuilds create fresh overlay widgets. Request every pane histogram in
    // ONE async batch from the same adjusted/ROI-aware path used by the side
    // panel — never an N-call loop.
    if (m_paneHistOverlay)
    {
        std::vector<int> all;
        const int n = static_cast<int>(m_cellHists.size());
        all.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            all.push_back(i);
        scheduleHistogramRefresh(false, all);
    }

    if (m_blinkTimer && m_blinkTimer->isActive())
    {
        syncEngineBlink();
        applyBlink(m_blinkState);
    }

    // Fresh panes start Empty; Browse→Compare handoff may already have seeded
    // pane bitmaps (warm stand-ins).
    if (m_session)
        m_session->displayState.reset(static_cast<int>(m_cellViews.size()));
    applyPendingWarmSeeds();

    // M28 P1-01: every pane was recreated, so materialize all pane images in ONE
    // async display batch (latest-wins generation). This is independent of the
    // terminal diff batch below: each rebuild submits exactly two Analysis tasks.
    {
        std::vector<int> all;
        all.reserve(m_cellViews.size());
        for (int i = 0; i < static_cast<int>(m_cellViews.size()); ++i)
            all.push_back(i);
        scheduleDisplayMaterialization(all);
    }

    // Every pane was recreated, so recompute all diff overlays + metrics in a
    // single async batch (latest-wins generation). This is the one terminal
    // refresh for the rebuild state transition.
    refreshAllDiffOverlays();
    m_rebuildingCells = false;

    QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
}

// M28 P1-01: schedule an async pane-materialization batch for the given panes
// (plus every pane currently showing a null image, so canceling an initial
// all-pane batch with a later single-pane adjustment never strands blank
// panes). One Analysis-priority task per call; never a synchronous fallback.
void CompareWorkspace::buildCompareCells(int n, int columns)
{
    QStringList allPaths;
    allPaths.reserve(n);
    for (int idx = 0; idx < n; ++idx)
    {
        const ImageFrame *f = m_engine.imageAt(idx);
        if (f && !f->metadata().filePath.empty())
            allPaths.append(QString::fromStdString(f->metadata().filePath));
        else if (f && !f->metadata().fileName.empty())
            allPaths.append(QString::fromStdString(f->metadata().fileName));
        else
            allPaths.append(QString());
    }
    const QStringList disambiguated = disambiguateCaptions(allPaths);

    for (int i = 0; i < n; ++i)
    {
        // Image view plus a hidden filename store; visible names use the top overlay.
        auto *cellWidget = new QWidget(m_grid);
        cellWidget->setObjectName(QString("comparePane%1").arg(i));
        cellWidget->setMinimumSize(0, 0);
        cellWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *cellLay = new QVBoxLayout(cellWidget);
        cellLay->setContentsMargins(0, 0, 0, 0);
        cellLay->setSpacing(1);
        mviewer::ui::installComparePaneHeader(cellWidget, cellLay);

        auto *view = new RawImageView(cellWidget);
        view->setProperty("paneHeaderOwnsFilename", true);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        view->setMinimumSize(64, 64);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        view->setCellIndex(i);
        view->setPaneTag(QString(QChar('A' + i)));
        view->setDisplayOverlay(m_displayOverlay);
        view->setPixelGridEnabled(m_pixelGridEnabled);
        cellLay->addWidget(view, 1);
        m_cellViews.push_back(view);
        connect(view, &RawImageView::scaleChanged, this,
                [this, view](double) { scheduleDisplayLodRefresh(view->cellIndex()); });
        connect(view, &RawImageView::transformChanged, this, &CompareWorkspace::positionROIHud);

        // M28 P1-01: panes start BLANK. ImageData -> QImage materialization is
        // one async Analysis batch scheduled below, never a synchronous
        // conversion on the UI thread.
        const ImageFrame *img = m_engine.imageAt(i);
        m_cellHists.push_back(createPaneHistogramOverlay(cellWidget, i, m_paneHistOverlay));

        const QString cellName = img ? QString::fromStdString(img->metadata().fileName) : QString();
        connect(view, &RawImageView::pixelInfo, this,
                [this, cellName, cellIndex = i](int x, int y, int, int, int, bool valid)
                {
                    if (!valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    const ImageFrame *frame = m_engine.imageAt(cellIndex);
                    const CellAdjust adjust =
                        cellIndex >= 0 && cellIndex < static_cast<int>(m_cellAdjusts.size())
                            ? m_cellAdjusts[static_cast<size_t>(cellIndex)]
                            : CellAdjust{};
                    const auto sample =
                        frame ? mviewer::core::sampleAnalysisPixel(frame->pixels(),
                                                                   analysisAdjustment(adjust), x, y)
                              : mviewer::core::AnalysisPixel{};
                    if (!sample.valid)
                    {
                        emit pixelInfo(QString());
                        return;
                    }
                    emit pixelInfo(formatPixelInfo(cellIndex, cellName, x, y, sample));
                    // M30: route the high-frequency hover through the coalescer
                    // so the sync-crosshair pixelInfo + crosshairMoved pair and
                    // rapid hovers render the inspector at most once per turn.
                    if (m_sidePanel && m_sidePanel->isVisible())
                        requestInspectorUpdate(x, y);
                });
        connect(view, &RawImageView::selectionChanged, this,
                [this, view](const mviewer::domain::Selection &sel)
                { applySelectionFromView(view, sel); });
        connect(view, &RawImageView::selectionPreviewChanged, this,
                [this, view](const mviewer::domain::Selection &sel)
                { applySelectionPreviewFromView(view, sel); });
        connect(view, &RawImageView::crosshairMoved, this,
                [this, view](const QPointF &p) { onCrosshairMoved(view, p); });
        connect(view, &RawImageView::focusRequested, this, &CompareWorkspace::onFocusRequested);

        // A-4.3: restore any existing pixel-link markers after rebuild.
        if (!m_linkPoints.isEmpty())
            view->setLinkMarkers(m_linkPoints);

        auto *caption = createPaneCaption(cellWidget, i, img);
        if (i < disambiguated.size() && !disambiguated[i].isEmpty())
            caption->setFullText(disambiguated[i]);
        // Floating overlay (see ComparePaneCaption): deliberately NOT in cellLay.
        m_cellLabels.push_back(caption);
        view->setFilenameOverlay(caption->fullText(), m_filenameOverlay);
        QString headerPath = caption->fullText();
        if (img && !img->metadata().filePath.empty())
            headerPath = QString::fromStdString(img->metadata().filePath);
        mviewer::ui::updateComparePaneHeaderName(cellWidget, caption->fullText(), headerPath,
                                                 m_filenameOverlay);

        const int row = i / columns;
        const int col = i % columns;
        m_layout->addWidget(cellWidget, row, col);
        // Let every cell expand to fill the available area so image panes (not the
        // widgets' minimum size) are what the grid lays out. Without stretch the
        // grid collapses to the cells' minimum size and fitAll() computes a tiny
        // shared scale, leaving compare panes blank or microscopic.
        m_layout->setRowStretch(row, 1);
        m_layout->setColumnStretch(col, 1);
    }
}

CompareWorkspace::SourceDisplayResult
CompareWorkspace::materializeSourceDisplay(const std::string &path, const DisplayRequest &request)
{
    SourceDisplayResult result;
    if (path.empty())
    {
        result.errorText = QStringLiteral("Source path is empty");
        return result;
    }
    const auto source = mviewer::core::SourceImage::open(path);
    if (!source)
    {
        result.errorText = QStringLiteral("Source format not supported or metadata read failed");
        return result;
    }
    result.sourceSize = QSize(source->metadata().width, source->metadata().height);
    if (result.sourceSize.isEmpty())
    {
        result.errorText = QStringLiteral("Source size unavailable");
        return result;
    }

    if (request.region && request.sourceRect.isValid())
    {
        const mviewer::core::SourceRect displayed{request.sourceRect.x(), request.sourceRect.y(),
                                                  request.sourceRect.width(),
                                                  request.sourceRect.height()};
        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
            displayed, source->rawWidth(), source->rawHeight(), source->orientation());
        const auto decoded =
            source->decodeRegion(raw, request.target.width(), request.target.height());
        const mviewer::core::SourceRect displayedCovered = mviewer::core::rawRectToOriented(
            decoded.coveredRect, source->rawWidth(), source->rawHeight(), source->orientation());
        result.coveredRect =
            QRect(displayedCovered.x, displayedCovered.y, displayedCovered.w, displayedCovered.h);
        if (!decoded.ok)
        {
            result.errorText = QStringLiteral("Bounded display decode unavailable");
            return result;
        }
        result.pixels = decoded.pixels;
        result.metadata = decoded.metadata;
        return result;
    }

    const int edge = std::max(request.target.width(), request.target.height());
    const int wantEdge = edge > 0 ? edge : 1024;
    result.coveredRect = QRect(QPoint(0, 0), result.sourceSize);

    // Prefer in-memory CacheManager mip before SourceImage::decodeLod.
    const int sourceEdge = std::max(result.sourceSize.width(), result.sourceSize.height());
    const bool needsDense = !request.provisional && !request.preview;
    ImageData mip = tryCacheBestMip(path, wantEdge);
    if (!mip.isNull())
    {
        const int mipEdge = std::max(mip.width, mip.height);
        // Reject a Browse/Preview-sized cache hit for a much larger viewport
        // request — accepting it paints a sticky nearest-neighbour mosaic.
        // A sharp request has no follow-up pass, so it needs the full density.
        if (wantEdge > 0 && mipEdge * 2 < wantEdge)
            mip = ImageData{};
        else if (needsDense && !mipDenseEnough(mip, wantEdge, sourceEdge))
            mip = ImageData{};
    }
    if (!mip.isNull())
    {
        if (request.target.isValid() &&
            (mip.width > request.target.width() || mip.height > request.target.height()))
        {
            mip = RenderEngine::scaleBoundedStatic(
                mip, RenderSize{request.target.width(), request.target.height()});
        }
        if (!mip.isNull())
        {
            result.pixels = std::move(mip);
            result.metadata = source->metadata();
            return result;
        }
    }

    auto decoded = source->decodeLod(wantEdge);
    // decodeLod serves the same coarse mip first; decode the full frame as a
    // region instead (decodeRegion never reads the mip chain).
    if (needsDense && (!decoded.ok || !mipDenseEnough(decoded.pixels, wantEdge, sourceEdge)))
    {
        // Same contract as the region branch above: raw rect in, target in
        // displayed (oriented) dimensions.
        const double fit = std::min(1.0, static_cast<double>(wantEdge) / sourceEdge);
        const int tw = std::max(1, static_cast<int>(std::lround(result.sourceSize.width() * fit)));
        const int th = std::max(1, static_cast<int>(std::lround(result.sourceSize.height() * fit)));
        const mviewer::core::SourceRect raw = mviewer::core::orientedRectToRaw(
            {0, 0, result.sourceSize.width(), result.sourceSize.height()}, source->rawWidth(),
            source->rawHeight(), source->orientation());
        auto full = source->decodeRegion(raw, tw, th);
        if (full.ok && !full.pixels.isNull())
            decoded = std::move(full);
    }
    if (!decoded.ok)
    {
        result.errorText = QStringLiteral("Bounded display decode unavailable");
        return result;
    }
    result.pixels = decoded.pixels;
    result.metadata = decoded.metadata;
    return result;
}

QSize CompareWorkspace::transformedSourceDims(const QSize &sourceDims,
                                              const CellAdjust &displayAdjust)
{
    const auto crop = mviewer::core::analysisCropBounds(sourceDims.width(), sourceDims.height(),
                                                        analysisAdjustment(displayAdjust));
    const int rot = std::abs(displayAdjust.rotation % 360);
    const bool swap = (rot == 90 || rot == 270);
    return QSize(swap ? crop.height : crop.width, swap ? crop.width : crop.height);
}

void CompareWorkspace::scaleUsmRadiusForDisplay(CellAdjust &adjust, const DisplayRequest &request,
                                                int fallbackSourceWidth, int lodWidth)
{
    if (!adjust.usmEnabled || lodWidth <= 0)
        return;
    const int sourceWidth = (request.region && request.sourceRect.width() > 0)
                                ? request.sourceRect.width()
                                : fallbackSourceWidth;
    if (sourceWidth <= 0 || sourceWidth == lodWidth)
        return;
    adjust.usmRadius *= static_cast<double>(lodWidth) / static_cast<double>(sourceWidth);
}

CompareWorkspace::DisplayBatchResult CompareWorkspace::materializeDisplayBatch(
    const std::vector<ImageData> &pixels,
    const std::vector<mviewer::domain::ImageMetadata> &metadata,
    const std::vector<DisplayRequest> &displayRequests, const std::vector<CellAdjust> &adjusts,
    const std::vector<int> &panes, int paneCount, uint64_t generation,
    const std::vector<std::string> &paths, const mviewer::core::DisplayColorContext &target,
    const TaskScheduler::TaskContext &context)
{
    DisplayBatchResult result;
    result.generation = generation;
    result.paneCount = paneCount;
    result.cells.reserve(panes.size());

    const auto adjustFor = [&adjusts](int idx) -> CellAdjust
    { return idx >= 0 && idx < static_cast<int>(adjusts.size()) ? adjusts[idx] : CellAdjust{}; };
    const auto recordFailure = [&result](int idx, const QString &reason)
    {
        DisplayBatchResult::CellImage cell;
        cell.index = idx;
        cell.errorText = reason;
        result.cells.push_back(std::move(cell));
    };

    for (int idx : panes)
    {
        if (context.isCancelled())
            return {};
        if (idx < 0 || idx >= static_cast<int>(pixels.size()))
            continue;
        const ImageData &src = pixels[static_cast<size_t>(idx)];
        QSize sourceDims;
        mviewer::domain::ImageMetadata convMeta;
        ImageData lod;
        QRect coveredRect;
        const DisplayRequest request = idx < static_cast<int>(displayRequests.size())
                                           ? displayRequests[idx]
                                           : DisplayRequest{};
        if (!src.isNull())
        {
            sourceDims = QSize(src.width, src.height);
            convMeta = metadata[static_cast<size_t>(idx)];
            coveredRect = coveredRectForRequest(sourceDims, request);
            const std::string pathKey = (idx < static_cast<int>(paths.size()))
                                            ? paths[static_cast<size_t>(idx)]
                                            : std::string();
            lod = resolveLodFromCachedFull(src, request, pathKey, context.cancel.get());
        }
        else
        {
            if (idx >= static_cast<int>(paths.size()))
            {
                recordFailure(idx, QStringLiteral("Source path is empty"));
                continue;
            }
            const SourceDisplayResult sourceResult =
                materializeSourceDisplay(paths[static_cast<size_t>(idx)], request);
            if (!sourceResult.errorText.isEmpty())
            {
                recordFailure(idx, sourceResult.errorText);
                continue;
            }
            sourceDims = sourceResult.sourceSize;
            coveredRect = sourceResult.coveredRect;
            lod = sourceResult.pixels;
            convMeta = sourceResult.metadata;
        }
        if (context.isCancelled())
            return {};
        if (lod.isNull())
        {
            recordFailure(idx, QStringLiteral("Display grid is empty"));
            continue;
        }
        if (sourceDims.isEmpty())
            sourceDims = QSize(lod.width, lod.height);
        if (!coveredRect.isValid())
            coveredRect = QRect(QPoint(0, 0), sourceDims);

        CellAdjust displayAdjust = adjustFor(idx);
        if (displayAdjust.hasCrop && !sourceDims.isEmpty())
        {
            const double sx = static_cast<double>(lod.width) / sourceDims.width();
            const double sy = static_cast<double>(lod.height) / sourceDims.height();
            displayAdjust.cropX = static_cast<int>(std::lround(displayAdjust.cropX * sx));
            displayAdjust.cropY = static_cast<int>(std::lround(displayAdjust.cropY * sy));
            displayAdjust.cropW = static_cast<int>(std::lround(displayAdjust.cropW * sx));
            displayAdjust.cropH = static_cast<int>(std::lround(displayAdjust.cropH * sy));
        }
        scaleUsmRadiusForDisplay(displayAdjust, request, sourceDims.width(), lod.width);
        const ImageData adjusted = CompareWorkspace::applyAdjusts(
            lod, displayAdjust, [&context]() { return context.isCancelled(); });
        if (context.isCancelled())
            return {};
        if (adjusted.isNull())
        {
            recordFailure(idx, QStringLiteral("Display adjustment failed"));
            continue;
        }
        DisplayBatchResult::CellImage cell;
        cell.index = idx;
        cell.image = mvcore::toDisplayQImage(adjusted, convMeta, target);
        const bool transformed = displayAdjust.hasCrop || displayAdjust.rotation != 0 ||
                                 displayAdjust.flipH || displayAdjust.flipV;
        if (transformed)
        {
            const QSize transformedDims = transformedSourceDims(sourceDims, displayAdjust);
            cell.sourceSize = transformedDims;
            cell.sourceRect = QRect(QPoint(0, 0), transformedDims);
        }
        else
        {
            cell.sourceSize = sourceDims;
            cell.sourceRect = coveredRect;
        }
        if (context.isCancelled())
            return {};
        if (cell.image.isNull())
        {
            recordFailure(idx, QStringLiteral("Display format conversion failed"));
            continue;
        }
        result.cells.push_back(std::move(cell));
    }
    return result;
}
