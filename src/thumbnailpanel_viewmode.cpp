// ThumbnailPanel view-mode configuration (M23 re-check: moved out of
// thumbnailpanel.cpp to keep that TU under the 800-line guard). setViewMode is a
// plain member function — it only touches ThumbnailPanel state and the shared
// delegates (all visible via thumbnailpanel_p.h), so it is safe to define here.
#include "thumbnailpanel_p.h"

void ThumbnailPanel::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
        return;
    m_viewMode = mode;

    // Large folders (1000+ images) scroll noticeably smoother with a larger
    // layout batch and a persistent scrollbar (no layout jump when items appear).
    setBatchSize(256);

    // P0-4: only the Details view reserves a top margin for the column header.
    // Reset here so switching away from Details restores the full viewport.
    if (mode != Details)
    {
        setViewportMargins(0, 0, 0, 0);
        if (m_detailsHeader)
            m_detailsHeader->hide();
    }

    // P0-2: Large/Small icon modes are thumbnail grids with fixed sizes.
    if (mode == LargeIcon)
    {
        setThumbSize(240);
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
        setSpacing(8);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
        return;
    }
    if (mode == SmallIcon)
    {
        setThumbSize(64);
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 12, m_thumbSize + 30));
        setSpacing(4);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
        return;
    }

    if (mode == Details)
    {
        QListView::setViewMode(QListView::ListMode);
        setWrapping(false);
        setUniformItemSizes(false);
        setGridSize(QSize());
        setIconSize(QSize(48, 48));
        setSpacing(0);
        // Swap to the details delegate.
        if (m_delegate)
            delete m_delegate;
        m_delegate = new DetailsDelegate(this, this);
        setItemDelegate(m_delegate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // Reserve space for and show the column header.
        if (!m_detailsHeader)
            m_detailsHeader = new DetailsHeader(this);
        setViewportMargins(0, kDetailsHeaderH, 0, 0);
        m_detailsHeader->show();
        positionDetailsHeader();
        // Details is the only view that shows the resolution column, so this is
        // where we pay the (deferred, background) header-read cost.
        ensureDimensions();
    }
    else if (mode == Filmstrip)
    {
        // M15: horizontal single-row strip, no wrapping
        QListView::setViewMode(QListView::IconMode);
        setWrapping(false);
        setUniformItemSizes(true);
        const int stripH = qMax(m_thumbSize, 64) + 18;
        setGridSize(QSize(stripH, stripH));
        setSpacing(4);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setResizeMode(QListView::Fixed);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
    else if (mode == Compact)
    {
        // M15: dense grid, minimised padding
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        const int compactS = qMax(m_thumbSize / 3, 32);
        setGridSize(QSize(compactS + 4, compactS + 14));
        setSpacing(2);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
    else if (mode == List)
    {
        // P0: Windows-Explorer-style list — small icon + name, wrapping into
        // columns. Uses the lightweight ListDelegate (no resolution column).
        QListView::setViewMode(QListView::ListMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize());
        setSpacing(2);
        setIconSize(QSize(16, 16));
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ListDelegate(this, this);
        setItemDelegate(m_delegate);
    }
    else
    {
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
        setSpacing(6);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        if (m_delegate)
            delete m_delegate;
        m_delegate = new ThumbDelegate(this, this);
        setItemDelegate(m_delegate);
    }
}
