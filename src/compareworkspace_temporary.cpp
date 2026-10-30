#include "compareworkspace_temporary.h"
#include "compareworkspace_p.h"

#include "compareworkspace_pane_header.h"

#include <QCursor>

namespace
{

// Off a pane, preview on the locked reference, then the edit cell, else pane 0.
int digitHoldFallbackPane(int imageCount, int focusIndex, int explicitEditIndex, int editIndex)
{
    if (focusIndex >= 0 && focusIndex < imageCount)
        return focusIndex;
    if (explicitEditIndex >= 0 && explicitEditIndex < imageCount)
        return explicitEditIndex;
    if (editIndex >= 0 && editIndex < imageCount)
        return editIndex;
    return -1;
}

} // namespace

int CompareWorkspace::paneIndexAtGlobalPos(const QPoint &globalPos) const
{
    if (m_hoverIdx >= 0 && m_hoverIdx < m_cellViews.size())
    {
        RawImageView *hovered = m_cellViews.at(m_hoverIdx);
        if (hovered && hovered->isVisible())
            return m_hoverIdx;
    }
    for (int i = 0; i < m_cellViews.size(); ++i)
    {
        RawImageView *view = m_cellViews.at(i);
        if (!view || !view->isVisible())
            continue;
        if (view->rect().contains(view->mapFromGlobal(globalPos)))
            return i;
    }
    if (m_compareCanvas && m_compareCanvas->isVisible())
        return canvasRefCellAt(m_compareCanvas->mapFromGlobal(globalPos));
    return -1;
}

bool CompareWorkspace::temporaryHoldBlocked() const
{
    return (m_blinkChk && m_blinkChk->isChecked());
}

void CompareWorkspace::applyTemporaryDisplay(int targetPane, int sourcePane)
{
    if (targetPane < 0 || sourcePane < 0 || targetPane >= m_cellViews.size() ||
        sourcePane >= m_cellViews.size())
        return;
    RawImageView *target = m_cellViews[targetPane];
    RawImageView *source = m_cellViews[sourcePane];
    if (!target || !source || source->displayImage().isNull() || !source->sourceSize().isValid())
    {
        m_temporaryDigit = 0;
        showCompareStatus(tr("No image to switch to on this side yet"));
        return;
    }
    if (m_temporaryCompareActive && m_temporaryTargetPane >= 0 &&
        m_temporaryTargetPane != targetPane && m_temporaryTargetPane < m_cellViews.size() &&
        m_cellViews[m_temporaryTargetPane])
        m_cellViews[m_temporaryTargetPane]->clearTransientDisplay();
    // L4/L5: replay the source pane's exact viewport (scale + offset + backdrop)
    // so Space / 1-8 shows the same local region the user was inspecting.
    // presentedScale() already carries the shared ratio AND the source pane's
    // own Ctrl+wheel multiplier, so it is the view the user actually sees.
    const double srcScale = source->presentedScale();
    const QPointF srcOffset = source->offset();
    QImage backdrop;
    if (source->hasBackdrop())
        backdrop = source->backdropImage();
    const QSize image = source->sourceSize();
    const auto plan = mviewer::ui::planTemporaryViewport(
        srcScale, srcOffset.x(), srcOffset.y(), image.width(), image.height(), source->width(),
        source->height(), target->width(), target->height(), !m_uniformScale && m_syncZoom);
    target->setTransientDisplay(source->displayImage(), source->sourceSize(), source->sourceRect(),
                                plan.scale, QPointF(plan.offsetX, plan.offsetY), backdrop);
    // The held image brings its ROI along (overlays follow the shown image).
    target->setTransientSelection(m_roiLinked ? m_lastSelection : source->selection(),
                                  source->paneTag());
    m_temporaryCompareActive = true;
    m_temporaryTargetPane = targetPane;
    m_temporarySourcePane = sourcePane;
    if (m_temporaryCompareButton && m_temporaryDigit == 0)
        m_temporaryCompareButton->setDown(true);
    applyDisplayedPaneOverlays();
    update();
}

void CompareWorkspace::reassertTemporaryDisplay()
{
    if (!m_temporaryCompareActive || m_canvasTemporaryPane >= 0)
        return;
    const int target = m_temporaryTargetPane;
    const int source = m_temporarySourcePane;
    if (target < 0 || source < 0 || target >= m_cellViews.size() || source >= m_cellViews.size())
        return;
    RawImageView *targetView = m_cellViews[target];
    RawImageView *sourceView = m_cellViews[source];
    if (!targetView || !sourceView || sourceView->displayImage().isNull() ||
        !sourceView->sourceSize().isValid())
        return;
    // Replay from the source pane's current raster and viewport: covers both a
    // delivery to the held pane (its transient was dropped) and a sharper
    // raster landing on the source pane (the held copy would be stale).
    applyTemporaryDisplay(target, source);
}

void CompareWorkspace::beginClassicTemporaryCompare()
{
    // Toolbar button: always classic A<-B regardless of cursor position.
    if (m_engine.imageCount() != 2 || temporaryHoldBlocked())
    {
        beginTemporaryCompare();
        return;
    }
    m_temporaryDigit = 0;
    if (anyCanvasCompareMode())
    {
        m_canvasTemporaryPane = 1;
        m_temporaryCompareActive = true;
        if (m_temporaryCompareButton)
            m_temporaryCompareButton->setDown(true);
        applyDisplayedPaneOverlays();
        update();
        return;
    }
    applyTemporaryDisplay(0, 1);
}

void CompareWorkspace::beginTemporaryCompare()
{
    const int hovered = paneIndexAtGlobalPos(QCursor::pos());
    const auto decision = mviewer::ui::decidePairHold(m_engine.imageCount(), hovered);
    if (decision.action == mviewer::ui::TemporaryAction::HintUseDigits)
    {
        showCompareStatus(
            tr("With more than 2 images, hold number keys 1–N to switch temporarily"));
        return;
    }
    if (decision.action == mviewer::ui::TemporaryAction::HintMoveMouse)
    {
        showCompareStatus(tr("Move the mouse to one side, then hold Space"));
        return;
    }
    if (decision.action != mviewer::ui::TemporaryAction::ShowPair)
        return;
    if (temporaryHoldBlocked())
    {
        showCompareStatus(tr("Cannot temporarily switch while flicker is on"));
        return;
    }
    m_temporaryDigit = 0;
    if (anyCanvasCompareMode())
    {
        m_canvasTemporaryPane = decision.sourcePane;
        m_temporaryCompareActive = true;
        if (m_temporaryCompareButton)
            m_temporaryCompareButton->setDown(true);
        applyDisplayedPaneOverlays();
        update();
        return;
    }
    applyTemporaryDisplay(decision.targetPane, decision.sourcePane);
}

void CompareWorkspace::beginDigitTemporaryCompare(int digit)
{
    const int count = m_engine.imageCount();
    const int hovered = paneIndexAtGlobalPos(QCursor::pos());
    const int fallback = digitHoldFallbackPane(count, m_focusIndex, m_explicitEditIdx, m_editIdx);
    const auto decision = mviewer::ui::decideDigitHold(count, hovered, digit, fallback);
    if (decision.action == mviewer::ui::TemporaryAction::HintUseDigits)
    {
        if (count < 1)
            showCompareStatus(tr("No images to switch"));
        else
            showCompareStatus(tr("Currently %1 images in total; hold 1–%2 to switch temporarily")
                                  .arg(count)
                                  .arg(count));
        return;
    }
    if (decision.action != mviewer::ui::TemporaryAction::ShowDigit)
        return;
    if (decision.sourcePane == decision.targetPane)
        return;
    m_temporaryDigit = digit;
    applyTemporaryDisplay(decision.targetPane, decision.sourcePane);
}

void CompareWorkspace::endTemporaryCompare()
{
    if (!m_temporaryCompareActive)
        return;
    if (m_canvasTemporaryPane >= 0)
    {
        m_canvasTemporaryPane = -1;
        if (m_compareCanvas)
            m_compareCanvas->update();
    }
    if (m_temporaryTargetPane >= 0 && m_temporaryTargetPane < m_cellViews.size() &&
        m_cellViews[m_temporaryTargetPane])
        m_cellViews[m_temporaryTargetPane]->clearTransientDisplay();
    m_temporaryCompareActive = false;
    m_temporaryTargetPane = -1;
    m_temporarySourcePane = -1;
    m_temporaryDigit = 0;
    if (m_temporaryCompareButton)
        m_temporaryCompareButton->setDown(false);
    applyDisplayedPaneOverlays();
    update();
}

int CompareWorkspace::displayedImageIndex(int pane) const
{
    if (!m_temporaryCompareActive)
        return pane;
    if (m_canvasTemporaryPane >= 0)
    {
        // Split keeps the switch in the hovered half; the other canvas modes
        // draw one full-canvas image, labelled as pane 0.
        const bool split = m_splitChk && m_splitChk->isChecked();
        const int shownIn = split ? 1 - m_canvasTemporaryPane : 0;
        return pane == shownIn ? m_canvasTemporaryPane : pane;
    }
    if (pane == m_temporaryTargetPane && m_temporarySourcePane >= 0)
        return m_temporarySourcePane;
    return pane;
}

// Overlays follow the image a pane presents, not the pane slot: a temporary
// switch (Space / 1-8 / hold button) and its release re-label the pane from
// cached data. Histograms come from the per-image cache, never a recompute.
void CompareWorkspace::applyDisplayedPaneOverlays()
{
    applyFilenameOverlays();
    updatePaneIndexBadges();
    for (size_t i = 0; i < m_cellHists.size(); ++i)
    {
        HistogramWidget *hw = m_cellHists[i];
        const auto shown = static_cast<size_t>(displayedImageIndex(static_cast<int>(i)));
        if (!hw)
            continue;
        if (shown < m_paneHistCache.size() && !m_paneHistCache[shown].r.empty())
            hw->setHistograms({m_paneHistCache[shown]});
        else
            hw->clear();
    }
    if (m_paneHistOverlay)
        positionCellHists();
    positionROIHud();
    if (m_compareCanvas)
        m_compareCanvas->update();
}

void CompareWorkspace::updatePaneIndexBadges()
{
    const int count = m_engine.imageCount();
    const bool show = count > 2 && count <= 8;
    for (int i = 0; i < m_cellViews.size(); ++i)
    {
        RawImageView *view = m_cellViews[i];
        QWidget *cell = view ? view->parentWidget() : nullptr;
        if (!cell)
            continue;
        // The index chip names the image shown (1-8 hold shows image N here).
        mviewer::ui::updateComparePaneHeaderBadge(cell, displayedImageIndex(i) + 1,
                                                  show && i < count);
    }
}

void CompareWorkspace::updateTemporaryCompareAvailability()
{
    const bool imagesReady =
        m_engine.imageCount() == 2 && m_cellViews.size() >= 2 && m_cellViews[0] && m_cellViews[1] &&
        !m_cellViews[0]->displayImage().isNull() && !m_cellViews[1]->displayImage().isNull() &&
        m_cellViews[0]->sourceSize().isValid() && m_cellViews[1]->sourceSize().isValid();
    const bool available = imagesReady && !temporaryHoldBlocked();
    if (m_temporaryCompareButton)
    {
        m_temporaryCompareButton->setEnabled(available);
        m_temporaryCompareButton->setToolTip(temporaryCompareTooltip());
    }
    updatePaneIndexBadges();
    if (!available && m_temporaryCompareActive && m_temporaryDigit == 0)
        endTemporaryCompare();
}

void CompareWorkspace::refreshCompareControlTooltips()
{
    const int count = m_engine.imageCount();
    const bool loading = m_loadInFlight;
    auto setTip = [](QWidget *widget, const QString &enabledTip, const QString &disabledTip)
    {
        if (!widget)
            return;
        widget->setToolTip(widget->isEnabled() ? enabledTip : disabledTip);
    };
    setTip(m_swapBtn, tr("Swap pane order (shortcut X)"),
           loading ? tr("Images are still loading; cannot swap yet")
                   : tr("At least 2 images are required to swap (shortcut X)"));
    setTip(m_analyzeBtn, tr("Open Comparison Inspection Panel"),
           loading ? tr("Images are still loading; cannot analyze yet")
                   : tr("No images available to analyze"));
    setTip(m_exportReportBtn, tr("Export comparison results as an HTML/Markdown/JSON report"),
           count < 2 ? tr("At least 2 images are required to export a report")
                     : tr("Images are still loading; cannot export a report yet"));
    if (m_clearLinksBtn)
        m_clearLinksBtn->setToolTip(clearLinkTooltip());
    updateTemporaryCompareAvailability();
}

QString CompareWorkspace::temporaryCompareTooltip() const
{
    const bool imagesReady =
        m_engine.imageCount() == 2 && m_cellViews.size() >= 2 && m_cellViews[0] && m_cellViews[1] &&
        !m_cellViews[0]->displayImage().isNull() && !m_cellViews[1]->displayImage().isNull() &&
        m_cellViews[0]->sourceSize().isValid() && m_cellViews[1]->sourceSize().isValid();
    if (m_engine.imageCount() > 2)
    {
        return tr(
            "With more than 2 images, this button and Space are unavailable. Hold number keys 1–N "
            "to temporarily show image N in the pane under the cursor (or the current pane); "
            "release to restore. "
            "Layout presets use Ctrl+2 / Ctrl+4 / Ctrl+8.");
    }
    if (!imagesReady)
        return tr("Exactly 2 displayed images are required to hold and switch temporarily");
    if (temporaryHoldBlocked())
        return tr("Cannot temporarily switch while flicker is on");
    return tr("While holding Space, the side under the mouse temporarily shows the other side's "
              "image; release to restore.");
}

QString CompareWorkspace::clearLinkTooltip() const
{
    if (m_clearLinksBtn && m_clearLinksBtn->isEnabled())
        return tr("Clear all Pixel Link markers");
    const bool linkOn = m_pixelLinkChk && m_pixelLinkChk->isChecked();
    if (!linkOn)
        return tr("Check \"Pixel Link\" first (available only with 2 images)");
    return tr("No markers yet");
}
