// CompareWorkspace navigation & session: pair navigation, layout presets, session apply (M20 P0#2).
#include "compareworkspace_p.h"

// A-4.5 / M20: continuous compare — walk a sliding window over the pool.
void CompareWorkspace::setImagePool(const QStringList &allPaths)
{
    m_imagePool = allPaths;
    // Align window start to the first currently-loaded image when possible.
    m_pairIndex = 0;
    if (!m_imagePool.isEmpty() && m_engine.imageCount() > 0)
    {
        const QString first = comparedImages().value(0);
        const int idx = m_imagePool.indexOf(first);
        if (idx >= 0)
            m_pairIndex = idx;
    }
    // Infer nav window from current load count when it is a known preset.
    const int n = m_engine.imageCount();
    if (n == 2 || n == 4 || n == 8)
        m_navWindow = n;
    updatePairButtons();
}

void CompareWorkspace::setNavWindow(int n)
{
    if (n != 2 && n != 4 && n != 8)
        n = 2;
    m_navWindow = n;
    updatePairButtons();
}

bool CompareWorkspace::hasNextPair() const
{
    return m_pairIndex + m_navWindow < m_imagePool.size();
}

bool CompareWorkspace::hasPrevPair() const
{
    return m_pairIndex >= m_navWindow;
}

CompareWorkspace::NavState CompareWorkspace::captureNavState() const
{
    NavState s;
    s.blink = m_blinkChk && m_blinkChk->isChecked();
    s.split = m_splitChk && m_splitChk->isChecked();
    s.swipe = m_swipeChk && m_swipeChk->isChecked();
    s.overlay = m_overlayChk && m_overlayChk->isChecked();
    s.checker = m_checkerChk && m_checkerChk->isChecked(); // M23
    s.checkerSize = m_checkerSize;                         // M23
    s.diffHighlight = m_diffHighlightChk && m_diffHighlightChk->isChecked();
    s.syncZoom = m_syncZoomChk ? m_syncZoomChk->isChecked() : true;
    s.syncDrag = m_syncDragChk ? m_syncDragChk->isChecked() : true;
    s.crosshair = m_crosshairChk && m_crosshairChk->isChecked();
    s.pixelLink = m_pixelLinkChk && m_pixelLinkChk->isChecked();
    s.overlayAlpha = m_overlayAlpha;
    s.threshold = m_thresholdValue;
    s.layoutIndex = m_layoutCombo ? m_layoutCombo->currentIndex() : 0;
    s.roi = m_lastSelection;
    s.hasRoi = m_lastSelection.width > 0 && m_lastSelection.height > 0;
    return s;
}

void CompareWorkspace::restoreNavState(const NavState &s)
{
    if (m_syncZoomChk)
        m_syncZoomChk->setChecked(s.syncZoom);
    if (m_syncDragChk)
        m_syncDragChk->setChecked(s.syncDrag);
    if (m_crosshairChk)
        m_crosshairChk->setChecked(s.crosshair);
    if (m_pixelLinkChk)
        m_pixelLinkChk->setChecked(s.pixelLink);
    if (m_diffHighlightChk)
        m_diffHighlightChk->setChecked(s.diffHighlight);
    m_overlayAlpha = s.overlayAlpha;
    if (m_overlayAlphaSlider)
        m_overlayAlphaSlider->setValue(s.overlayAlpha);
    m_thresholdValue = s.threshold;
    if (m_thresholdSlider)
        m_thresholdSlider->setValue(s.threshold);
    if (m_layoutCombo && s.layoutIndex >= 0 && s.layoutIndex < m_layoutCombo->count())
        m_layoutCombo->setCurrentIndex(s.layoutIndex);
    // Exclusive modes — only restore if still meaningful for image count.
    // Apply at most one of Split/Swipe/Overlay/Checker (priority order below)
    // so the toggled handlers do not thrash each other during restore.
    const int n = m_engine.imageCount();
    if (m_blinkChk)
        m_blinkChk->setChecked(s.blink && n >= 2);
    m_checkerSize = s.checkerSize; // M23
    if (m_checkerSizeSlider)
        m_checkerSizeSlider->setValue(s.checkerSize);
    if (n == 2)
    {
        const bool wantSplit = s.split;
        const bool wantSwipe = s.swipe && !wantSplit;
        const bool wantOverlay = s.overlay && !wantSplit && !wantSwipe;
        const bool wantChecker = s.checker && !wantSplit && !wantSwipe && !wantOverlay; // M23
        if (m_splitChk)
            m_splitChk->setChecked(wantSplit);
        if (m_swipeChk)
            m_swipeChk->setChecked(wantSwipe);
        if (m_overlayChk)
            m_overlayChk->setChecked(wantOverlay);
        if (m_checkerChk)
            m_checkerChk->setChecked(wantChecker);
    }
    if (s.hasRoi)
        applySelectionToAll(s.roi);
}

QString CompareWorkspace::focusImagePath() const
{
    if (m_focusIndex >= 0)
    {
        const int idx = m_pairIndex + m_focusIndex;
        if (idx >= 0 && idx < m_imagePool.size())
            return m_imagePool[idx];
        // Pool not seeded yet (openCompare calls setImages before
        // setImagePool): resolve the focused cell via the engine instead.
        const QStringList imgs = comparedImages();
        if (m_focusIndex < imgs.size())
            return imgs[m_focusIndex];
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

void CompareWorkspace::nextPair()
{
    if (!hasNextPair())
        return;
    const NavState saved = captureNavState();
    m_pairIndex += m_navWindow;
    QStringList win;
    for (int i = 0; i < m_navWindow && m_pairIndex + i < m_imagePool.size(); ++i)
        win << m_imagePool[m_pairIndex + i];
    setImages(win);
    restoreNavState(saved);
    updatePairButtons();
    // P0: write the first image of the new window back to global SelectionModel.
    if (m_selection && !win.isEmpty())
        m_selection->setCurrentImage(win.first());
}

void CompareWorkspace::prevPair()
{
    if (!hasPrevPair())
        return;
    const NavState saved = captureNavState();
    m_pairIndex -= m_navWindow;
    if (m_pairIndex < 0)
        m_pairIndex = 0;
    QStringList win;
    for (int i = 0; i < m_navWindow && m_pairIndex + i < m_imagePool.size(); ++i)
        win << m_imagePool[m_pairIndex + i];
    setImages(win);
    restoreNavState(saved);
    updatePairButtons();
    // P0: write the first image of the new window back to global SelectionModel.
    if (m_selection && !win.isEmpty())
        m_selection->setCurrentImage(win.first());
}

void CompareWorkspace::updatePairButtons()
{
    if (m_nextPairBtn)
        m_nextPairBtn->setEnabled(hasNextPair());
    if (m_prevPairBtn)
        m_prevPairBtn->setEnabled(hasPrevPair());
}

void CompareWorkspace::applyLayoutPreset(int n)
{
    // M16: number keys 1–8 select an N-up compare preset.
    if (n < 1 || n > 8)
        return;
    m_navWindow = n;
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
    const NavState saved = captureNavState();
    setImages(win);
    // Choose a near-square column count per preset:
    // 1 → 1, 2 → 2, 3 → 3, 4 → 2 (2×2), 5/6 → 3, 7/8 → 4.
    const int cols = (n <= 1) ? 1 : (n == 2) ? 2 : (n == 3) ? 3 : (n == 4) ? 2 : (n <= 6) ? 3 : 4;
    m_engine.setColumns(cols);
    if (m_layoutCombo)
    {
        // Combo indices: 单列=1, 2列=2, 3列=3, 4列=4.
        if (cols < m_layoutCombo->count())
            m_layoutCombo->setCurrentIndex(cols);
    }
    rebuildCells();
    fitAll();
    restoreNavState(saved);
    updatePairButtons();
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

    // Sync mode: All == both zoom+drag synced; Off == neither. (The CompareSession
    // only stores All/Off for the persisted snapshot; per-axis toggles default
    // to the synced state.)
    const bool syncOn = (s.syncMode != mviewer::domain::SyncMode::Off);
    m_syncZoom = syncOn;
    m_syncDrag = syncOn;
    m_syncZoomChk->setChecked(syncOn);
    m_syncDragChk->setChecked(syncOn);
    m_engine.setSyncEnabled(syncOn);

    // Shared transform (used when sync is on).
    m_engine.setScale(s.sharedScale);
    m_engine.setOffset(s.sharedOffsetX, s.sharedOffsetY);

    // Per-cell independent transforms (used when sync is off).
    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        const int idx = static_cast<int>(i);
        if (idx >= m_engine.imageCount())
            break;
        m_engine.setCellScale(idx, s.cells[i].scale);
        m_engine.setCellOffset(idx, s.cells[i].offsetX, s.cells[i].offsetY);
    }

    // M15 P0#1: replay the UI-only state so the reopened view is identical.
    // HeatMap / Diff threshold. Block the slider's valueChanged handler (which
    // would schedule its own batch refresh) so the threshold restore below
    // contributes exactly one logical refresh.
    m_thresholdValue = s.threshold;
    if (m_thresholdSlider)
    {
        const QSignalBlocker blocker(m_thresholdSlider);
        m_thresholdSlider->setValue(static_cast<int>(s.threshold));
        if (m_thresholdLabel)
            m_thresholdLabel->setText(QString::number(static_cast<int>(s.threshold)));
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
        refreshAllDiffOverlays();
    }

    // Layout combo (0=auto,1=single-col,2=2col,3=3col,4=4col,5=one-row). Setting
    // the index triggers onLayoutChanged which drives the engine's column count.
    if (m_layoutCombo && m_layoutCombo->currentIndex() != s.layoutIndex)
        m_layoutCombo->setCurrentIndex(s.layoutIndex);

    // Side (inspector + histogram) panel visibility.
    if (m_sideChk && m_sideChk->isChecked() != s.sidePanelVisible)
        m_sideChk->setChecked(s.sidePanelVisible);

    // H5: "统一像素倍率" — restore the shared-zoom alignment flag and checkbox.
    m_uniformScale = s.uniformScale;
    if (m_uniformScaleChk && m_uniformScaleChk->isChecked() != s.uniformScale)
        m_uniformScaleChk->setChecked(s.uniformScale);

    // Blink compare: restore interval + on/off state.
    if (m_blinkChk)
    {
        const bool wantBlink = s.isBlinking();
        if (m_blinkChk->isChecked() != wantBlink)
            m_blinkChk->setChecked(wantBlink);
        if (wantBlink)
            startBlink(s.blinkIntervalMs > 0 ? s.blinkIntervalMs : 500);
        else
            stopBlink();
    }

    update();
}

void CompareWorkspace::applySelectionToAll(const mviewer::domain::Selection &sel)
{
    // Engine owns the frames; it mirrors the synchronized ROI to every ImageFrame.
    m_engine.applySelectionToAll(sel);
    m_lastSelection = sel;
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        m_cellViews[i]->setSelection(sel);
    }
    // M23: ROI + Histogram 联动 — histogram surfaces keep their ROI scope
    // even when the analysis side panel is collapsed.
    if (m_roiHistChk && m_roiHistChk->isChecked())
        refreshHistograms();

    // The ROI feeds the diff overlays + metrics on the async batch path. Skip
    // the refresh while rebuildCells() is in progress — its single terminal
    // refreshAllDiffOverlays() already covers the restored ROI.
    if (!m_rebuildingCells)
        refreshAllDiffOverlays();
    update();
}
