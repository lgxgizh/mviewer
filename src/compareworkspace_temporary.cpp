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
    target->setTransientDisplay(source->displayImage(), source->sourceSize(), source->sourceRect());
    // The target pane's scale matches its own source. A different-resolution
    // stand-in must use the scale that keeps the same on-screen footprint.
    // Uniform pixel scale keeps the target's absolute scale (render scale 0).
    // Same-resolution images preserve target pane scale directly without re-fitting.
    if (!m_uniformScale && m_syncZoom && source->sourceSize() != target->sourceSize())
    {
        const QSize src = source->sourceSize();
        const double render = scaleForPaneSource(targetPane, src.width(), src.height());
        if (render > 0.0)
            target->setTransientRenderScale(render);
    }
    m_temporaryCompareActive = true;
    m_temporaryTargetPane = targetPane;
    if (m_temporaryCompareButton && m_temporaryDigit == 0)
        m_temporaryCompareButton->setDown(true);
    update();
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
        if (m_compareCanvas)
            m_compareCanvas->update();
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
        if (m_compareCanvas)
            m_compareCanvas->update();
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
    m_temporaryDigit = 0;
    if (m_temporaryCompareButton)
        m_temporaryCompareButton->setDown(false);
    update();
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
        mviewer::ui::updateComparePaneHeaderBadge(cell, i + 1, show && i < count);
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
            "在鼠标所在窗格临时显示第 N 张（不在窗格上则用当前窗格）；松开恢复。"
            "布局预设用 Ctrl+2 / Ctrl+4 / Ctrl+8。");
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
