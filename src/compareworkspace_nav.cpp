// CompareWorkspace navigation & session: pair navigation, layout presets, session apply (M20 P0#2).
#include "compareworkspace_p.h"

#include <limits>

namespace
{
void enableWidget(QWidget *widget, bool on)
{
    if (widget)
        widget->setEnabled(on);
}

void checkBox(QCheckBox *button, bool on)
{
    if (button)
        button->setChecked(on);
}

void applyCellTransforms(CompareEngine &engine, const mviewer::domain::CompareSession &s,
                         bool syncOn, const QVector<double> &fitScales, double sharedZoomRatio)
{
    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        const int idx = static_cast<int>(i);
        if (idx >= engine.imageCount())
            break;
        engine.setCellScale(idx, s.cells[i].scale);
        engine.setCellOffset(idx, s.cells[i].offsetX, s.cells[i].offsetY);
    }
    if (syncOn && !s.uniformScale && fitScales.size() == engine.imageCount())
    {
        for (int i = 0; i < engine.imageCount(); ++i)
        {
            const double m = (static_cast<size_t>(i) < s.cells.size() &&
                              std::isfinite(s.cells[static_cast<size_t>(i)].localZoomMultiplier) &&
                              s.cells[static_cast<size_t>(i)].localZoomMultiplier > 0.0)
                                 ? s.cells[static_cast<size_t>(i)].localZoomMultiplier
                                 : 1.0;
            engine.setCellScale(i, fitScales[i] * sharedZoomRatio * m);
        }
    }
}

void applySessionToComponents(mviewer::ui::CompareSessionState *sessionState,
                              mviewer::ui::CompareDiffRenderer *diffRenderer,
                              const mviewer::domain::CompareSession &s)
{
    if (diffRenderer)
        diffRenderer->setThreshold(s.threshold);
    if (sessionState)
    {
        sessionState->setSyncEnabled(s.syncMode != mviewer::domain::SyncMode::Off);
        sessionState->setSharedScale(s.sharedScale);
        sessionState->setSharedOffset(QPointF(s.sharedOffsetX, s.sharedOffsetY));
        sessionState->setLayoutPreset(s.layoutIndex);
        sessionState->setCurrentROI(
            mviewer::domain::Selection{s.selection.x, s.selection.y, s.selection.w, s.selection.h});
    }
}
} // namespace

const mviewer::core::DisplayColorContext &CompareWorkspace::displayColorContext() const
{
    return m_displayColorTarget;
}

CompareEngine &CompareWorkspace::engine()
{
    return m_engine;
}

mviewer::domain::Selection CompareWorkspace::currentROI() const
{
    if (m_roiController && m_roiController->hasValidROI())
        return m_roiController->currentROI();
    return m_lastSelection;
}

void CompareWorkspace::applyROI(const mviewer::domain::Selection &sel)
{
    applySelectionToAll(sel);
}

mviewer::domain::CompareSession CompareWorkspace::compareSession() const
{
    mviewer::domain::CompareSession s = m_engine.session();
    if (m_syncZoom && m_syncDrag)
        s.syncMode = mviewer::domain::SyncMode::All;
    else if (m_syncZoom)
        s.syncMode = mviewer::domain::SyncMode::Zoom;
    else if (m_syncDrag)
        s.syncMode = mviewer::domain::SyncMode::Drag;
    else
        s.syncMode = mviewer::domain::SyncMode::Off;
    s.threshold = m_thresholdValue;
    s.blinkIntervalMs = d->blinkTimer ? d->blinkTimer->interval() : 150;
    s.sidePanelVisible = d->sideChk ? d->sideChk->isChecked() : false;
    s.layoutIndex = d->layoutCombo ? d->layoutCombo->currentIndex() : 0;
    s.customColumns = d->gridColsSpin ? d->gridColsSpin->value() : 2;
    s.uniformScale = m_uniformScale; // H5
    for (size_t i = 0; i < s.cells.size(); ++i)
        s.cells[i].localZoomMultiplier = localZoomMultiplier(static_cast<int>(i));
    return s;
}

int CompareWorkspace::comparedImageCount() const
{
    return m_engine.imageCount();
}

mviewer::OverlayMode CompareWorkspace::overlayMode() const
{
    return m_displayOverlay;
}

bool CompareWorkspace::filenameOverlayVisible() const
{
    return m_filenameOverlay;
}

int CompareWorkspace::navWindow() const
{
    return m_navWindow;
}

bool CompareWorkspace::isSplitOrSwipe() const
{
    return (d->splitChk && d->splitChk->isChecked()) || (m_swipeChk && m_swipeChk->isChecked());
}

bool CompareWorkspace::anyCanvasCompareMode() const
{
    return isSplitOrSwipe() || (d->overlayChk && d->overlayChk->isChecked()) ||
           (d->checkerChk && d->checkerChk->isChecked());
}

int CompareWorkspace::diffBaseIndex() const
{
    return m_focusIndex >= 0 ? m_focusIndex : 0;
}

// A-4.5 / M20: continuous compare — walk a sliding window over the pool.
void CompareWorkspace::setImagePool(const QStringList &allPaths, const QStringList &currentWindow)
{
    m_imagePool = allPaths;
    m_pairIndex = 0;
    if (m_sessionState)
        m_sessionState->setImagePool(allPaths, currentWindow);
    if (!currentWindow.isEmpty())
    {
        // MainWindow seeds the pool before the deferred setImages() call, so
        // use the selected window explicitly instead of waiting for an engine
        // count that is not populated yet.
        const int idx = m_imagePool.indexOf(currentWindow.first());
        if (idx >= 0)
            m_pairIndex = idx;

        const int n = currentWindow.size();
        if (n == 2 || n == 4 || n == 8)
            m_navWindow = n;
    }
    else
    {
        // Preserve the legacy one-argument behavior for callers that seed a
        // pool around an already-loaded compare set.
        if (!m_imagePool.isEmpty() && m_engine.imageCount() > 0)
        {
            const QString first = comparedImages().value(0);
            const int idx = m_imagePool.indexOf(first);
            if (idx >= 0)
                m_pairIndex = idx;
        }
        const int n = m_engine.imageCount();
        if (n == 2 || n == 4 || n == 8)
            m_navWindow = n;
    }
}

void CompareWorkspace::setNavWindow(int n)
{
    if (n != 2 && n != 4 && n != 8)
        n = 2;
    m_navWindow = n;
    if (m_sessionState)
        m_sessionState->setNavWindow(n);
}

void CompareWorkspace::updateActionAvailability()
{
    const int count = m_engine.imageCount();
    const bool loading = m_loadInFlight;
    const bool hasImage = !loading && count > 0;
    const bool hasPair = !loading && count == 2;
    const bool hasSet = !loading && count >= 2;

    if (!loading && !hasSet)
    {
        // A failed or partial batch must not leave analysis toggles visibly
        // active after the panes they refer to have disappeared.
        checkBox(d->diffOverlayChk, false);
        checkBox(d->diffHighlightChk, false);
        checkBox(d->pixelLinkChk, false);
        m_linkPoints.clear();
        refreshLinkMarkers();
        updateLinkInfo();
    }

    // These controls operate on the rendered comparison, not on a pending
    // request. Keeping one gate here prevents a stale successful pair from
    // making the new loading/empty state look interactive.
    const bool overlayOn = hasPair && d->overlayChk && d->overlayChk->isChecked();
    const bool linksOn =
        hasPair && d->pixelLinkChk && d->pixelLinkChk->isChecked() && !m_linkPoints.isEmpty();
    enableWidget(d->blinkChk, hasPair);
    enableWidget(d->splitChk, hasPair);
    enableWidget(m_swipeChk, hasPair);
    enableWidget(d->overlayChk, hasPair);
    enableWidget(d->checkerChk, hasPair);
    enableWidget(d->overlayAlphaSlider, overlayOn);
    // Threshold / gain stay gated by syncContextualCompareControls (diff overlay on).
    enableWidget(d->diffOverlayChk, hasSet);
    enableWidget(d->diffHighlightChk, hasSet);
    enableWidget(d->pixelLinkChk, hasPair);
    enableWidget(d->clearLinksBtn, linksOn);
    enableWidget(d->analyzeBtn, hasImage);
    enableWidget(d->exportReportBtn, hasSet);
    enableWidget(d->swapBtn, hasSet);
    enableWidget(d->autoAlignChk, hasSet);
    refreshCompareControlTooltips();
    syncContextualCompareControls();
}

CompareWorkspace::NavState CompareWorkspace::captureNavState() const
{
    NavState s;
    s.blink = d->blinkChk && d->blinkChk->isChecked();
    s.split = d->splitChk && d->splitChk->isChecked();
    s.swipe = m_swipeChk && m_swipeChk->isChecked();
    s.overlay = d->overlayChk && d->overlayChk->isChecked();
    s.checker = d->checkerChk && d->checkerChk->isChecked(); // M23
    s.checkerSize = m_checkerSize;                         // M23
    s.diffHighlight = d->diffHighlightChk && d->diffHighlightChk->isChecked();
    s.diffOverlay = m_diffOverlayVisible;
    s.diffOverlayPromoted = m_diffOverlayPromoted;
    s.diffGainIndex = d->diffGainCombo ? d->diffGainCombo->currentIndex() : 0;
    s.syncZoom = d->syncZoomChk ? d->syncZoomChk->isChecked() : true;
    s.syncDrag = d->syncDragChk ? d->syncDragChk->isChecked() : true;
    s.syncRotate = m_syncRotate;
    s.crosshair = d->crosshairChk && d->crosshairChk->isChecked();
    s.pixelLink = d->pixelLinkChk && d->pixelLinkChk->isChecked();
    s.filenameOverlay = m_filenameOverlay;
    s.overlayMode = static_cast<int>(m_displayOverlay);
    s.overlayAlpha = m_overlayAlpha;
    s.threshold = m_thresholdValue;
    s.layoutIndex = d->layoutCombo ? d->layoutCombo->currentIndex() : 0;
    s.roi = m_lastSelection;
    s.hasRoi = m_roiLinked && m_lastSelection.width > 0 && m_lastSelection.height > 0;
    return s;
}

void CompareWorkspace::restoreNavState(const NavState &s)
{
    if (d->syncZoomChk)
        d->syncZoomChk->setChecked(s.syncZoom);
    if (d->syncDragChk)
        d->syncDragChk->setChecked(s.syncDrag);
    setSyncRotate(s.syncRotate);
    if (d->crosshairChk)
        d->crosshairChk->setChecked(s.crosshair);
    if (d->pixelLinkChk)
        d->pixelLinkChk->setChecked(s.pixelLink);
    m_filenameOverlay = s.filenameOverlay;
    if (d->filenameOverlayChk)
        d->filenameOverlayChk->setChecked(s.filenameOverlay);
    applyFilenameOverlays();
    setOverlayMode(static_cast<mviewer::OverlayMode>(s.overlayMode));
    // Restore heatmap + highlight without going through the promote/demote
    // toggled handlers (those would fight each other and lose a manual
    // "Show Difference" that was never promoted by H).
    m_diffOverlayPromoted = s.diffOverlayPromoted;
    m_diffOverlayVisible = s.diffOverlay;
    m_diffHighlight = s.diffHighlight;
    if (d->diffOverlayChk)
    {
        const QSignalBlocker blocker(d->diffOverlayChk);
        d->diffOverlayChk->setChecked(s.diffOverlay);
    }
    if (d->diffHighlightChk)
    {
        const QSignalBlocker blocker(d->diffHighlightChk);
        d->diffHighlightChk->setChecked(s.diffHighlight);
    }
    if (d->diffGainCombo && s.diffGainIndex >= 0 && s.diffGainIndex < d->diffGainCombo->count())
        d->diffGainCombo->setCurrentIndex(s.diffGainIndex);
    refreshAllDiffOverlays();
    m_overlayAlpha = s.overlayAlpha;
    if (d->overlayAlphaSlider)
        d->overlayAlphaSlider->setValue(s.overlayAlpha);
    m_thresholdValue = s.threshold;
    if (d->thresholdSlider)
        d->thresholdSlider->setValue(s.threshold);
    if (d->layoutCombo && s.layoutIndex >= 0 && s.layoutIndex < d->layoutCombo->count())
        d->layoutCombo->setCurrentIndex(s.layoutIndex);
    // Exclusive modes — only restore if still meaningful for image count.
    // Apply at most one of Split/Swipe/Overlay/Checker (priority order below)
    // so the toggled handlers do not thrash each other during restore.
    const int n = m_engine.imageCount();
    if (d->blinkChk)
        d->blinkChk->setChecked(s.blink && n >= 2);
    m_checkerSize = s.checkerSize; // M23
    if (d->checkerSizeSlider)
        d->checkerSizeSlider->setValue(s.checkerSize);
    if (n == 2)
    {
        const bool wantSplit = s.split;
        const bool wantSwipe = s.swipe && !wantSplit;
        const bool wantOverlay = s.overlay && !wantSplit && !wantSwipe;
        const bool wantChecker = s.checker && !wantSplit && !wantSwipe && !wantOverlay; // M23
        if (d->splitChk)
            d->splitChk->setChecked(wantSplit);
        if (m_swipeChk)
            m_swipeChk->setChecked(wantSwipe);
        if (d->overlayChk)
            d->overlayChk->setChecked(wantOverlay);
        if (d->checkerChk)
            d->checkerChk->setChecked(wantChecker);
    }
    if (s.hasRoi)
        applySelectionToAll(s.roi);
    syncContextualCompareControls();
}

QString CompareWorkspace::focusImagePath() const
{
    if (m_focusIndex >= 0)
    {
        const QString panePath = comparedImages().value(m_focusIndex);
        if (!panePath.isEmpty())
            return panePath;

        const int idx = m_pairIndex + m_focusIndex;
        if (idx >= 0 && idx < m_imagePool.size())
            return m_imagePool[idx];
    }
    // Fall back to first image in the current window.
    if (!m_imagePool.isEmpty())
    {
        int idx = qBound(0, m_pairIndex, m_imagePool.size() - 1);
        return m_imagePool[idx];
    }
    // Beta UX regression (workflow_ux_tests): a fresh compare set must always
    // publish a non-empty default reference to the SelectionModel SSOT —
    // otherwise Metadata/Analysis/Export see no reference until the user
    // manually locks one. Default = first compared image.
    return comparedImages().value(0);
}

void CompareWorkspace::applyLayoutPreset(int n)
{
    // N-up compare preset. Keyboard entry is Ctrl+2 / Ctrl+4 / Ctrl+8.
    if (n < 1 || n > 8)
        return;
    m_navWindow = n;
    if (m_sessionState)
        m_sessionState->setLayoutPreset(n);
    // Prefer pool; fall back to currently loaded images.
    QStringList src = m_imagePool;
    if (src.isEmpty())
        src = comparedImages();
    if (src.isEmpty())
        return;
    // Align start so we take a contiguous window of n images.
    if (m_pairIndex < 0 || m_pairIndex >= src.size())
        m_pairIndex = 0;
    // If remaining images are fewer than n, clamp start.
    if (m_pairIndex + n > src.size())
        m_pairIndex = qMax(0, src.size() - n);
    QStringList win;
    for (int i = 0; i < n && m_pairIndex + i < src.size(); ++i)
        win << src[m_pairIndex + i];
    if (win.isEmpty())
        return;
    // Columns before setImages so finishLoad (in-place or rebuild) sees the
    // preset grid; avoid a redundant rebuildCells on the still-old frames.
    const int cols = CompareLayout::forCount(n).cols;
    m_engine.setColumns(cols);
    if (d->layoutCombo)
    {
        // Combo indices: Single=1, 2 columns=2, 3 columns=3, 4 columns=4.
        const QSignalBlocker block(d->layoutCombo);
        if (cols < d->layoutCombo->count())
            d->layoutCombo->setCurrentIndex(cols);
    }
    const NavState saved = captureNavState();
    setImages(win);
    restoreNavState(saved);
    showCompareStatus(tr("Switched to %1-pane comparison layout").arg(n));
    update();
}

void CompareWorkspace::applySession(const mviewer::domain::CompareSession &s)
{
    // M28 P1-01: with async loading the engine may not own the frames yet
    // (openCompare -> setImages -> applySession runs before the decode batch
    // lands). Defer the session and replay it inside finishLoad().
    if (m_loadInFlight)
    {
        m_pendingSession = s;
        return;
    }

    // Restore the two independent axes. SyncMode is not collapsed to a
    // boolean: Zoom-only and Drag-only sessions must survive reopen.
    m_syncZoom = s.syncMode == mviewer::domain::SyncMode::Zoom ||
                 s.syncMode == mviewer::domain::SyncMode::All;
    m_syncDrag = s.syncMode == mviewer::domain::SyncMode::Drag ||
                 s.syncMode == mviewer::domain::SyncMode::All;
    {
        const QSignalBlocker zoomBlocker(d->syncZoomChk);
        const QSignalBlocker dragBlocker(d->syncDragChk);
        d->syncZoomChk->setChecked(m_syncZoom);
        d->syncDragChk->setChecked(m_syncDrag);
    }
    m_engine.setSyncMode(m_syncZoom, m_syncDrag);

    // M35: sharedScale is the zoom ratio relative to each pane's own Fit.
    m_sharedZoomRatio = s.sharedScale > 0.0 ? s.sharedScale : 1.0;
    m_engine.setScale(m_sharedZoomRatio);
    m_engine.setOffset(s.sharedOffsetX, s.sharedOffsetY);

    const bool syncOn = m_syncZoom && m_syncDrag;
    applyCellTransforms(m_engine, s, syncOn, m_fitScales, m_sharedZoomRatio);
    ensureLocalZoomMultipliers(m_engine.imageCount());
    for (int i = 0; i < m_engine.imageCount(); ++i)
    {
        const double m = (static_cast<size_t>(i) < s.cells.size() &&
                          std::isfinite(s.cells[static_cast<size_t>(i)].localZoomMultiplier) &&
                          s.cells[static_cast<size_t>(i)].localZoomMultiplier > 0.0)
                             ? s.cells[static_cast<size_t>(i)].localZoomMultiplier
                             : 1.0;
        m_localZoomMultipliers[i] = m;
    }

    // M15 P0#1: replay the UI-only state so the reopened view is identical.
    // HeatMap / Diff threshold. Block the slider's valueChanged handler (which
    // would schedule its own batch refresh) so the threshold restore below
    // contributes exactly one logical refresh.
    m_thresholdValue = s.threshold;
    applySessionToComponents(m_sessionState.get(), m_diffRenderer.get(), s);
    if (d->thresholdSlider)
    {
        const QSignalBlocker blocker(d->thresholdSlider);
        d->thresholdSlider->setValue(static_cast<int>(s.threshold));
        if (d->thresholdLabel)
            d->thresholdLabel->setText(QString::number(static_cast<int>(s.threshold)));
    }

    // ROI / selection (synchronized across cells). Applying the selection
    // schedules the single refreshAllDiffOverlays() (ROI + threshold are both
    // restored by now); without a saved ROI the threshold restore needs one
    // refresh explicitly.
    const bool hasRoi = (s.selection.w > 0 && s.selection.h > 0);
    if (hasRoi)
    {
        applySelectionToAll(
            mviewer::domain::Selection{s.selection.x, s.selection.y, s.selection.w, s.selection.h});
    }
    else
    {
        if (!m_lastSelection.isEmpty())
            clearROI();
        else
            refreshAllDiffOverlays();
    }

    // Layout combo (0=auto,1=single-col,2=2col,3=3col,4=4col,5=one-row). Setting
    // the index triggers onLayoutChanged which drives the engine's column count.
    if (d->gridColsSpin && s.customColumns >= 1 && s.customColumns <= 8)
    {
        const QSignalBlocker blocker(d->gridColsSpin);
        d->gridColsSpin->setValue(s.customColumns);
    }
    if (d->layoutCombo && d->layoutCombo->currentIndex() != s.layoutIndex)
        d->layoutCombo->setCurrentIndex(s.layoutIndex);

    // Side (inspector + histogram) panel visibility.
    if (d->sideChk && d->sideChk->isChecked() != s.sidePanelVisible)
        d->sideChk->setChecked(s.sidePanelVisible);

    // H5: "Uniform Pixel Scale" — restore the shared-zoom alignment flag and checkbox.
    m_uniformScale = s.uniformScale;
    if (d->uniformScaleChk && d->uniformScaleChk->isChecked() != s.uniformScale)
        d->uniformScaleChk->setChecked(s.uniformScale);

    // Blink compare: restore interval + on/off state.
    if (d->blinkChk)
    {
        const bool wantBlink = s.isBlinking();
        if (d->blinkChk->isChecked() != wantBlink)
            d->blinkChk->setChecked(wantBlink);
        if (wantBlink)
            startBlink(s.blinkIntervalMs > 0 ? s.blinkIntervalMs : 500);
        else
            stopBlink();
    }

    update();
}

void CompareWorkspace::applySelectionToAll(const mviewer::domain::Selection &sel)
{
    if (!linkedROIAvailable())
    {
        // Programmatic/session callers may still provide a useful local ROI
        // for the first pane. Retain that source-space geometry for histogram
        // and state persistence, but never mirror it or present measurements
        // as linked when dimensions differ.
        // Unequal-size comparisons intentionally keep the caller's canonical
        // source-space rectangle even when it extends past the first pane.
        // There is no linked measurement to clip against, and preserving the
        // geometry keeps existing local-ROI/session behavior deterministic.
        m_lastSelection = mviewer::domain::normalizeSelection(
            sel.x, sel.y, sel.x + sel.width, sel.y + sel.height, std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max());
        if (m_roiController)
        {
            const QSignalBlocker b(m_roiController.get());
            m_roiController->setROI(m_lastSelection);
        }
        if (m_sessionState)
            m_sessionState->setCurrentROI(m_lastSelection);
        m_roiLinked = false;
        m_sharedUnlinkedRoi = !m_lastSelection.isEmpty();
        m_engine.selection().setSelection(m_lastSelection);
        for (int i = 0; i < m_cellViews.size(); ++i)
        {
            RawImageView *view = m_cellViews[i];
            if (!view)
                continue;
            if (i != 0)
            {
                view->clearSelection();
                continue;
            }
            const ImageFrame *frame = m_engine.imageAt(0);
            const int srcW =
                frame ? (frame->metadata().width > 0 ? frame->metadata().width : frame->width())
                      : 0;
            const int srcH =
                frame ? (frame->metadata().height > 0 ? frame->metadata().height : frame->height())
                      : 0;
            const CellAdjust adjust = !m_cellAdjusts.empty() ? m_cellAdjusts.front() : CellAdjust{};
            view->setSelection(mviewer::core::mapSourceSelectionToDisplay(
                m_lastSelection, analysisAdjustment(adjust), srcW, srcH));
        }
        if (d->roiGeometryLabel)
            d->roiGeometryLabel->setText(m_lastSelection.isEmpty()
                                            ? tr("ROI: —")
                                            : tr("ROI   X: %1   Y: %2   W: %3   H: %4")
                                                  .arg(m_lastSelection.x)
                                                  .arg(m_lastSelection.y)
                                                  .arg(m_lastSelection.width)
                                                  .arg(m_lastSelection.height));
        if (d->roiHistChk && d->roiHistChk->isChecked())
            refreshHistograms();
        scheduleROIMeasurement();
        update();
        return;
    }

    const QSize common = paneEffectiveSize(0);
    const int width = common.isValid() ? common.width() : 0;
    const int height = common.isValid() ? common.height() : 0;
    m_lastSelection = mviewer::domain::normalizeSelection(sel.x, sel.y, sel.x + sel.width,
                                                          sel.y + sel.height, width, height);
    if (m_roiController)
    {
        const QSignalBlocker b(m_roiController.get());
        m_roiController->setROI(m_lastSelection);
    }
    if (m_sessionState)
        m_sessionState->setCurrentROI(m_lastSelection);
    m_roiLinked = !m_lastSelection.isEmpty();
    m_sharedUnlinkedRoi = false;
    // Engine owns the frames; it mirrors the synchronized ROI to every ImageFrame.
    if (m_roiLinked)
        m_engine.applySelectionToAll(m_lastSelection);
    else
        m_engine.selection().clearSelection();
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        if (!m_roiLinked)
        {
            m_cellViews[i]->clearSelection();
            continue;
        }
        m_cellViews[i]->setSelection(m_lastSelection);
    }
    // M23: ROI + Histogram linkage — histogram surfaces keep their ROI scope
    // even when the analysis side panel is collapsed.
    if (d->roiHistChk && d->roiHistChk->isChecked())
        refreshHistograms();

    // The ROI feeds the diff overlays + metrics on the async batch path. Skip
    // the refresh while rebuildCells() is in progress — its single terminal
    // refreshAllDiffOverlays() already covers the restored ROI.
    if (!m_rebuildingCells)
        refreshAllDiffOverlays();
    scheduleROIMeasurement();
    if (d->roiGeometryLabel)
        d->roiGeometryLabel->setText(m_roiLinked ? tr("ROI   X: %1   Y: %2   W: %3   H: %4")
                                                      .arg(m_lastSelection.x)
                                                      .arg(m_lastSelection.y)
                                                      .arg(m_lastSelection.width)
                                                      .arg(m_lastSelection.height)
                                                : tr("ROI: —"));
    updateROIAvailabilityStatus();
    update();
}

void CompareWorkspace::applySelectionPreviewFromView(RawImageView *view,
                                                     const mviewer::domain::Selection &sel)
{
    if (!view)
        return;
    if (!linkedROIAvailable())
    {
        m_roiLinked = false;
        m_sharedUnlinkedRoi = false;
        const QSize srcSize = view->sourceSize();
        const int w = srcSize.isValid() ? srcSize.width() : 0;
        const int h = srcSize.isValid() ? srcSize.height() : 0;
        const auto norm = mviewer::domain::normalizeSelection(sel.x, sel.y, sel.x + sel.width,
                                                              sel.y + sel.height, w, h);
        view->setSelection(norm);
        m_lastSelection = norm;
        if (d->roiGeometryLabel)
            d->roiGeometryLabel->setText(norm.isEmpty() ? tr("ROI: —")
                                                       : tr("ROI   X: %1   Y: %2   W: %3   H: %4")
                                                             .arg(norm.x)
                                                             .arg(norm.y)
                                                             .arg(norm.width)
                                                             .arg(norm.height));
        setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle,
                               tr("Measure source image RGB after release"));
        update();
        return;
    }

    const QSize common = paneEffectiveSize(0);
    const int width = common.isValid() ? common.width() : 0;
    const int height = common.isValid() ? common.height() : 0;
    m_lastSelection = mviewer::domain::normalizeSelection(sel.x, sel.y, sel.x + sel.width,
                                                          sel.y + sel.height, width, height);
    m_roiLinked = !m_lastSelection.isEmpty();
    m_sharedUnlinkedRoi = false;
    for (RawImageView *other : m_cellViews)
    {
        if (!other)
            continue;
        other->setSelection(m_lastSelection);
    }
    clearROIStatsDisplay();
    if (d->roiGeometryLabel)
        d->roiGeometryLabel->setText(m_roiLinked ? tr("ROI   X: %1   Y: %2   W: %3   H: %4")
                                                      .arg(m_lastSelection.x)
                                                      .arg(m_lastSelection.y)
                                                      .arg(m_lastSelection.width)
                                                      .arg(m_lastSelection.height)
                                                : tr("ROI: —"));
    setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle,
                           tr("Measure source image RGB after release"));
    update();
}

void CompareWorkspace::applySelectionFromView(RawImageView *view,
                                              const mviewer::domain::Selection &sel)
{
    if (!view)
        return;
    if (linkedROIAvailable())
    {
        applySelectionToAll(sel);
        return;
    }

    m_roiLinked = false;
    m_sharedUnlinkedRoi = false;
    const QSize srcSize = view->sourceSize();
    const int w = srcSize.isValid() ? srcSize.width() : 0;
    const int h = srcSize.isValid() ? srcSize.height() : 0;
    const auto norm = mviewer::domain::normalizeSelection(sel.x, sel.y, sel.x + sel.width,
                                                          sel.y + sel.height, w, h);
    view->setSelection(norm);
    m_lastSelection = norm;
    if (m_roiController)
    {
        const QSignalBlocker b(m_roiController.get());
        m_roiController->setROI(m_lastSelection);
    }
    if (m_sessionState)
        m_sessionState->setCurrentROI(m_lastSelection);
    if (d->roiGeometryLabel)
        d->roiGeometryLabel->setText(norm.isEmpty() ? tr("ROI: —")
                                                   : tr("ROI   X: %1   Y: %2   W: %3   H: %4")
                                                         .arg(norm.x)
                                                         .arg(norm.y)
                                                         .arg(norm.width)
                                                         .arg(norm.height));
    if (d->roiHistChk && d->roiHistChk->isChecked())
        refreshHistograms();
    scheduleROIMeasurement();
    update();
}

void CompareWorkspace::clearROI()
{
    if (m_roiTask)
        TaskScheduler::cancel(m_roiTask);
    m_roiTask.reset();
    ++m_roiGen;
    m_roiLinked = false;
    m_lastSelection = {};
    if (m_roiController)
    {
        const QSignalBlocker b(m_roiController.get());
        m_roiController->clearROI();
    }
    if (m_sessionState)
        m_sessionState->clearROI();
    m_engine.selection().clearSelection();
    for (RawImageView *view : m_cellViews)
        if (view)
            view->clearSelection();
    clearROIStatsDisplay();
    // The geometry readout is written only by the applySelection* paths, so
    // clearing the ROI used to leave "X/Y/W/H" of a box that no longer exists.
    if (d->roiGeometryLabel)
        d->roiGeometryLabel->setText(tr("ROI: —"));
    if (d->roiHistChk && d->roiHistChk->isChecked())
        refreshHistograms();
    refreshAllDiffOverlays();
    setROIMeasurementState(mviewer::ui::ROIMeasurementState::Idle);
    update();
}
