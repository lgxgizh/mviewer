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
    if (m_hoverIdx >= 0 && m_hoverIdx < m_cellViews.size())
        return m_hoverIdx;
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
        showCompareStatus(tr("这一侧还没有可切换的图像"));
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
        showCompareStatus(tr("超过 2 张时请按住数字键 1–N 临时换图"));
        return;
    }
    if (decision.action == mviewer::ui::TemporaryAction::HintMoveMouse)
    {
        showCompareStatus(tr("将鼠标移到某一侧再按住 Space"));
        return;
    }
    if (decision.action != mviewer::ui::TemporaryAction::ShowPair)
        return;
    if (temporaryHoldBlocked())
    {
        showCompareStatus(tr("闪烁开启时不能临时切换"));
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
            showCompareStatus(tr("没有可切换的图像"));
        else
            showCompareStatus(tr("当前共 %1 张，按住 1–%2 临时换图").arg(count).arg(count));
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
    setTip(m_prevPairBtn, pairNavTooltip(false, false), pairNavTooltip(false, true));
    setTip(m_nextPairBtn, pairNavTooltip(true, false), pairNavTooltip(true, true));
    setTip(m_swapBtn, tr("交换窗格顺序（快捷键 X）"),
           loading ? tr("图片还在加载，暂时不能交换")
                   : tr("至少需要 2 张图片才能交换（快捷键 X）"));
    setTip(m_analyzeBtn, tr("打开比较检视面板"),
           loading ? tr("图片还在加载，暂时不能分析") : tr("当前没有可分析的图像"));
    setTip(m_exportReportBtn, tr("将对比结果导出为 HTML/Markdown/JSON 报告"),
           count < 2 ? tr("至少需要 2 张图片才能导出报告") : tr("图片还在加载，暂时不能导出报告"));
    if (m_clearLinksBtn)
        m_clearLinksBtn->setToolTip(clearLinkTooltip());
    updateTemporaryCompareAvailability();
}

QString CompareWorkspace::pairNavTooltip(bool next, bool atEnd) const
{
    if (next && atEnd)
    {
        return tr("已经是最后一对，没有下一对（N、PgDn、→）。Ctrl+Shift+A 取消选择。"
                  "Ctrl+Alt+A 批量分析导出。");
    }
    if (next)
    {
        return tr("N 下一对（PgDn、→ 同样是下一对）。Ctrl+Shift+A 取消选择。"
                  "Ctrl+Alt+A 批量分析导出。");
    }
    if (atEnd)
    {
        return tr("已经是第一对，没有上一对（P、PgUp、←）。Ctrl+Shift+A 取消选择。"
                  "Ctrl+Alt+A 批量分析导出。");
    }
    return tr("P 上一对（PgUp、← 同样是上一对）。Ctrl+Shift+A 取消选择。"
              "Ctrl+Alt+A 批量分析导出。");
}

QString CompareWorkspace::temporaryCompareTooltip() const
{
    const bool imagesReady =
        m_engine.imageCount() == 2 && m_cellViews.size() >= 2 && m_cellViews[0] && m_cellViews[1] &&
        !m_cellViews[0]->displayImage().isNull() && !m_cellViews[1]->displayImage().isNull() &&
        m_cellViews[0]->sourceSize().isValid() && m_cellViews[1]->sourceSize().isValid();
    if (m_engine.imageCount() > 2)
    {
        return tr("超过 2 张时此按钮和 Space 不可用。按住数字键 1–N "
                  "在鼠标所在窗格临时显示第 N 张（不在窗格上则用当前窗格）；松开恢复。"
                  "布局预设用 Ctrl+2 / Ctrl+4 / Ctrl+8。");
    }
    if (!imagesReady)
        return tr("需要恰好 2 张已显示的图片才能按住临时切换");
    if (temporaryHoldBlocked())
        return tr("闪烁开启时不能临时切换");
    return tr("按住 Space 时，鼠标所在一侧临时显示另一侧的图；松开恢复。");
}

QString CompareWorkspace::clearLinkTooltip() const
{
    if (m_clearLinksBtn && m_clearLinksBtn->isEnabled())
        return tr("清除全部像素连线标记");
    const bool linkOn = m_pixelLinkChk && m_pixelLinkChk->isChecked();
    if (!linkOn)
        return tr("请先勾选「像素连线」（仅 2 张图片时可用）");
    return tr("还没有标记点");
}
