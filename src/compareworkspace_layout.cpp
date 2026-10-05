#include "compareworkspace_caption.h"
#include "compareworkspace_p.h"

#include <QTimer>

#include <utility>

// Re-grid existing pane widgets when only the column mapping changes.
// Keeps RawImageView instances, display rasters, ROI, and overlays alive.
void CompareWorkspace::relayoutGridKeepingPanes()
{
    if (!m_layout || m_cellViews.isEmpty())
        return;

    endTemporaryCompare();

    for (RawImageView *view : m_cellViews)
    {
        QWidget *pane = view ? view->parentWidget() : nullptr;
        if (pane && m_layout->indexOf(pane) < 0)
            m_layout->addWidget(pane);
    }

    while (QLayoutItem *item = m_layout->takeAt(0))
        delete item; // widgets stay owned by their parents

    for (int r = 0; r < m_layout->rowCount(); ++r)
        m_layout->setRowStretch(r, 0);
    for (int c = 0; c < m_layout->columnCount(); ++c)
        m_layout->setColumnStretch(c, 0);

    const int n = m_cellViews.size();
    const int columns = std::max(1, m_engine.layout().cols);
    for (int i = 0; i < n; ++i)
    {
        RawImageView *view = m_cellViews[i];
        QWidget *pane = view ? view->parentWidget() : nullptr;
        if (!pane)
            continue;
        const int row = i / columns;
        const int col = i % columns;
        m_layout->addWidget(pane, row, col);
        m_layout->setRowStretch(row, 1);
        m_layout->setColumnStretch(col, 1);
    }

    schedulePostLayoutFit();
    refreshLinkMarkers();
    QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
    update();
}

namespace
{

void swapPaneViewsAndCaptions(RawImageView *va, RawImageView *vb, QLabel *ca, QLabel *cb,
                              const ImageFrame *fa, const ImageFrame *fb, bool filenameOverlay)
{
    if (va && vb)
    {
        const QImage ia = va->displayImage(), oa = va->overlay();
        const QImage ib = vb->displayImage(), ob = vb->overlay();
        const QSize sa = va->sourceSize(), sb = vb->sourceSize();
        const QRect ra = va->sourceRect(), rb = vb->sourceRect();
        const double aa = va->overlayOpacity(), ab = vb->overlayOpacity();
        va->setImage(ib, sb, rb);
        vb->setImage(ia, sa, ra);
        va->setOverlay(ob, ab);
        vb->setOverlay(oa, aa);
        const QString tagA = va->paneTag();
        va->setPaneTag(vb->paneTag());
        vb->setPaneTag(tagA);
    }
    if (ca && cb)
    {
        auto nameOf = [](const ImageFrame *img) -> QString
        {
            if (!img)
                return {};
            return QString::fromUtf8(img->metadata().fileName.data(),
                                     static_cast<int>(img->metadata().fileName.size()));
        };
        setComparePaneCaptionText(ca, nameOf(fa));
        setComparePaneCaptionText(cb, nameOf(fb));
        if (va)
            va->setFilenameOverlay(comparePaneCaptionFullText(ca), filenameOverlay);
        if (vb)
            vb->setFilenameOverlay(comparePaneCaptionFullText(cb), filenameOverlay);
    }
}

int resolveSwapTarget(int focusIdx, int editIdx, int count)
{
    if (focusIdx > 0 && focusIdx < count)
        return focusIdx;
    return (editIdx > 0 && editIdx < count) ? editIdx : 1;
}

void swapIndexIfMatch(int &val, int a, int b)
{
    if (val == a)
        val = b;
    else if (val == b)
        val = a;
}

void swapSessionPaneSlots(mviewer::ui::CompareSessionRuntime &session, int a, int b)
{
    auto &pyramids = session.panePyramids;
    if (pyramids.size() <= static_cast<size_t>(b))
        pyramids.resize(static_cast<size_t>(b) + 1);
    std::swap(pyramids[static_cast<size_t>(a)], pyramids[static_cast<size_t>(b)]);

    auto &frames = session.frameIndices;
    if (static_cast<size_t>(b) < frames.size())
        std::swap(frames[static_cast<size_t>(a)], frames[static_cast<size_t>(b)]);

    // The next setImage() (pyramid paint or async delivery) fits the raster.
    // Pin the panes so that fit is replaced with the swapped cell transform.
    session.reapplyCellTransform = {a, b};
}

} // namespace

void CompareWorkspace::reapplyPinnedCellTransforms(bool clearPins)
{
    if (!m_session)
        return;
    for (int index : m_session->reapplyCellTransform)
    {
        if (index < 0 || index >= m_cellViews.size())
            continue;
        RawImageView *view = m_cellViews[index];
        if (!view)
            continue;
        const auto cell = m_engine.cellTransform(index);
        const QPointF offset = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                                    m_engine.syncTransform().offset.y)
                                          : QPointF(cell.offset.x, cell.offset.y);
        view->setTransform(cell.scale, offset);
    }
    if (clearPins)
        m_session->reapplyCellTransform.clear();
}

void CompareWorkspace::onSwapPanes()
{
    const int n = m_cellViews.size();
    if (n < 2)
        return;

    const int a = 0;
    const int b = resolveSwapTarget(m_focusIndex, m_editIdx, n);
    if (b <= 0 || b >= n)
        return;

    endTemporaryCompare();

    m_engine.swapFrames(a, b);
    if (static_cast<size_t>(b) < m_cellAdjusts.size())
        std::swap(m_cellAdjusts[0], m_cellAdjusts[static_cast<size_t>(b)]);

    const bool blinkActive = m_blinkChk && m_blinkChk->isChecked();
    if (blinkActive)
    {
        rebuildCells();
        schedulePostLayoutFit();
    }
    else
    {
        if (static_cast<size_t>(b) < m_comparePaths.size())
            std::swap(m_comparePaths[0], m_comparePaths[static_cast<size_t>(b)]);
        if (m_session)
            swapSessionPaneSlots(*m_session, a, b);
        if (a < m_fitScales.size() && b < m_fitScales.size())
            std::swap(m_fitScales[a], m_fitScales[b]);

        RawImageView *va = m_cellViews.value(a, nullptr);
        RawImageView *vb = m_cellViews.value(b, nullptr);
        QLabel *ca = m_cellLabels.value(a, nullptr);
        QLabel *cb = m_cellLabels.value(b, nullptr);
        // setImage() fits each incoming raster. scheduleDisplayMaterialization()
        // may fit again from the pyramid. Both run before this returns; the pin
        // set above puts the swapped scale and pan back on the views, and the
        // async delivery does the same when its raster lands.
        swapPaneViewsAndCaptions(va, vb, ca, cb, m_engine.imageAt(a), m_engine.imageAt(b),
                                 m_filenameOverlay);
        swapIndexIfMatch(m_focusIndex, a, b);
        swapIndexIfMatch(m_editIdx, a, b);

        scheduleDisplayMaterialization({a, b});
        reapplyPinnedCellTransforms();
        refreshAllDiffOverlays();
        updateTemporaryCompareAvailability();
    }

    if (m_sidePanel && m_sidePanel->isVisible())
        refreshHistograms();
    update();

    // Drop a window-fit queued before this swap, and one coalesced by the
    // layout pass above. A fit queued after we return still runs. Blink keeps
    // its own post-rebuild fit.
    if (!blinkActive && m_session)
    {
        ++m_session->cellTransformEpoch;
        m_session->postLayoutFitAgain = false;
    }
}
