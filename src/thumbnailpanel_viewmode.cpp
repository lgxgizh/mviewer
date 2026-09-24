// ThumbnailPanel view-mode configuration (M23 re-check: moved out of
// thumbnailpanel.cpp to keep that TU under the 800-line guard). setViewMode is a
// plain member function — it only touches ThumbnailPanel state and the shared
// delegates (all visible via thumbnailpanel_p.h), so it is safe to define here.
#include "thumbnailpanel_p.h"

#include <QSettings>
#include <optional>

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

// ---- Details column widths + interactive header ------------------------------

namespace
{
constexpr char kDetailWidthsKey[] = "ui/detailsColumnWidths";
}

void ThumbnailPanel::initDetailColumnWidths()
{
    for (int i = 0; i < DetailColCount; ++i)
        m_detailColW[i] = kDetailDefaultW[i];

    const QVariantList saved = QSettings().value(QLatin1String(kDetailWidthsKey)).toList();
    if (saved.size() != DetailColCount)
        return;
    for (int i = 0; i < DetailColCount; ++i)
    {
        bool ok = false;
        const int w = saved.at(i).toInt(&ok);
        if (ok)
            m_detailColW[i] = qMax(kDetailMinW[i], w);
    }
}

void ThumbnailPanel::persistDetailColumnWidths() const
{
    QVariantList list;
    list.reserve(DetailColCount);
    for (int i = 0; i < DetailColCount; ++i)
        list.append(m_detailColW[i]);
    QSettings().setValue(QLatin1String(kDetailWidthsKey), list);
}

void ThumbnailPanel::setDetailColumnWidth(DetailColumn column, int width)
{
    const int idx = static_cast<int>(column);
    if (idx < 0 || idx >= DetailColCount)
        return;
    const int clamped = qMax(kDetailMinW[idx], width);
    if (m_detailColW[idx] == clamped)
        return;
    m_detailColW[idx] = clamped;
    notifyDetailColumnsChanged();
}

int ThumbnailPanel::detailColumnWidth(DetailColumn column) const
{
    const int idx = static_cast<int>(column);
    if (idx < 0 || idx >= DetailColCount)
        return 0;
    return m_detailColW[idx];
}

int ThumbnailPanel::detailContentWidth() const
{
    return detailTotalWidth(m_detailColW);
}

void ThumbnailPanel::notifyDetailColumnsChanged()
{
    if (m_detailsHeader)
        m_detailsHeader->update();
    if (m_viewMode == Details)
    {
        scheduleDelayedItemsLayout();
        if (viewport())
            viewport()->update();
    }
}

namespace
{
std::optional<ThumbnailPanel::SortMode> sortModeForDetailColumn(ThumbnailPanel::DetailColumn col)
{
    switch (col)
    {
    case ThumbnailPanel::DetailColName:
        return ThumbnailPanel::SortName;
    case ThumbnailPanel::DetailColRes:
        return ThumbnailPanel::SortResolution;
    case ThumbnailPanel::DetailColSize:
        return ThumbnailPanel::SortSize;
    case ThumbnailPanel::DetailColDate:
        return ThumbnailPanel::SortDate;
    case ThumbnailPanel::DetailColFmt:
        return ThumbnailPanel::SortType;
    case ThumbnailPanel::DetailColRate:
        return ThumbnailPanel::SortRating;
    case ThumbnailPanel::DetailColCamera:
        return ThumbnailPanel::SortCamera;
    case ThumbnailPanel::DetailColLens:
        return ThumbnailPanel::SortLens;
    default:
        return std::nullopt;
    }
}
} // namespace

DetailsHeader::DetailsHeader(ThumbnailPanel *panel) : QWidget(panel), m_panel(panel)
{
    setObjectName(QStringLiteral("detailsHeader"));
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

QRect DetailsHeader::contentRect() const
{
    const int contentW = qMax(m_panel->detailContentWidth(), width());
    return QRect(0, 0, contentW, height());
}

int DetailsHeader::separatorAt(int x) const
{
    // x is in header widget coords; convert to content coords via h-scroll.
    const int contentX = x + m_panel->horizontalScrollBar()->value();
    const DetailLayout L = detailLayout(contentRect(), m_panel->detailColWidths());
    const QRect cols[] = {L.thumb, L.name,  L.res,    L.size, L.date, L.fmt,
                          L.rate,  L.label, L.camera, L.lens, L.iso};
    for (int i = 0; i < ThumbnailPanel::DetailColCount; ++i)
    {
        const int edge = cols[i].right() + 1;
        if (qAbs(contentX - edge) <= kDetailSepHitSlop)
            return i;
    }
    return -1;
}

int DetailsHeader::columnAt(int x) const
{
    const int contentX = x + m_panel->horizontalScrollBar()->value();
    const DetailLayout L = detailLayout(contentRect(), m_panel->detailColWidths());
    const QRect cols[] = {L.thumb, L.name,  L.res,    L.size, L.date, L.fmt,
                          L.rate,  L.label, L.camera, L.lens, L.iso};
    for (int i = 0; i < ThumbnailPanel::DetailColCount; ++i)
    {
        if (contentX >= cols[i].left() && contentX <= cols[i].right())
            return i;
    }
    return -1;
}

void DetailsHeader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect full(0, 0, width(), height());
    p.fillRect(full, palette().color(QPalette::Button));
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(full.bottomLeft(), full.bottomRight());

    const int xOff = -m_panel->horizontalScrollBar()->value();
    p.translate(xOff, 0);
    const DetailLayout L = detailLayout(contentRect(), m_panel->detailColWidths());
    p.setPen(palette().color(QPalette::ButtonText));
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    const int flags = Qt::AlignVCenter | Qt::TextSingleLine;

    const auto activeSortMode = m_panel->sortMode();
    const bool ascending = m_panel->sortAscending();
    const QString arrow = ascending ? QStringLiteral(" ▲") : QStringLiteral(" ▼");

    auto drawCol = [&](const QRect &rect, const QString &text, ThumbnailPanel::SortMode mode)
    {
        if (activeSortMode == mode)
            p.drawText(rect, flags, text + arrow);
        else
            p.drawText(rect, flags, text);
    };

    drawCol(L.name, QStringLiteral("名称"), ThumbnailPanel::SortName);
    drawCol(L.res, QStringLiteral("分辨率"), ThumbnailPanel::SortResolution);
    drawCol(L.size, QStringLiteral("大小"), ThumbnailPanel::SortSize);
    drawCol(L.date, QStringLiteral("修改日期"), ThumbnailPanel::SortDate);
    drawCol(L.fmt, QStringLiteral("格式"), ThumbnailPanel::SortType);
    drawCol(L.rate, QStringLiteral("评分"), ThumbnailPanel::SortRating);
    p.drawText(L.label, flags, QStringLiteral("标签"));
    drawCol(L.camera, QStringLiteral("相机"), ThumbnailPanel::SortCamera);
    drawCol(L.lens, QStringLiteral("镜头"), ThumbnailPanel::SortLens);
    p.drawText(L.iso, flags, QStringLiteral("ISO"));
}

void DetailsHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    const int x = event->position().toPoint().x();
    const int col = separatorAt(x);
    if (col >= 0)
    {
        m_dragCol = col;
        m_dragOriginX = x;
        m_dragOriginW = m_panel->detailColumnWidth(static_cast<ThumbnailPanel::DetailColumn>(col));
        setCursor(Qt::SplitHCursor);
        event->accept();
        return;
    }
    m_pressedCol = columnAt(x);
    event->accept();
}

void DetailsHeader::mouseMoveEvent(QMouseEvent *event)
{
    const int x = event->position().toPoint().x();
    if (m_dragCol >= 0)
    {
        const int delta = x - m_dragOriginX;
        m_panel->setDetailColumnWidth(static_cast<ThumbnailPanel::DetailColumn>(m_dragCol),
                                      m_dragOriginW + delta);
        event->accept();
        return;
    }
    setCursor(separatorAt(x) >= 0 ? Qt::SplitHCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void DetailsHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_dragCol >= 0)
        {
            m_dragCol = -1;
            m_panel->persistDetailColumnWidths();
            setCursor(separatorAt(event->position().toPoint().x()) >= 0 ? Qt::SplitHCursor
                                                                        : Qt::ArrowCursor);
            event->accept();
            return;
        }
        if (m_pressedCol >= 0)
        {
            const int releasedCol = columnAt(event->position().toPoint().x());
            if (releasedCol == m_pressedCol)
            {
                auto modeOpt = sortModeForDetailColumn(
                    static_cast<ThumbnailPanel::DetailColumn>(m_pressedCol));
                if (modeOpt.has_value())
                {
                    const ThumbnailPanel::SortMode targetMode = *modeOpt;
                    if (m_panel->sortMode() == targetMode)
                        m_panel->setSortAscending(!m_panel->sortAscending());
                    else
                        m_panel->setSort(targetMode, true);
                    update();
                }
            }
            m_pressedCol = -1;
            event->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void DetailsHeader::leaveEvent(QEvent *event)
{
    if (m_dragCol < 0)
    {
        setCursor(Qt::ArrowCursor);
        m_pressedCol = -1;
    }
    QWidget::leaveEvent(event);
}
