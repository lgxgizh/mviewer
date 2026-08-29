#include "thumbnailpanel_p.h"

void ThumbnailPanel::pruneThumbnailState()
{
    QMutexLocker lk(&m_thumbMtx);
    // Directory transitions clear this cache in resetDirectoryState(). Within
    // one directory, keep hot history across filter/sort rebuilds so A -> B ->
    // A selection does not trigger avoidable duplicate decodes. The hard
    // entry/byte budget is enforced on every insert and here as a convergence
    // point after a large model rebuild.
    enforceThumbPixmapBudgetLocked();
    m_thumbPending.clear();
    m_thumbFailed.clear();
}

QString ThumbnailPanel::thumbCacheKey(const QString &path, int size) const
{
    return path + QChar(0x1f) + QString::number(size);
}

void ThumbnailPanel::enforceThumbPixmapBudgetLocked()
{
    while (m_thumbReady.size() > kThumbPixmapCacheMaxEntries ||
           m_thumbReadyBytes > kThumbPixmapCacheMaxBytes)
    {
        auto oldest = m_thumbReady.end();
        for (auto it = m_thumbReady.begin(); it != m_thumbReady.end(); ++it)
        {
            if (oldest == m_thumbReady.end() || it->lastUse < oldest->lastUse)
                oldest = it;
        }
        if (oldest == m_thumbReady.end())
            break;
        m_thumbReadyBytes = qMax<qint64>(0, m_thumbReadyBytes - oldest->bytes);
        m_thumbReady.erase(oldest);
    }
}

void ThumbnailPanel::updateVisibleRange()
{
    const int n = m_model->rowCount();
    if (n == 0)
        return;
    // Geometry may not be ready yet (panel not shown / zero-size). Wait for
    // showEvent / resizeEvent before scheduling.
    const int viewportWidth = viewport()->width();
    const int viewportHeight = viewport()->height();
    if (viewportWidth < 10 || viewportHeight < 10)
        return;

    // Compute every view mode arithmetically. Calling indexAt()/visualRect()
    // here can force QListView to synchronously lay out the entire model.
    int first = 0;
    int last = 0;
    if (m_viewMode == ViewMode::List)
    {
        // ListMode wraps TopToBottom: fill one vertical column, then advance
        // horizontally. Both scroll offsets and item extents are pixel based.
        const int stepW = qMax(1, kListItemWidth + spacing());
        const int stepH = qMax(1, kListItemHeight + spacing());
        const int rowsPerColumn = qMax(1, viewportHeight / stepH);
        const int offset = qMax(0, horizontalScrollBar()->value());
        const int firstColumn = offset / stepW;
        const int visibleColumns =
            qMax(1, (offset % stepW + viewportWidth + stepW - 1) / stepW);
        first = firstColumn * rowsPerColumn;
        last = (firstColumn + visibleColumns) * rowsPerColumn - 1;
    }
    else if (m_viewMode == ViewMode::Details)
    {
        const int offset = qMax(0, verticalScrollBar()->value());
        const int visibleRows =
            qMax(1, (offset % kDetailsItemHeight + viewportHeight + kDetailsItemHeight - 1) /
                        kDetailsItemHeight);
        first = offset / kDetailsItemHeight;
        last = first + visibleRows - 1;
    }
    else if (m_viewMode == ViewMode::Filmstrip)
    {
        const int cellW = qMax(1, gridSize().width());
        const int offset = qMax(0, horizontalScrollBar()->value());
        const int visibleCells = qMax(1, (offset % cellW + viewportWidth + cellW - 1) / cellW);
        first = offset / cellW;
        last = first + visibleCells - 1;
    }
    else
    {
        // Icon grids are row-major and retain the established uniform-cell
        // formula used by Thumbnail/Large/Small/Compact modes.
        const QSize cell = gridSize();
        const int cellW = qMax(1, cell.width());
        const int cellH = qMax(1, cell.height());
        const int cols = qMax(1, viewportWidth / cellW);
        const int firstRow = verticalScrollBar()->value() / cellH;
        first = firstRow * cols;
        last = first + cols * qMax(1, viewportHeight / cellH);
    }
    first = qBound(0, first, n - 1);
    last = qBound(first, last, n - 1);

    // M25: the visible-range disk probe is GONE. It used to synchronously
    // stat + PNG-load every visible cell ON THE GUI THREAD, and it duplicated
    // the pipeline's own cache read (two cache paths could disagree on what
    // "cached" means). ThumbnailProvider::produce() on the worker is now the
    // single authoritative disk-cache path: warm folders paint as soon as the
    // worker's cache load lands (async, no UI-thread I/O).

    // P0 #鈶?/ A-2.5: visible range at Thumbnail priority, then predictive
    // neighbors. Scale the predictive window with directory size so 10k-image
    // folders still feel snappy when scrolling, without over-scheduling tiny
    // folders.
    const int predictive = n > 2000 ? 96 : (n > 500 ? 64 : 48);
    ThumbnailPipeline::instance().setPredictiveCount(static_cast<size_t>(predictive));
    ThumbnailPipeline::instance().setVisibleRange(static_cast<size_t>(first),
                                                  static_cast<size_t>(last + 1));
}

void ThumbnailPanel::onThumbReady(const QString &path)
{
    // M24 (S3): coalesce dataChanged. Emitting per decoded thumbnail triggers
    // a QListView layout pass per item; on a 10000-row gallery the per-item
    // emits stall the UI thread for seconds while thumbnails stream in. One
    // batched emit per event-loop turn keeps the view correct at ~30x less
    // layout work.
    if (!m_thumbDirty)
    {
        m_thumbDirty = true;
        QTimer::singleShot(0, this, [this]() { flushThumbUpdates(); });
    }
    m_thumbDirtyPaths.insert(path);
}

void ThumbnailPanel::flushThumbUpdates()
{
    m_thumbDirty = false;
    const int rows = m_model->rowCount();
    if (rows <= 0)
    {
        m_thumbDirtyPaths.clear();
        return;
    }

    QVector<int> dirtyRows;
    dirtyRows.reserve(m_thumbDirtyPaths.size());
    for (const QString &path : m_thumbDirtyPaths)
    {
        const int row = m_rowByPath.value(path, -1);
        if (row >= 0 && row < rows)
            dirtyRows.append(row);
    }
    m_thumbDirtyPaths.clear();
    if (dirtyRows.isEmpty())
        return;
    std::sort(dirtyRows.begin(), dirtyRows.end());
    int spanStart = dirtyRows.first();
    int spanEnd = spanStart;
    for (int i = 1; i < dirtyRows.size(); ++i)
    {
        if (dirtyRows.at(i) == spanEnd + 1)
        {
            spanEnd = dirtyRows.at(i);
            continue;
        }
        emit dataChanged(m_model->index(spanStart, 0), m_model->index(spanEnd, 0),
                         {Qt::DecorationRole});
        spanStart = spanEnd = dirtyRows.at(i);
    }
    emit dataChanged(m_model->index(spanStart, 0), m_model->index(spanEnd, 0),
                     {Qt::DecorationRole});
}

QPixmap ThumbnailPanel::thumbReady(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    auto it = m_thumbReady.find(thumbCacheKey(path, m_thumbSize));
    if (it == m_thumbReady.end())
        return QPixmap();
    it->lastUse = ++m_thumbReadyClock;
    return it->pixmap;
}

bool ThumbnailPanel::thumbFailed(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    return m_thumbFailed.contains(thumbCacheKey(path, m_thumbSize));
}

int ThumbnailPanel::thumbReadyCount() const
{
    QMutexLocker lk(&m_thumbMtx);
    return m_thumbReady.size();
}

qint64 ThumbnailPanel::thumbReadyBytes() const
{
    QMutexLocker lk(&m_thumbMtx);
    return m_thumbReadyBytes;
}
