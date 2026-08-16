// ThumbnailPanel view-mode configuration (M23 re-check: moved out of
// thumbnailpanel.cpp to keep that TU under the 800-line guard). setViewMode is a
// plain member function — it only touches ThumbnailPanel state and the shared
// delegates (all visible via thumbnailpanel_p.h), so it is safe to define here.
#include "thumbnailpanel_p.h"

void ThumbnailPanel::replaceDelegate(QStyledItemDelegate *delegate)
{
    QStyledItemDelegate *previous = m_delegate;
    m_delegate = delegate;
    setItemDelegate(m_delegate);
    if (previous)
        previous->deleteLater();
}

void ThumbnailPanel::configureLargeIconMode()
{
    applyThumbSize(240, false);
    QListView::setViewMode(QListView::IconMode);
    setWrapping(true);
    setUniformItemSizes(true);
    setGridSize(QSize(m_thumbSize + 24, m_thumbSize + 62));
    setSpacing(8);
    setResizeMode(QListView::Adjust);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    replaceDelegate(new ThumbDelegate(this, this));
}

void ThumbnailPanel::configureSmallIconMode()
{
        applyThumbSize(64, false);
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 12, m_thumbSize + 30));
        setSpacing(4);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        replaceDelegate(new ThumbDelegate(this, this));
}

void ThumbnailPanel::configureDetailsMode()
{
        QListView::setViewMode(QListView::ListMode);
        setWrapping(false);
        setUniformItemSizes(false);
        setGridSize(QSize());
        setIconSize(QSize(48, 48));
        setSpacing(0);
        // Swap to the details delegate.
        replaceDelegate(new DetailsDelegate(this, this));
        // EXIF columns (camera/lens/ISO) can make the row wider than the viewport;
        // allow horizontal scrolling rather than overlapping columns.
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
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

void ThumbnailPanel::configureFilmstripMode()
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
        replaceDelegate(new ThumbDelegate(this, this));

}

void ThumbnailPanel::configureCompactMode()
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
        replaceDelegate(new ThumbDelegate(this, this));

}

void ThumbnailPanel::configureListMode()
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
        replaceDelegate(new ListDelegate(this, this));

}

void ThumbnailPanel::configureThumbnailMode()
{
        QListView::setViewMode(QListView::IconMode);
        setWrapping(true);
        setUniformItemSizes(true);
        setGridSize(QSize(m_thumbSize + 24, m_thumbSize + 62));
        setSpacing(6);
        setResizeMode(QListView::Adjust);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        replaceDelegate(new ThumbDelegate(this, this));

}

void ThumbnailPanel::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode)
    {
        if (mode == LargeIcon)
            applyThumbSize(240, false);
        else if (mode == SmallIcon)
            applyThumbSize(64, false);
        else if (mode == Thumbnail)
            applyThumbSize(m_gridThumbSize, true);
        emit viewModeChanged(m_viewMode);
        return;
    }
    m_viewMode = mode;
    setBatchSize(256);
    if (mode != Details)
    {
        setViewportMargins(0, 0, 0, 0);
        if (m_detailsHeader)
            m_detailsHeader->hide();
    }
    if (mode == LargeIcon)
    {
        configureLargeIconMode();
        emit viewModeChanged(m_viewMode);
        return;
    }
    if (mode == SmallIcon)
    {
        configureSmallIconMode();
        emit viewModeChanged(m_viewMode);
        return;
    }
    if (mode == Details)
        configureDetailsMode();
    else if (mode == Filmstrip)
        configureFilmstripMode();
    else if (mode == Compact)
        configureCompactMode();
    else if (mode == List)
        configureListMode();
    else
        configureThumbnailMode();
    if (mode == Thumbnail)
        setThumbSize(m_gridThumbSize);
    emit viewModeChanged(m_viewMode);
}
