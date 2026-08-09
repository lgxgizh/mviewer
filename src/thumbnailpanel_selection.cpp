#include "thumbnailpanel_p.h"

void ThumbnailPanel::scrollToPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    setCurrentIndex(idx);
    scrollTo(idx, PositionAtCenter);
}

void ThumbnailPanel::selectPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
    {
        // M24 (A#8): the path may belong to an async directory rescan that has
        // not landed yet (rename/refresh/restore). Park it: buildModel applies
        // it when the model containing the path arrives. If the path never
        // appears, the next successful selectPath() overwrites it.
        if (!path.isEmpty())
            m_pendingSelect = path;
        return;
    }
    const QModelIndex idx = m_model->index(row, 0);
    if (currentIndex() == idx && selectionModel() && selectionModel()->isSelected(idx))
        return; // already the current selected item 鈥?nothing to do, no scroll jank
    // If the path is already part of a multi-selection, only move the current
    // focus 鈥?do NOT ClearAndSelect (that would collapse multi-select).
    if (selectionModel() && selectionModel()->isSelected(idx))
    {
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        scrollTo(idx);
        return;
    }
    // Single-path focus: replace selection with this item.
    if (selectionModel())
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    else
        setCurrentIndex(idx);
    scrollTo(idx); // default EnsureVisible: only scrolls when off-screen
}

void ThumbnailPanel::selectPaths(const QStringList &paths, const QString &current)
{
    if (!selectionModel() || !m_model)
        return;
    QItemSelection sel;
    for (const QString &p : paths)
    {
        const int row = m_rowByPath.value(p, -1);
        if (row < 0)
            continue;
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
    }
    // Block currentChanged 鈫?itemClicked while we rebuild multi-select, so the
    // focus move cannot collapse SelectionModel via setCurrentImage.
    const bool wasBlocked = selectionModel()->blockSignals(true);
    selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    const QString focus =
        !current.isEmpty() ? current : (paths.isEmpty() ? QString() : paths.first());
    if (!focus.isEmpty())
    {
        const int row = m_rowByPath.value(focus, -1);
        if (row >= 0)
        {
            const QModelIndex idx = m_model->index(row, 0);
            selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            scrollTo(idx);
        }
    }
    selectionModel()->blockSignals(wasBlocked);
    // Emit a single selectionChanged so MainWindow can re-sync if needed.
    emit selectionModel() -> selectionChanged(selectionModel()->selection(), QItemSelection());
}

int ThumbnailPanel::scrollOffset() const
{
    return verticalScrollBar()->value();
}

QStringList ThumbnailPanel::selectedPaths() const
{
    QStringList r;
    for (const QModelIndex &idx : selectionModel()->selectedIndexes())
        r.append(m_paths.value(idx.row()));
    return r;
}
