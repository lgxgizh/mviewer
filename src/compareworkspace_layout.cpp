#include "compareworkspace_p.h"

#include <QTimer>

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
