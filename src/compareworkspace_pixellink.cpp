// Pixel-link mode: drop a marker in image coordinates and see the same point in
// every pane, with crosshairs, numeric deltas and dashed connectors.
//
// Extracted from compareworkspace_interact.cpp (948 lines against the 1000-line
// responsibility ceiling). Behaviour-preserving: the methods keep their
// declarations in compareworkspace.h.
#include "compareworkspace_p.h"

#include <QPainter>
#include <QPen>
#include <QPointF>

#include <algorithm>

void CompareWorkspace::onPixelLinkToggled(bool on)
{
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(on && !m_linkPoints.isEmpty());
    if (!on)
    {
        // Keep markers stored but hide them when mode is off.
        for (auto *v : m_cellViews)
            if (v)
                v->clearLinkMarkers();
    }
    else
    {
        refreshLinkMarkers();
    }
    updateLinkInfo();
    update();
}

void CompareWorkspace::addLinkPoint(const QPointF &imgPt)
{
    if (imgPt.x() < 0 || imgPt.y() < 0)
        return;
    // Cap at 16 markers to keep the UI readable.
    if (m_linkPoints.size() >= 16)
        m_linkPoints.removeFirst();
    m_linkPoints.push_back(imgPt);
    refreshLinkMarkers();
    updateLinkInfo();
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(m_pixelLinkChk && m_pixelLinkChk->isChecked());
    update();
}

void CompareWorkspace::clearLinkPoints()
{
    m_linkPoints.clear();
    refreshLinkMarkers();
    updateLinkInfo();
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(false);
    update();
}

void CompareWorkspace::refreshLinkMarkers()
{
    for (auto *v : m_cellViews)
    {
        if (!v)
            continue;
        if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && !m_linkPoints.isEmpty())
            v->setLinkMarkers(m_linkPoints);
        else
            v->clearLinkMarkers();
    }
}

void CompareWorkspace::updateLinkInfo()
{
    if (!m_linkInfoLabel)
        return;
    if (m_linkPoints.isEmpty())
    {
        m_linkInfoLabel->setText(tr("Markers: 0"));
        m_linkInfoLabel->setToolTip(QString());
        return;
    }

    struct LinkSample
    {
        int r = 0;
        int g = 0;
        int b = 0;
        bool valid = false;
    };
    const int n = m_engine.imageCount();
    const int baseIdx = n > 0 ? qBound(0, diffBaseIndex(), n - 1) : -1;
    QStringList lines;
    lines << tr("%1 markers").arg(m_linkPoints.size());
    for (int i = 0; i < m_linkPoints.size(); ++i)
    {
        const int x = qRound(m_linkPoints[i].x());
        const int y = qRound(m_linkPoints[i].y());
        QString line = tr("#%1 (%2,%3)").arg(i + 1).arg(x).arg(y);
        std::vector<LinkSample> samples(static_cast<size_t>(n));
        for (int c = 0; c < n; ++c)
        {
            const ImageFrame *frame = m_engine.imageAt(c);
            if (!frame)
                continue;
            const CellAdjust adjust = c < static_cast<int>(m_cellAdjusts.size())
                                          ? m_cellAdjusts[static_cast<size_t>(c)]
                                          : CellAdjust{};
            // Pixel Link is an analysis readout, not a display readout. The
            // RawImageView raster may be a bounded LOD or covered region, and
            // an infeasible source intentionally has no full pixels. Keep the
            // latter invalid instead of triggering materialization or sampling
            // whatever display raster happens to be visible.
            const auto pixel = mviewer::core::sampleAnalysisPixel(frame->pixels(),
                                                                  analysisAdjustment(adjust), x, y);
            if (!pixel.valid)
                continue;
            samples[static_cast<size_t>(c)] = {pixel.r, pixel.g, pixel.b, true};
        }
        const bool baseValid = baseIdx >= 0 && samples[static_cast<size_t>(baseIdx)].valid;
        const LinkSample base = baseValid ? samples[static_cast<size_t>(baseIdx)] : LinkSample{};
        for (int c = 0; c < n; ++c)
        {
            const LinkSample &sample = samples[static_cast<size_t>(c)];
            if (!sample.valid)
            {
                line += tr("  [%1] invalid").arg(c);
            }
            else if (c == baseIdx)
            {
                line += QString("  [%1] RGB(%2,%3,%4)")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b);
            }
            else if (baseValid)
            {
                line += QString("  [%1] RGB(%2,%3,%4) Δ(%5,%6,%7)")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b)
                            .arg(sample.r - base.r)
                            .arg(sample.g - base.g)
                            .arg(sample.b - base.b);
            }
            else
            {
                line += tr("  [%1] RGB(%2,%3,%4) Δinvalid")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b);
            }
        }
        lines << line;
    }
    m_linkInfoLabel->setText(tr("Markers: %1").arg(m_linkPoints.size()));
    m_linkInfoLabel->setToolTip(lines.join('\n'));
    const auto &last = m_linkPoints.last();
    emit pixelInfo(tr("Pixel Link #%1 @ (%2,%3) — hover \"Markers\" to view all RGB/Δ")
                       .arg(m_linkPoints.size())
                       .arg(qRound(last.x()))
                       .arg(qRound(last.y())));
}

void CompareWorkspace::drawPixelLinkLines(QPainter &p)
{
    // Draw dashed connectors between the same marker index on cell 0 and cell 1
    // when both cells are visible in the grid (not split/swipe/overlay modes).
    if (m_cellViews.size() < 2 || !m_cellViews[0] || !m_cellViews[1])
        return;
    if (isSplitOrSwipe() || (m_overlayChk && m_overlayChk->isChecked()))
        return;

    auto mapToWorkspace = [this](RawImageView *view, const QPointF &imgPt) -> QPointF
    {
        if (!view || view->image().isNull() || view->scale() <= 0.0 ||
            !view->sourceSize().isValid())
            return {};
        // Image → widget (view-local), then map to this workspace. The view
        // owns the source-space transform so this seam stays identical to its
        // marker paint path even when the display raster is an LOD/region.
        const QPointF widgetPoint = view->sourcePointToWidget(imgPt);
        if (!std::isfinite(widgetPoint.x()) || !std::isfinite(widgetPoint.y()))
            return {};
        return view->mapTo(this, widgetPoint.toPoint());
    };

    QPen pen(QColor(0xFF, 0x66, 0x66, 180), 1, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    for (const QPointF &pt : m_linkPoints)
    {
        const QPointF a = mapToWorkspace(m_cellViews[0], pt);
        const QPointF b = mapToWorkspace(m_cellViews[1], pt);
        if (a.isNull() || b.isNull())
            continue;
        p.drawLine(a, b);
    }
}
