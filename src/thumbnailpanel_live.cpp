// M56: incremental live-folder application for the Browse gallery.
#include "selectionmodel.h"
#include "thumbnailpanel_p.h"

#include "core/batch/BatchRename.h"
#include "core/image/ImageSortKeys.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailprovider.h"

#include <QScrollBar>

#include <algorithm>
#include <vector>

namespace
{

QString qpath(const std::string &path)
{
    return QString::fromUtf8(path.data(), static_cast<int>(path.size()));
}

// Snapshot paths are forward-slash normalized. Gallery rows keep
// QFileInfo::absoluteFilePath(), which uses '\\' on Windows. galleryPathKey
// makes those the same key; Windows equality stays case-insensitive.
QString galleryIndexKey(const QString &path)
{
    const QString key = ThumbnailPanel::galleryPathKey(path);
#ifdef Q_OS_WIN
    return key.toCaseFolded();
#else
    return key;
#endif
}

bool pathEqual(const QString &a, const QString &b)
{
    const QString left = ThumbnailPanel::galleryPathKey(a);
    const QString right = ThumbnailPanel::galleryPathKey(b);
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

ThumbnailPanel::Entry toPanelEntry(const mviewer::core::DirectoryEntry &entry)
{
    ThumbnailPanel::Entry result;
    result.path = qpath(entry.path);
    result.name = qpath(entry.filename);
    result.size = static_cast<qint64>(entry.size);
    result.date = QDateTime::fromMSecsSinceEpoch(entry.modifiedEpochMs);
    return result;
}

} // namespace

void ThumbnailPanel::applyDirectoryDelta(const mviewer::core::DirectoryDelta &delta)
{
    if (m_currentDir.isEmpty() || !pathEqual(m_currentDir, qpath(delta.path)))
        return;
    if (!delta.hasImageChanges() && !delta.hasSidecarChanges() && !delta.directoryUnavailable &&
        !delta.directoryRecovered)
        return;

    // Keep the last coherent gallery visible while the directory is
    // temporarily unavailable. MainWindow presents the explicit availability
    // status; clearing the model here would falsely look like an empty folder.
    if (delta.directoryUnavailable)
    {
        m_directoryUnavailable = true;
        return;
    }
    if (!delta.hasImageChanges() && !delta.directoryRecovered)
        return;

    QStringList previousSelection = selectedPaths();
    QString previousCurrent;
    QString anchorPath;
    int anchorOffset = 0;
    int previousCurrentRow = -1;
    captureIncrementalDeltaState(previousSelection, previousCurrent, anchorPath, anchorOffset,
                                 previousCurrentRow);
    m_transferState.mergeIntoNextSelection(previousSelection, previousCurrent);

    // The delta applies to the whole listing, including rows the type filter
    // hides, so the "N files hidden" hint and a later un-hide stay accurate.
    QList<Entry> next =
        delta.directoryRecovered ? QList<Entry>() : m_allEntries + m_typeHiddenEntries;
    m_directoryUnavailable = false;
    QStringList removedPaths;
    QStringList renamedFrom;
    QStringList renamedTo;
    applyDirectoryDeltaEntries(delta, next, removedPaths, renamedFrom, renamedTo);
    sortDirectoryDeltaEntries(next);
    adoptListing(std::move(next));

    // Rename is an identity migration, not a selection loss. Rewrite the
    // saved view selection before the row-local model mutation so both the
    // native selection and the app-wide SelectionModel can follow the path.
    for (const QString &oldPath : renamedFrom)
    {
        const QString newPath = renamedTo.value(renamedFrom.indexOf(oldPath));
        for (QString &selected : previousSelection)
            if (pathEqual(selected, oldPath))
                selected = newPath;
        if (pathEqual(previousCurrent, oldPath))
            previousCurrent = newPath;
    }

    m_incrementalApply = true;
    m_incrementalPrevSelection = previousSelection;
    m_incrementalPrevCurrent = previousCurrent;
    m_incrementalAnchorPath = anchorPath;
    m_incrementalAnchorOffset = anchorOffset;
    m_incrementalPreviousCurrentRow = previousCurrentRow;
    applyFilter();
    updateTypeHiddenHint();

    if (!renamedFrom.isEmpty())
        emit pathsRenamed(renamedFrom, renamedTo);
    if (!renamedFrom.isEmpty() && m_selection)
        m_selection->setSelection(previousSelection, previousCurrent);
    if (!removedPaths.isEmpty())
        emit pathsRemoved(removedPaths);
    QStringList modifiedPaths;
    for (const auto &entry : delta.modified)
        modifiedPaths.append(qpath(entry.path));
    if (!modifiedPaths.isEmpty())
        emit pathsModified(modifiedPaths);
}

static void
applyDeltaRemovalsAndModifications(const mviewer::core::DirectoryDelta &delta,
                                   QList<ThumbnailPanel::Entry> &next, QStringList &removedPaths,
                                   QStringList &renamedFrom, QStringList &renamedTo,
                                   const std::function<void(const QString &)> &invalidate)
{
    QHash<QString, int> nextIndex;
    nextIndex.reserve(next.size());
    for (int i = 0; i < next.size(); ++i)
        nextIndex.insert(galleryIndexKey(next[i].path), i);

    QList<int> removeIndices;
    removeIndices.reserve(delta.removed.size());
    for (const auto &entry : delta.removed)
    {
        const QString path = qpath(entry.path);
        auto idxIt = nextIndex.find(galleryIndexKey(path));
        if (idxIt != nextIndex.end())
            removeIndices.append(idxIt.value());
        removedPaths.append(path);
        invalidate(path);
    }
    std::sort(removeIndices.begin(), removeIndices.end(), std::greater<int>());
    for (int idx : removeIndices)
    {
        nextIndex.remove(galleryIndexKey(next[idx].path));
        next.removeAt(idx);
    }
    nextIndex.clear();
    nextIndex.reserve(next.size());
    for (int i = 0; i < next.size(); ++i)
        nextIndex.insert(galleryIndexKey(next[i].path), i);

    for (const auto &rename : delta.renamed)
    {
        const QString oldPath = qpath(rename.before.path);
        const QString newPath = qpath(rename.after.path);
        auto idxIt = nextIndex.find(galleryIndexKey(oldPath));
        if (idxIt != nextIndex.end())
        {
            const int idx = idxIt.value();
            ThumbnailPanel::Entry replacement = toPanelEntry(rename.after);
            replacement.width = next[idx].width;
            replacement.height = next[idx].height;
            next[idx] = replacement;
            nextIndex.remove(galleryIndexKey(oldPath));
            nextIndex.insert(galleryIndexKey(newPath), idx);
        }
        else
        {
            next.append(toPanelEntry(rename.after));
            nextIndex.insert(galleryIndexKey(newPath), next.size() - 1);
        }
        renamedFrom.append(oldPath);
        renamedTo.append(newPath);
        ThumbnailPipeline::instance().invalidatePath(oldPath.toUtf8().toStdString());
        ThumbnailProvider::invalidateSource(oldPath.toUtf8().toStdString());
    }
    for (const auto &entry : delta.modified)
    {
        const QString path = qpath(entry.path);
        auto idxIt = nextIndex.find(galleryIndexKey(path));
        if (idxIt != nextIndex.end())
        {
            const int idx = idxIt.value();
            ThumbnailPanel::Entry replacement = toPanelEntry(entry);
            replacement.width = next[idx].width;
            replacement.height = next[idx].height;
            next[idx] = replacement;
        }
        else
        {
            next.append(toPanelEntry(entry));
            nextIndex.insert(galleryIndexKey(path), next.size() - 1);
        }
        invalidate(path);
    }
    for (const auto &entry : delta.added)
    {
        const QString key = galleryIndexKey(qpath(entry.path));
        if (nextIndex.contains(key))
            continue;
        next.append(toPanelEntry(entry));
        nextIndex.insert(key, next.size() - 1);
    }
}

void ThumbnailPanel::applyDirectoryDeltaEntries(const mviewer::core::DirectoryDelta &delta,
                                                QList<Entry> &next, QStringList &removedPaths,
                                                QStringList &renamedFrom, QStringList &renamedTo)
{
    QStringList diskInvalidations;
    auto invalidate = [this, &diskInvalidations](const QString &path)
    {
        if (path.isEmpty())
            return;
        const std::string utf8 = path.toUtf8().toStdString();
        ThumbnailPipeline::instance().invalidatePath(utf8);
        invalidateThumbnailCacheFor(path);
        diskInvalidations.append(path);
    };

    applyDeltaRemovalsAndModifications(delta, next, removedPaths, renamedFrom, renamedTo,
                                       invalidate);

    for (const auto &rename : delta.renamed)
    {
        const QString oldPath = qpath(rename.before.path);
        const QString newPath = qpath(rename.after.path);
        const QString oldKey = galleryPathKey(oldPath);
        const QString newKey = galleryPathKey(newPath);
        m_metaIndex.insert(newKey, m_metaIndex.take(oldKey));
        m_metaIso.insert(newKey, m_metaIso.take(oldKey));
        m_metaCamera.insert(newKey, m_metaCamera.take(oldKey));
        m_metaLens.insert(newKey, m_metaLens.take(oldKey));
        if (pathEqual(m_pendingSelect, oldPath))
            m_pendingSelect = newPath;
    }

    if (!diskInvalidations.isEmpty())
    {
        const QStringList paths = diskInvalidations;
        TaskScheduler::instance().submit(TaskScheduler::MetadataPool,
                                         [paths]()
                                         {
                                             for (const QString &path : paths)
                                                 ThumbnailProvider::invalidateSource(
                                                     path.toUtf8().toStdString());
                                         });
    }
}

void ThumbnailPanel::sortDirectoryDeltaEntries(QList<Entry> &entries) const
{
    QHash<QString, int> ratingCache;
    if (m_sortMode == SortRating)
    {
        ratingCache.reserve(entries.size());
        auto &rs = mviewer::core::RatingStore::instance();
        for (const auto &e : entries)
            ratingCache.insert(e.path, rs.rating(e.path.toStdString()));
    }

    const auto field = static_cast<mviewer::core::BrowseSortField>(m_sortMode);
    auto compareEntries = [this, &ratingCache, field](const Entry &a, const Entry &b)
    {
        const int primary =
            browsePrimaryCompare(field, a, b, ratingCache, m_metaCamera, m_metaLens);
        return browseOrderedLess(primary, a.name, b.name, a.path, b.path, m_sortAscending);
    };
    std::stable_sort(entries.begin(), entries.end(), compareEntries);
}

void ThumbnailPanel::captureIncrementalDeltaState(QStringList &selection, QString &current,
                                                  QString &anchorPath, int &anchorOffset,
                                                  int &currentRow) const
{
    current = currentIndex().isValid() ? m_paths.value(currentIndex().row())
                                       : (m_selection ? m_selection->currentImage() : QString());
    currentRow = m_paths.indexOf(current);
    const QModelIndex anchorIndex = indexAt(QPoint(2, 2));
    if (!anchorIndex.isValid())
        return;
    anchorPath = m_paths.value(anchorIndex.row());
    anchorOffset = visualRect(anchorIndex).top() - viewport()->rect().top();
}

void ThumbnailPanel::captureScrollAnchor(QString &anchorPath, int &anchorOffset) const
{
    const bool horizontal = (m_viewMode == Filmstrip || m_viewMode == List);
    const int initialScroll = horizontal
                                  ? (horizontalScrollBar() ? horizontalScrollBar()->value() : 0)
                                  : (verticalScrollBar() ? verticalScrollBar()->value() : 0);
    if (initialScroll <= 0)
        return;

    QModelIndex anchorIndex =
        indexAt(QPoint(viewport()->rect().left() + 20, viewport()->rect().top() + 20));
    if (!anchorIndex.isValid())
        anchorIndex =
            indexAt(QPoint(viewport()->rect().left() + 10, viewport()->rect().top() + 10));
    if (!anchorIndex.isValid())
        anchorIndex =
            indexAt(QPoint(viewport()->rect().left() + 35, viewport()->rect().top() + 35));
    if (!anchorIndex.isValid())
    {
        const int cellH = qMax(1, gridSize().height());
        const int cellW = qMax(1, gridSize().width());
        const int cols = qMax(1, viewport()->width() / cellW);
        const int first = (initialScroll / cellH) * cols;
        if (first >= 0 && first < m_paths.size())
            anchorIndex = m_model->index(first, 0);
    }
    if (anchorIndex.isValid() && anchorIndex.row() >= 0 && anchorIndex.row() < m_paths.size())
    {
        anchorPath = m_paths.at(anchorIndex.row());
        anchorOffset = horizontal ? (visualRect(anchorIndex).left() - viewport()->rect().left())
                                  : (visualRect(anchorIndex).top() - viewport()->rect().top());
    }
}

void ThumbnailPanel::refreshSidecarPaths(const QStringList &paths)
{
    if (m_currentDir.isEmpty() || paths.isEmpty())
        return;

    const QStringList previousSelection = selectedPaths();
    const QString previousCurrent = currentIndex().isValid()
                                        ? m_paths.value(currentIndex().row())
                                        : (m_selection ? m_selection->currentImage() : QString());
    QString anchorPath;
    int anchorOffset = 0;
    captureScrollAnchor(anchorPath, anchorOffset);
    m_incrementalApply = true;
    m_incrementalPrevSelection = previousSelection;
    m_incrementalPrevCurrent = previousCurrent;
    m_incrementalAnchorPath = anchorPath;
    m_incrementalAnchorOffset = anchorOffset;
    m_incrementalPreviousCurrentRow = m_paths.indexOf(previousCurrent);
    applyFilter();
}

// Progressive rows land where the finished scan's sort will put them, so the
// list no longer jumps when the scan completes (directory order is not the
// natural-name order). Sort fields that need metadata snapshots (rating,
// camera, lens, resolution) are not known yet and still append.
void ThumbnailPanel::insertProgressiveRows(const QList<Entry> &batch)
{
    QString anchorPath;
    int anchorOffset = 0;
    captureScrollAnchor(anchorPath, anchorOffset);

    const auto field = static_cast<mviewer::core::BrowseSortField>(m_sortMode);
    const bool sortable = field == mviewer::core::BrowseSortField::Name ||
                          field == mviewer::core::BrowseSortField::Date ||
                          field == mviewer::core::BrowseSortField::Size ||
                          field == mviewer::core::BrowseSortField::Type;
    QList<Entry> sorted = batch;
    std::vector<int> oldPos(static_cast<size_t>(sorted.size()), m_displayEntries.size());
    if (sortable)
    {
        const QHash<QString, int> noRatings;
        const auto less = [this, field, &noRatings](const Entry &a, const Entry &b)
        {
            const int primary =
                browsePrimaryCompare(field, a, b, noRatings, m_metaCamera, m_metaLens);
            return browseOrderedLess(primary, a.name, b.name, a.path, b.path, m_sortAscending);
        };
        std::stable_sort(sorted.begin(), sorted.end(), less);
        for (int k = 0; k < sorted.size(); ++k)
            oldPos[static_cast<size_t>(k)] =
                static_cast<int>(std::upper_bound(m_displayEntries.cbegin(),
                                                  m_displayEntries.cend(), sorted.at(k), less) -
                                 m_displayEntries.cbegin());
    }
    // Existing rows shift by the number of new rows placed before them.
    if (!oldPos.empty() && oldPos.front() < m_displayEntries.size())
    {
        const auto shifted = [&oldPos](int row)
        {
            return row + static_cast<int>(std::upper_bound(oldPos.begin(), oldPos.end(), row) -
                                          oldPos.begin());
        };
        for (auto it = m_rowByPath.begin(); it != m_rowByPath.end(); ++it)
            it.value() = shifted(it.value());
        for (auto it = m_displayEntryRow.begin(); it != m_displayEntryRow.end(); ++it)
            it.value() = shifted(it.value());
    }
    for (int k = 0; k < sorted.size(); ++k)
    {
        const Entry &entry = sorted.at(k);
        const int row = oldPos[static_cast<size_t>(k)] + k;
        // One model insert per contiguous run (all appends -> one call).
        if (k == 0 || oldPos[static_cast<size_t>(k)] != oldPos[static_cast<size_t>(k - 1)])
        {
            int run = 1;
            while (k + run < sorted.size() &&
                   oldPos[static_cast<size_t>(k + run)] == oldPos[static_cast<size_t>(k)])
                ++run;
            if (!m_model->insertRows(row, run))
                return;
        }
        m_displayEntries.insert(row, entry);
        m_paths.insert(row, entry.path);
        const QString key = galleryPathKey(entry.path);
        m_rowByPath.insert(key, row);
        m_displayEntryRow.insert(key, row);
        m_sizeByPath.insert(key, entry.size);
        m_model->setData(m_model->index(row, 0), entry.name);
        m_totalBytes += entry.size;
    }
    if (!anchorPath.isEmpty())
        preserveScrollAnchor(anchorPath, anchorOffset);
}

static void syncModelRows(QAbstractItemModel *model, const QList<ThumbnailPanel::Entry> &entries,
                          QStringList &working)
{
    QSet<QString> desired;
    for (const auto &entry : entries)
        desired.insert(entry.path);

    for (int row = working.size() - 1; row >= 0; --row)
    {
        if (desired.contains(working.at(row)))
            continue;
        model->removeRows(row, 1);
        working.removeAt(row);
    }

    QHash<QString, int> workingIndex;
    workingIndex.reserve(working.size());
    for (int i = 0; i < working.size(); ++i)
        workingIndex.insert(working.at(i), i);

    for (int target = 0; target < entries.size(); ++target)
    {
        const auto &entry = entries.at(target);
        if (target < working.size() && working.at(target) == entry.path)
        {
            model->setData(model->index(target, 0), entry.name);
            continue;
        }
        const auto idxIt = workingIndex.find(entry.path);
        const int existing =
            (idxIt != workingIndex.end() && idxIt.value() >= target) ? idxIt.value() : -1;
        if (existing >= 0)
        {
            const QString moved = working.takeAt(existing);
            model->removeRows(existing, 1);
            model->insertRows(target, 1);
            model->setData(model->index(target, 0), entry.name);
            working.insert(target, moved);
            workingIndex.clear();
            for (int i = 0; i < working.size(); ++i)
                workingIndex.insert(working.at(i), i);
        }
        else
        {
            model->insertRows(target, 1);
            model->setData(model->index(target, 0), entry.name);
            working.insert(target, entry.path);
            workingIndex.clear();
            for (int i = 0; i < working.size(); ++i)
                workingIndex.insert(working.at(i), i);
        }
    }
    while (working.size() > entries.size())
    {
        const int row = working.size() - 1;
        model->removeRows(row, 1);
        working.removeAt(row);
    }
}

void ThumbnailPanel::applyDisplayedEntriesIncremental(const QList<Entry> &entries,
                                                      const QStringList &previousSelection,
                                                      const QString &previousCurrent,
                                                      const QString &anchorPath, int anchorOffset)
{
    QStringList working = m_paths;
    syncModelRows(m_model, entries, working);

    m_paths.clear();
    m_rowByPath.clear();
    m_sizeByPath.clear();
    m_displayEntries = entries;
    m_displayEntryRow.clear();
    m_displayEntryRow.reserve(entries.size());
    QStringList names;
    names.reserve(entries.size());
    m_totalBytes = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        const Entry &entry = entries.at(i);
        m_paths.append(entry.path);
        const QString key = galleryPathKey(entry.path);
        m_rowByPath.insert(key, i);
        m_displayEntryRow.insert(key, i);
        m_sizeByPath.insert(key, entry.size);
        names.append(entry.name);
        m_totalBytes += entry.size;
    }

    selectionModel()->clearSelection();
    QItemSelection restored;
    for (const QString &path : previousSelection)
    {
        const int row = m_rowByPath.value(galleryPathKey(path), -1);
        if (row >= 0)
            restored.select(m_model->index(row, 0), m_model->index(row, 0));
    }
    if (!restored.isEmpty())
        selectionModel()->select(restored, QItemSelectionModel::ClearAndSelect);

    QString nextCurrent = previousCurrent;
    if (!m_rowByPath.contains(galleryPathKey(nextCurrent)))
    {
        const int candidate =
            m_paths.isEmpty() ? -1 : qBound(0, m_incrementalPreviousCurrentRow, m_paths.size() - 1);
        nextCurrent = candidate >= 0 ? m_paths.at(candidate) : QString();
        if (m_selection)
        {
            QStringList validSelection;
            for (const QString &path : previousSelection)
                if (m_rowByPath.contains(galleryPathKey(path)))
                    validSelection.append(path);
            m_selection->setSelection(validSelection, nextCurrent);
        }
    }
    if (!nextCurrent.isEmpty() && m_rowByPath.contains(galleryPathKey(nextCurrent)))
    {
        // NoUpdate preserves multi-select restored above; plain setCurrentIndex
        // would ClearAndSelect and leave only the current path selected.
        const QModelIndex idx = m_model->index(m_rowByPath.value(galleryPathKey(nextCurrent)), 0);
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
    }
    else
    {
        selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }

    pruneThumbnailState();
    ThumbnailPipeline::instance().updateSourcesForConsumer(m_consumerId, toStdPaths(m_paths));
    emit sequenceChanged(m_currentDir, m_paths);
    emit statsChanged(m_paths.size(), m_totalBytes, restored.indexes().size(), 0);
    preserveScrollAnchor(anchorPath, anchorOffset);
    m_incrementalApply = false;
    m_incrementalPrevSelection.clear();
    m_incrementalPrevCurrent.clear();
    m_incrementalAnchorPath.clear();
    m_incrementalPreviousCurrentRow = -1;
}

void ThumbnailPanel::preserveScrollAnchor(const QString &anchorPath, int anchorOffset)
{
    if (anchorPath.isEmpty())
        return;
    const int row = m_rowByPath.value(galleryPathKey(anchorPath), -1);
    if (row < 0)
        return;
    const QModelIndex index = m_model->index(row, 0);
    if (!index.isValid())
        return;

    const bool horizontal = (m_viewMode == Filmstrip || m_viewMode == List);
    QScrollBar *bar = horizontal ? horizontalScrollBar() : verticalScrollBar();
    if (!bar)
        return;

    const QRect vr = visualRect(index);
    if (vr.isValid() && !vr.isEmpty())
    {
        const int currentPos = horizontal ? (vr.left() - viewport()->rect().left())
                                          : (vr.top() - viewport()->rect().top());
        const int delta = currentPos - anchorOffset;
        if (delta != 0)
            bar->setValue(bar->value() + delta);
    }
    else
    {
        scrollTo(index, QAbstractItemView::PositionAtTop);
        const int currentPos = horizontal ? (visualRect(index).left() - viewport()->rect().left())
                                          : (visualRect(index).top() - viewport()->rect().top());
        bar->setValue(bar->value() + currentPos - anchorOffset);
    }
}

namespace
{

QStringView nameExtension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? QStringView(name).mid(dot + 1) : QStringView();
}

} // namespace

bool browseOrderedLess(int primary, QStringView nameA, QStringView nameB, const QString &pathA,
                       const QString &pathB, bool ascending)
{
    if (primary != 0)
        return ascending ? primary < 0 : primary > 0;
    const int byName = compareNaturalName(nameA, nameB);
    if (byName != 0)
        return byName < 0;
    return QString::compare(ThumbnailPanel::galleryPathKey(pathA),
                            ThumbnailPanel::galleryPathKey(pathB), Qt::CaseSensitive) < 0;
}

int browsePrimaryCompare(mviewer::core::BrowseSortField field, const ThumbnailPanel::Entry &a,
                         const ThumbnailPanel::Entry &b, const QHash<QString, int> &ratingCache,
                         const QHash<QString, QString> &metaCamera,
                         const QHash<QString, QString> &metaLens)
{
    switch (field)
    {
    case mviewer::core::BrowseSortField::Name:
        return compareNaturalName(a.name, b.name);
    case mviewer::core::BrowseSortField::Date:
        if (a.date == b.date)
            return 0;
        return a.date < b.date ? -1 : 1;
    case mviewer::core::BrowseSortField::Size:
        if (a.size == b.size)
            return 0;
        return a.size < b.size ? -1 : 1;
    case mviewer::core::BrowseSortField::Resolution:
    {
        const qint64 left = static_cast<qint64>(a.width) * a.height;
        const qint64 right = static_cast<qint64>(b.width) * b.height;
        if (left == right)
            return 0;
        return left < right ? -1 : 1;
    }
    case mviewer::core::BrowseSortField::Type:
        return nameExtension(a.name).compare(nameExtension(b.name), Qt::CaseInsensitive);
    case mviewer::core::BrowseSortField::Rating:
        return ratingCache.value(a.path) - ratingCache.value(b.path);
    case mviewer::core::BrowseSortField::Camera:
        return QString::compare(metaCamera.value(ThumbnailPanel::galleryPathKey(a.path)),
                                metaCamera.value(ThumbnailPanel::galleryPathKey(b.path)),
                                Qt::CaseInsensitive);
    case mviewer::core::BrowseSortField::Lens:
        return QString::compare(metaLens.value(ThumbnailPanel::galleryPathKey(a.path)),
                                metaLens.value(ThumbnailPanel::galleryPathKey(b.path)),
                                Qt::CaseInsensitive);
    }
    return 0;
}

bool sortedFileLess(ThumbnailPanel::SortMode mode, bool ascending, const QString &nameA,
                    const mviewer::core::ImageSortKey &keyA, const QString &nameB,
                    const mviewer::core::ImageSortKey &keyB)
{
    int primary = 0;
    switch (mode)
    {
    case ThumbnailPanel::SortName:
        primary = compareNaturalName(nameA, nameB);
        break;
    case ThumbnailPanel::SortDate:
        if (keyA.mtimeSec != keyB.mtimeSec)
            primary = keyA.mtimeSec < keyB.mtimeSec ? -1 : 1;
        break;
    case ThumbnailPanel::SortSize:
        if (keyA.size != keyB.size)
            primary = keyA.size < keyB.size ? -1 : 1;
        break;
    case ThumbnailPanel::SortResolution:
        if (keyA.resolution != keyB.resolution)
            primary = keyA.resolution < keyB.resolution ? -1 : 1;
        break;
    case ThumbnailPanel::SortType:
        if (keyA.suffix != keyB.suffix)
            primary = keyA.suffix < keyB.suffix ? -1 : 1;
        break;
    case ThumbnailPanel::SortRating:
        if (keyA.rating != keyB.rating)
            primary = keyA.rating < keyB.rating ? -1 : 1;
        break;
    case ThumbnailPanel::SortCamera:
        if (keyA.camera != keyB.camera)
            primary = keyA.camera < keyB.camera ? -1 : 1;
        break;
    case ThumbnailPanel::SortLens:
        if (keyA.lens != keyB.lens)
            primary = keyA.lens < keyB.lens ? -1 : 1;
        break;
    }
    const QString pathA = QString::fromUtf8(keyA.path.data(), static_cast<int>(keyA.path.size()));
    const QString pathB = QString::fromUtf8(keyB.path.data(), static_cast<int>(keyB.path.size()));
    return browseOrderedLess(primary, nameA, nameB, pathA, pathB, ascending);
}

QString renameBlockedReason(const QString &directory, const QString &oldName,
                            const QString &newName)
{
    const QString nameError =
        QString::fromStdString(mviewer::core::fileNameError(newName.toStdString()));
    if (!nameError.isEmpty())
        return nameError;

    const QString oldPath = QDir(directory).filePath(oldName);
    const QString newPath = QDir(directory).filePath(newName);
    const QFileInfo destination(newPath);
    if (!destination.exists())
        return {};
    const QString oldCanonical = QFileInfo(oldPath).canonicalFilePath();
    const QString newCanonical = destination.canonicalFilePath();
    if (!oldCanonical.isEmpty() && oldCanonical == newCanonical)
        return {};
    return QStringLiteral("A file with the same name already exists.");
}
