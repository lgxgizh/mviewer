// CompareWorkspace rendering: paint modes, diff overlay, blink, histograms (M20 P0#2).
#include "compareworkspace_p.h"

#include "core/image/SourceImage.h"

#include <QResizeEvent>
#include <cmath>

namespace
{
constexpr double kDisplayLodBucketSteps = 16.0;
constexpr double kDisplayLodOverscan = 1.25;
// Ignore sub-4% fit drift (scrollbar, rounding). A resolution or cell-geometry
// change that matters for FOV match is much larger.
constexpr double kStaleFitRatio = 0.04;

double sharedZoomRatio(double ratio)
{
    return (ratio > 0.0 && std::isfinite(ratio)) ? ratio : 1.0;
}

bool withinOne(int have, int want)
{
    const int delta = have - want;
    return delta <= 1 && delta >= -1;
}

QSize fallbackFrameSize(const ImageFrame *img)
{
    if (!img)
        return {};
    if (!img->pixels().isNull() && img->width() > 0 && img->height() > 0)
        return QSize(img->width(), img->height());
    const auto &meta = img->metadata();
    if (meta.width > 0 && meta.height > 0)
        return QSize(meta.width, meta.height);
    return {};
}

} // namespace

// M34: the dedicated compare canvas page. The normal grid scrolls inside
// "compareGridPage"; split/swipe/overlay/checker render on the sibling
// "compareCanvas" widget. A stacked layout shows exactly one page, so canvas
// modes are never obscured by the QScrollArea and hidden RawImageViews never
// own wheel/drag input while a canvas mode is active.
QStackedLayout *CompareWorkspace::buildCanvasPage()
{
    m_compareGridPage = new QWidget(this);
    m_compareGridPage->setObjectName("compareGridPage");
    auto *gridLay = new QVBoxLayout(m_compareGridPage);
    gridLay->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(m_compareGridPage);
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_grid);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    gridLay->addWidget(scroll);

    m_compareCanvas = new QWidget(this);
    m_compareCanvas->setObjectName("compareCanvas");
    m_compareCanvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_compareCanvas->setMinimumSize(64, 64);
    m_compareCanvas->setMouseTracking(true);
    m_compareCanvas->setFocusPolicy(Qt::StrongFocus);
    m_compareCanvas->installEventFilter(this);

    // Keep async Compare startup explicit. A non-modal page avoids showing an
    // empty/black grid while the whole batch is still decoding; the
    // indeterminate bar intentionally does not imply per-pane progress.
    m_compareLoadingPage = new QWidget(this);
    m_compareLoadingPage->setObjectName("compareLoadingPage");
    auto *loadingLay = new QVBoxLayout(m_compareLoadingPage);
    loadingLay->setContentsMargins(24, 24, 24, 24);
    loadingLay->setSpacing(12);
    loadingLay->addStretch(1);
    m_compareLoadingLabel = new QLabel(tr("正在加载比较图片…"), m_compareLoadingPage);
    m_compareLoadingLabel->setObjectName("compareLoadingLabel");
    m_compareLoadingLabel->setAlignment(Qt::AlignCenter);
    loadingLay->addWidget(m_compareLoadingLabel);
    m_compareLoadingProgress = new QProgressBar(m_compareLoadingPage);
    m_compareLoadingProgress->setObjectName("compareLoadingProgress");
    m_compareLoadingProgress->setRange(0, 0);
    m_compareLoadingProgress->setTextVisible(false);
    m_compareLoadingProgress->setFixedWidth(280);
    loadingLay->addWidget(m_compareLoadingProgress, 0, Qt::AlignHCenter);
    loadingLay->addStretch(1);

    m_pageStack = new QStackedLayout;
    m_pageStack->setContentsMargins(0, 0, 0, 0);
    m_pageStack->addWidget(m_compareGridPage);
    m_pageStack->addWidget(m_compareCanvas);
    m_pageStack->addWidget(m_compareLoadingPage);
    m_pageStack->setCurrentWidget(m_compareGridPage);
    return m_pageStack;
}

// Central page transition: canvas modes (split / swipe / overlay / checker) are
// only meaningful for exactly two images and switch the visible page to the
// canvas; any other state (or a non-two-image load) restores the grid page.
void CompareWorkspace::updateCanvasModeVisibility()
{
    // Canvas-mode transitions are semantic presentation changes. A transient
    // A<-B hold must never survive a mode switch into a different renderer.
    endTemporaryCompare();
    if (!m_pageStack || !m_compareCanvas || !m_compareGridPage || !m_compareLoadingPage)
        return;
    // Soft keep-grid: only blank to the loading page when there is no prior
    // pair to keep on screen. Pair navigation keeps the grid/canvas visible.
    if (m_loadInFlight && m_engine.imageCount() == 0)
    {
        m_pageStack->setCurrentWidget(m_compareLoadingPage);
        update();
        return;
    }
    if (m_engine.imageCount() == 0)
    {
        if (m_compareLoadingProgress)
            m_compareLoadingProgress->setVisible(false);
        m_pageStack->setCurrentWidget(m_compareLoadingPage);
        update();
        return;
    }
    const bool canvasMode = m_engine.imageCount() == 2 && anyCanvasCompareMode();
    QWidget *target = canvasMode ? static_cast<QWidget *>(m_compareCanvas)
                                 : static_cast<QWidget *>(m_compareGridPage);
    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    const bool switchedToGrid =
        !canvasMode && !blinkActive && m_pageStack->currentWidget() != target;
    m_pageStack->setCurrentWidget(target);
    if (switchedToGrid)
        schedulePostLayoutFit();
    if (canvasMode)
        m_compareCanvas->update();
    updateROISurfaces();
    updateTemporaryCompareAvailability();
    update();
}

QRect CompareWorkspace::canvasRect() const
{
    return m_compareCanvas ? m_compareCanvas->rect() : rect();
}

// M34: deterministic split geometry contract. The two rects are adjacent and
// cover the whole canvas exactly: left = [0, midX), right = [midX, width). For
// an even width the halves are equal; for an odd width they differ by at most
// one column. No state — drawSplitCompare and the workflow test share it.
QPair<QRect, QRect> CompareWorkspace::splitRects(const QRect &canvas)
{
    const int midX = canvas.width() / 2;
    const QRect left(canvas.left(), canvas.top(), midX, canvas.height());
    const QRect right(canvas.left() + midX, canvas.top(), canvas.width() - midX, canvas.height());
    return {left, right};
}

// Center-relative destination helper: image center = target rect center + the
// selected transform offset; top-left = center - scaled image size / 2. This
// matches RawImageView's offset semantics (a pan delta from the pane center).
QRectF CompareWorkspace::cellFullDestRect(int idx, const QRectF &geom) const
{
    if (idx < 0 || idx >= m_cellViews.size() || !m_cellViews[idx])
        return {};
    const QImage &img = m_cellViews[idx]->image();
    const QSize sourceSize = m_cellViews[idx]->sourceSize();
    if (img.isNull() || !sourceSize.isValid() || geom.isEmpty())
        return {};
    const auto &ct = m_engine.cellTransform(idx);
    double sc = ct.scale;
    if (!m_uniformScale && m_syncZoom)
    {
        const double canvasFit =
            std::min(geom.width() / sourceSize.width(), geom.height() / sourceSize.height());
        sc = canvasFit * m_sharedZoomRatio;
    }
    if (!(sc > 0.0) || !std::isfinite(sc))
        return {};
    const double ox = m_syncDrag ? m_engine.syncTransform().offset.x : ct.offset.x;
    const double oy = m_syncDrag ? m_engine.syncTransform().offset.y : ct.offset.y;
    const double dw = sourceSize.width() * sc;
    const double dh = sourceSize.height() * sc;
    const QPointF center(geom.x() + geom.width() / 2.0 + ox, geom.y() + geom.height() / 2.0 + oy);
    return QRectF(center.x() - dw / 2.0, center.y() - dh / 2.0, dw, dh);
}
QRectF CompareWorkspace::cellDestRect(int idx, const QRectF &geom) const
{
    const QRectF full = cellFullDestRect(idx, geom);
    if (full.isEmpty() || idx < 0 || idx >= m_cellViews.size() || !m_cellViews[idx])
        return {};
    const QRect sourceRect = m_cellViews[idx]->sourceRect();
    const QSize sourceSize = m_cellViews[idx]->sourceSize();
    if (!sourceRect.isValid() || !sourceSize.isValid())
        return {};
    const double sx = full.width() / sourceSize.width();
    const double sy = full.height() / sourceSize.height();
    return QRectF(full.left() + sourceRect.x() * sx, full.top() + sourceRect.y() * sy,
                  sourceRect.width() * sx, sourceRect.height() * sy);
}

QRect CompareWorkspace::sourceVisibleRect(int pane) const
{
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
        return {};
    const RawImageView *view = m_cellViews[pane];
    const QSize sourceSize = view->sourceSize();
    if (!sourceSize.isValid() || view->width() <= 0 || view->height() <= 0)
        return {};
    const QPointF a = view->widgetToImage(QPoint(0, 0));
    const QPointF b = view->widgetToImage(QPoint(view->width(), view->height()));
    const QRect full(QPoint(0, 0), sourceSize);
    const int left = qFloor(std::min(a.x(), b.x()));
    const int top = qFloor(std::min(a.y(), b.y()));
    const int right = qCeil(std::max(a.x(), b.x()));
    const int bottom = qCeil(std::max(a.y(), b.y()));
    return QRect(left, top, std::max(1, right - left), std::max(1, bottom - top)).intersected(full);
}

mviewer::ui::CompareDisplayPlanningInput CompareWorkspace::displayPlanningInput(int pane) const
{
    mviewer::ui::CompareDisplayPlanningInput input;
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
        return input;
    const RawImageView *view = m_cellViews[pane];
    const QSize viewSourceSize = view->sourceSize();
    QSize sourceSize = viewSourceSize;
    // The first materialization is scheduled while the placeholder pane is
    // still blank. Carry the probed metadata dimensions through that request
    // so it is an explicit full-frame LOD (and never a partial region based on
    // a stale engine transform).
    if (!sourceSize.isValid() && pane < m_engine.imageCount())
    {
        const ImageFrame *img = m_engine.imageAt(pane);
        if (img)
        {
            const auto &meta = img->metadata();
            sourceSize = QSize(meta.width, meta.height);
        }
    }
    if (!sourceSize.isValid())
        return input;

    input.pane = pane;
    input.sourceWidth = sourceSize.width();
    input.sourceHeight = sourceSize.height();
    input.viewportWidth = view->width();
    input.viewportHeight = view->height();
    input.devicePixelRatio = view->devicePixelRatioF();
    input.hasWidgetSourceSize = viewSourceSize.isValid();
    input.uniformScale = m_uniformScale;
    input.fitScales.reserve(static_cast<size_t>(m_fitScales.size()));
    for (const double fit : m_fitScales)
        input.fitScales.push_back(fit);
    input.currentScale = view->scale();
    input.paneScale = input.currentScale;
    if (pane < m_engine.imageCount())
    {
        input.currentScale = m_engine.cellTransform(pane).scale;
        input.paneScale = input.currentScale;
    }
    if (pane < static_cast<int>(m_cellAdjusts.size()))
    {
        const auto &adjust = m_cellAdjusts[static_cast<size_t>(pane)];
        input.hasCropOrRotation =
            adjust.hasCrop || adjust.rotation != 0 || adjust.flipH || adjust.flipV;
    }
    const QRect visible = sourceVisibleRect(pane);
    input.visibleSourceRect = {visible.x(), visible.y(), visible.width(), visible.height()};
    return input;
}

CompareWorkspace::DisplayRequest CompareWorkspace::sourceDisplayRequest(int pane) const
{
    const mviewer::ui::CompareDisplayPlanningInput input = displayPlanningInput(pane);
    if (input.sourceWidth <= 0 || input.sourceHeight <= 0)
        return {};
    const mviewer::ui::CompareDisplayPlan plan = mviewer::ui::planCompareDisplay(input);
    if (!plan.isValid())
        return {};
    return {{plan.targetWidth, plan.targetHeight},
            {plan.sourceRect.x, plan.sourceRect.y, plan.sourceRect.width, plan.sourceRect.height},
            plan.region};
}

CompareWorkspace::DisplayRequest CompareWorkspace::memoryDisplayRequest(int pane, bool blank) const
{
    if (pane < 0 || pane >= m_engine.imageCount())
        return {};
    const ImageFrame *frame = m_engine.imageAt(pane);
    if (!frame || frame->pixels().isNull())
        return {};

    DisplayRequest req;
    const mviewer::ui::CompareDisplayPlanningInput input = displayPlanningInput(pane);
    const bool sharp = input.hasWidgetSourceSize && !input.hasCropOrRotation &&
                       input.sourceWidth > 0 && input.sourceHeight > 0;
    if (sharp)
    {
        const mviewer::ui::CompareDisplayPlan plan = mviewer::ui::planSharpCompareDisplay(input);
        if (plan.isValid())
        {
            req.target = QSize(plan.targetWidth, plan.targetHeight);
            req.sourceRect = QRect(plan.sourceRect.x, plan.sourceRect.y, plan.sourceRect.width,
                                   plan.sourceRect.height);
            req.region = plan.region;
        }
    }
    if (!req.target.isValid())
    {
        req.target = displayLodTarget(pane, frame->pixels());
        req.sourceRect = QRect(0, 0, frame->pixels().width, frame->pixels().height);
        req.region = false;
    }
    if (!req.target.isValid() || !blank)
        return req;

    const int longEdge = (std::max)(req.target.width(), req.target.height());
    const int edge = (std::min)(640, longEdge);
    if (edge <= 0 || edge >= longEdge)
        return req;
    req.target = QSize(edge, edge);
    req.sourceRect = QRect(0, 0, frame->pixels().width, frame->pixels().height);
    req.region = false;
    req.provisional = true;
    return req;
}

bool CompareWorkspace::rasterCoversView(int pane, const DisplayRequest &desired) const
{
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane] || !desired.target.isValid())
        return false;
    const RawImageView *view = m_cellViews[pane];
    const QImage &image = view->image();
    const QRect current = view->sourceRect();
    if (image.isNull() || !current.isValid())
        return false;
    if (!withinOne(image.width(), desired.target.width()) ||
        !withinOne(image.height(), desired.target.height()))
        return false;
    if (!withinOne(current.width(), desired.sourceRect.width()) ||
        !withinOne(current.height(), desired.sourceRect.height()))
        return false;
    const QRect visible = sourceVisibleRect(pane);
    return !visible.isValid() || current.contains(visible);
}

QSize CompareWorkspace::displayLodTarget(int idx, const ImageData &source) const
{
    if (source.isNull() || idx < 0 || idx >= m_cellViews.size() || !m_cellViews[idx])
        return {};
    const QSize viewport = m_cellViews[idx]->size();
    if (!viewport.isValid() || viewport.width() <= 0 || viewport.height() <= 0)
        return QSize(source.width, source.height);

    const double dpr = std::max(1.0, m_cellViews[idx]->devicePixelRatioF());
    const double logicalFit = std::min(static_cast<double>(viewport.width()) / source.width,
                                       static_cast<double>(viewport.height()) / source.height);
    const double physicalFit = logicalFit * dpr;
    double requested = logicalFit;
    if (idx < m_engine.imageCount())
        requested = std::max(requested, m_engine.cellTransform(idx).scale);
    if (!m_cellViews[idx]->image().isNull())
        requested = std::max(requested, m_cellViews[idx]->scale());
    if (!(requested > 0.0) || !std::isfinite(requested))
        requested = logicalFit;
    // Round the requested source-pixel density to a stable bucket. This keeps
    // wheel bursts from producing a new raster size on every fractional tick,
    // while DPR keeps the Fit raster dense enough for the physical viewport.
    const double bucketed =
        std::ceil(requested * dpr * kDisplayLodOverscan * kDisplayLodBucketSteps) /
        kDisplayLodBucketSteps;
    // Never upscale a source that already fits inside the viewport. Avoid
    // passing an inverted [physicalFit, 1.0] interval to std::clamp for thumbnails.
    const double factor =
        physicalFit >= 1.0 ? 1.0 : std::clamp(std::max(physicalFit, bucketed), physicalFit, 1.0);
    return QSize(std::max(1, static_cast<int>(std::ceil(source.width * factor))),
                 std::max(1, static_cast<int>(std::ceil(source.height * factor))));
}

void CompareWorkspace::fitAll()
{
    double sharedScale = 1.0;
    bool first = true;
    const int n = m_engine.imageCount();
    m_fitScales.fill(1.0, n);
    m_sharedZoomRatio = 1.0;
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const ImageFrame *img = m_engine.imageAt(i);
        const QSize qs = m_cellViews[i]->size();
        const CellSize cell{qs.width(), qs.height()};
        if (!img || cell.w <= 0 || cell.h <= 0)
            continue;
        QSize sourceSize;
        if (!img->pixels().isNull())
        {
            // Preserve the full-frame path's existing source geometry.
            sourceSize = QSize(img->width(), img->height());
        }
        else
        {
            // M47 placeholder: the pane has no pixels yet, but its probed
            // metadata is authoritative for the fit transform. Once the LOD
            // arrives, sourceSize() is an equivalent fallback for adjusted
            // display geometry.
            sourceSize = m_cellViews[i]->sourceSize();
            if (!sourceSize.isValid())
            {
                const auto &meta = img->metadata();
                sourceSize = QSize(meta.width, meta.height);
            }
        }
        if (!sourceSize.isValid())
            continue;
        const CellSize imgSize{sourceSize.width(), sourceSize.height()};
        m_engine.fitCell(i, cell, imgSize);
        m_fitScales[i] = m_engine.cellScale(i);
        if (first || m_engine.cellScale(i) < sharedScale)
            sharedScale = m_engine.cellScale(i);
        first = false;
    }
    if (first)
        return;

    m_engine.setScale(m_sharedZoomRatio);
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const double effective = m_uniformScale ? sharedScale : m_fitScales.value(i, 1.0);
        m_engine.setCellScale(i, effective);
    }
    if (m_uniformScale || m_syncDrag)
    {
        m_engine.setOffset(0.0, 0.0);
        for (int i = 0; i < n; ++i)
            if (i < m_cellViews.size() && m_cellViews[i])
                m_engine.setCellOffset(i, 0.0, 0.0);
    }
}

void CompareWorkspace::zoomActual()
{
    const double currentScale = m_engine.cellTransform(0).scale;
    if (currentScale > 0.0)
    {
        applyAnchorZoom(0, 0.0, 0.0, 1.0 / currentScale);
        showCompareStatus(tr("视图缩放: 100% (原始大小)"));
        if (m_compareCanvas)
            m_compareCanvas->update();
        update();
    }
}

double CompareWorkspace::scaleForPaneSource(int pane, int sourceW, int sourceH) const
{
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
        return 0.0;
    if (sourceW <= 0 || sourceH <= 0)
        return 0.0;
    const QSize viewSize = m_cellViews[pane]->size();
    if (viewSize.width() <= 0 || viewSize.height() <= 0)
        return 0.0;
    const double fit = std::min(static_cast<double>(viewSize.width()) / sourceW,
                                static_cast<double>(viewSize.height()) / sourceH);
    if (!(fit > 0.0) || !std::isfinite(fit))
        return 0.0;
    if (m_uniformScale)
    {
        const double cell = pane < m_engine.imageCount() ? m_engine.cellTransform(pane).scale : fit;
        return (cell > 0.0 && std::isfinite(cell)) ? cell : fit;
    }
    return fit * sharedZoomRatio(m_sharedZoomRatio);
}

double CompareWorkspace::scaleKeepingFit(int pane, double engineScale)
{
    if (m_uniformScale || !m_syncZoom)
        return engineScale;
    if (pane < 0 || pane >= m_cellViews.size() || !m_cellViews[pane])
        return engineScale;
    RawImageView *view = m_cellViews[pane];
    if (view->hasTransientDisplay() || !view->isVisible())
        return engineScale;
    if (pane >= m_fitScales.size() || !(m_fitScales[pane] > 0.0))
        return engineScale;
    const QSize src = view->sourceSize();
    const QSize vs = view->size();
    if (src.width() <= 0 || src.height() <= 0 || vs.width() <= 32 || vs.height() <= 32)
        return engineScale;
    const double fitNow = std::min(static_cast<double>(vs.width()) / src.width(),
                                   static_cast<double>(vs.height()) / src.height());
    if (!(fitNow > 0.0) || !std::isfinite(fitNow))
        return engineScale;
    const double rel = fitNow / m_fitScales[pane];
    if (!std::isfinite(rel) || std::abs(rel - 1.0) <= kStaleFitRatio)
        return engineScale;
    const double scale = fitNow * sharedZoomRatio(m_sharedZoomRatio);
    m_fitScales[pane] = fitNow;
    m_engine.setCellScale(pane, scale);
    return scale;
}

void CompareWorkspace::reapplyFitPreservingRatio()
{
    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    if (!m_syncZoom && !blinkActive)
        return;
    const int n = m_engine.imageCount();
    if (n <= 0)
        return;
    if (m_fitScales.size() < n)
        m_fitScales.resize(n, 1.0);
    const double ratio = sharedZoomRatio(m_sharedZoomRatio);
    double sharedFit = 0.0;
    QVector<int> live;
    live.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i] || !m_cellViews[i]->isVisible())
            continue;
        RawImageView *view = m_cellViews[i];
        const QSize vs = view->size();
        QSize src = view->sourceSize();
        if (src.width() <= 0 || src.height() <= 0)
            src = fallbackFrameSize(m_engine.imageAt(i));
        if (src.width() <= 0 || src.height() <= 0 || vs.width() <= 0 || vs.height() <= 0)
            continue;
        const double fit = std::min(static_cast<double>(vs.width()) / src.width(),
                                    static_cast<double>(vs.height()) / src.height());
        if (!(fit > 0.0) || !std::isfinite(fit))
            continue;
        m_fitScales[i] = fit;
        if (!(sharedFit > 0.0) || fit < sharedFit)
            sharedFit = fit;
        live.push_back(i);
    }
    if (live.isEmpty() || !(sharedFit > 0.0))
        return;
    for (int i : live)
    {
        // Unsynced blink has no shared ratio; fit the stretched cell on its own.
        const double base =
            (!m_syncZoom) ? m_fitScales.at(i) : (m_uniformScale ? sharedFit : m_fitScales.at(i));
        const double applied = m_syncZoom ? ratio : 1.0;
        m_engine.setCellScale(i, base * applied);
    }
    update();
}

void CompareWorkspace::schedulePreserveFit()
{
    if (m_preserveFitPending)
        return;
    m_preserveFitPending = true;
    QPointer<CompareWorkspace> guard(this);
    QTimer::singleShot(0, this,
                       [guard]()
                       {
                           CompareWorkspace *ws = guard.data();
                           if (!ws)
                               return;
                           ws->m_preserveFitPending = false;
                           if (!(ws->m_blinkChk && ws->m_blinkChk->isChecked()))
                               return;
                           ws->reapplyFitPreservingRatio();
                       });
}

void CompareWorkspace::schedulePostLayoutFit()
{
    if (m_postLayoutFitPending)
    {
        if (m_session)
            m_session->postLayoutFitAgain = true;
        return;
    }
    m_postLayoutFitPending = true;
    const uint64_t fitEpoch = m_session ? m_session->cellTransformEpoch : 0;
    QPointer<CompareWorkspace> guard(this);
    QTimer::singleShot(
        0, this,
        [guard, fitEpoch]()
        {
            CompareWorkspace *ws = guard.data();
            if (!ws)
                return;
            ws->m_postLayoutFitPending = false;
            const bool again = ws->m_session && ws->m_session->postLayoutFitAgain;
            if (ws->m_session)
                ws->m_session->postLayoutFitAgain = false;
            // A swap committed per-pane scale and pan after this fit was
            // queued. Fitting now would put both panes back at window-fit.
            if (ws->m_session && ws->m_session->cellTransformEpoch != fitEpoch)
            {
                if (again)
                    ws->schedulePostLayoutFit();
                return;
            }
            // A canvas toggle can cancel Blink before its
            // 0-ms fit callback runs. In that transition the
            // grid is already hidden, so fitting it here would
            // capture transient/invalid pane geometry. The
            // Canvas -> Grid transition above queues a fresh
            // fit after the grid becomes current again.
            const bool blinkActive = ws->m_blinkChk && ws->m_blinkChk->isChecked();
            if (blinkActive || (ws->m_pageStack && ws->m_compareGridPage &&
                                ws->m_pageStack->currentWidget() != ws->m_compareGridPage))
                return;
            const double requestedScale = ws->m_engine.syncTransform().scale;
            const double requestedRatio = ws->m_sharedZoomRatio;
            const bool preserveZoom =
                (std::isfinite(requestedScale) && std::abs(requestedScale - 1.0) > 1e-9) ||
                (std::isfinite(requestedRatio) && std::abs(requestedRatio - 1.0) > 1e-9);
            ws->fitAll();
            if (preserveZoom)
            {
                const double ratio =
                    std::abs(requestedRatio - 1.0) > 1e-9 ? requestedRatio : requestedScale;
                double commonFit = 1.0;
                bool firstFit = true;
                for (double fit : ws->m_fitScales)
                {
                    if (firstFit || fit < commonFit)
                        commonFit = fit;
                    firstFit = false;
                }
                ws->m_sharedZoomRatio = ratio;
                ws->m_engine.setScale(ratio);
                for (int i = 0; i < ws->m_engine.imageCount(); ++i)
                {
                    if (i >= ws->m_cellViews.size() || !ws->m_cellViews[i])
                        continue;
                    const double fit =
                        ws->m_uniformScale ? commonFit : ws->m_fitScales.value(i, 1.0);
                    ws->m_engine.setCellScale(i, fit * ratio);
                }
            }
            ws->scheduleDisplayLodRefresh();
            ws->update();
        });
}

void CompareWorkspace::scheduleDisplayLodRefresh(int idx)
{
    // Keep the latest pane request while the debounce timer is pending. This
    // matters when independent-pane zoom switches panes faster than the timer.
    // Do NOT mark interaction-busy here: setImage/resetFit emits scaleChanged
    // during materialization and would defer hist/diff after rebuildCells.
    m_displayLodRefreshPane = idx;
    if (m_displayLodRefreshPending)
        return;
    m_displayLodRefreshPending = true;
    QPointer<CompareWorkspace> guard(this);
    QTimer::singleShot(
        70, this,
        [guard]()
        {
            CompareWorkspace *ws = guard.data();
            if (!ws)
                return;
            ws->m_displayLodRefreshPending = false;
            // A hidden session already dropped its display batch. Do not
            // submit another HQ job after close/teardown.
            if (!ws->isVisible())
                return;
            const int requestedPane = ws->m_displayLodRefreshPane;
            ws->m_displayLodRefreshPane = -1;
            std::vector<int> dirty;
            auto addIfNeeded = [&dirty, ws](int pane)
            {
                if (pane < 0 || pane >= ws->m_cellViews.size() || !ws->m_cellViews[pane])
                    return;
                const ImageFrame *frame = ws->m_engine.imageAt(pane);
                if (!frame || frame->pixels().isNull())
                {
                    // M47: source-backed pane (no full frame —
                    // e.g. an infeasible source): refresh when
                    // its LOD raster is stale (zoom/pan/scale).
                    if (pane < static_cast<int>(ws->m_comparePaths.size()) &&
                        !ws->m_comparePaths[static_cast<size_t>(pane)].empty())
                    {
                        const RawImageView *view = ws->m_cellViews[pane];
                        const DisplayRequest desired = ws->sourceDisplayRequest(pane);
                        const QRect current = view->sourceRect();
                        const bool hasRaster = !view->image().isNull();
                        bool stale = !hasRaster || !current.isValid();
                        if (!stale && desired.region)
                        {
                            const QRect visible = ws->sourceVisibleRect(pane);
                            const double currentDensity =
                                static_cast<double>(view->image().width()) /
                                std::max(1, current.width());
                            const double requiredDensity =
                                static_cast<double>(desired.target.width()) /
                                std::max(1, desired.sourceRect.width());
                            stale = !current.contains(desired.sourceRect) ||
                                    currentDensity < requiredDensity * 0.9 ||
                                    !current.contains(visible);
                        }
                        else if (!stale)
                        {
                            stale = desired.region ||
                                    std::max(view->image().width(), view->image().height()) !=
                                        std::max(desired.target.width(), desired.target.height()) ||
                                    current != desired.sourceRect;
                        }
                        if (stale)
                            dirty.push_back(pane);
                    }
                    return;
                }
                const DisplayRequest desired = ws->memoryDisplayRequest(pane, false);
                if (!ws->rasterCoversView(pane, desired))
                    dirty.push_back(pane);
            };
            if (requestedPane >= 0 && !ws->m_syncZoom)
                addIfNeeded(requestedPane);
            else
            {
                dirty.reserve(ws->m_cellViews.size());
                for (int i = 0; i < ws->m_cellViews.size(); ++i)
                    addIfNeeded(i);
            }
            if (!dirty.empty())
                ws->scheduleDisplayMaterialization(dirty);
        });
}
