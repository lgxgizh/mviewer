// M56: incremental live-folder application for the Browse gallery.
#include "thumbnailpanel_p.h"
#include "selectionmodel.h"

#include "core/batch/BatchRename.h"
#include "core/image/ImageSortKeys.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailprovider.h"

#include <QScrollBar>

#include <algorithm>

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

    QList<Entry> next = delta.directoryRecovered ? QList<Entry>() : m_allEntries;
    m_directoryUnavailable = false;
    QStringList removedPaths;
    QStringList renamedFrom;
    QStringList renamedTo;
    applyDirectoryDeltaEntries(delta, next, removedPaths, renamedFrom, renamedTo);
    sortDirectoryDeltaEntries(next);

    m_allEntries = std::move(next);
    m_sourceRowByPath.clear();
    m_sourceRowByPath.reserve(m_allEntries.size());
    for (int i = 0; i < m_allEntries.size(); ++i)
        m_sourceRowByPath.insert(galleryPathKey(m_allEntries.at(i).path), i);

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

static void applyDeltaRemovalsAndModifications(
    const mviewer::core::DirectoryDelta &delta,
    QList<ThumbnailPanel::Entry> &next,
    QStringList &removedPaths,
    QStringList &renamedFrom,
    QStringList &renamedTo,
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
        next.append(toPanelEntry(entry));
    }
}

void ThumbnailPanel::applyDirectoryDeltaEntries(const mviewer::core::DirectoryDelta &delta,
                                                 QList<Entry> &next,
                                                 QStringList &removedPaths,
                                                 QStringList &renamedFrom,
                                                 QStringList &renamedTo)
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

    applyDeltaRemovalsAndModifications(delta, next, removedPaths, renamedFrom, renamedTo, invalidate);

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
    current = currentIndex().isValid()
                  ? m_paths.value(currentIndex().row())
                  : (m_selection ? m_selection->currentImage() : QString());
    currentRow = m_paths.indexOf(current);
    const QModelIndex anchorIndex = indexAt(QPoint(2, 2));
    if (!anchorIndex.isValid())
        return;
    anchorPath = m_paths.value(anchorIndex.row());
    anchorOffset = visualRect(anchorIndex).top() - viewport()->rect().top();
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
    const QModelIndex anchorIndex = indexAt(QPoint(2, 2));
    if (anchorIndex.isValid())
    {
        anchorPath = m_paths.value(anchorIndex.row());
        anchorOffset = visualRect(anchorIndex).top() - viewport()->rect().top();
    }
    m_incrementalApply = true;
    m_incrementalPrevSelection = previousSelection;
    m_incrementalPrevCurrent = previousCurrent;
    m_incrementalAnchorPath = anchorPath;
    m_incrementalAnchorOffset = anchorOffset;
    m_incrementalPreviousCurrentRow = m_paths.indexOf(previousCurrent);
    applyFilter();
}

static void syncModelRows(QAbstractItemModel *model,
                           const QList<ThumbnailPanel::Entry> &entries,
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
        const int existing = (idxIt != workingIndex.end() && idxIt.value() >= target)
                                 ? idxIt.value()
                                 : -1;
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
        const int candidate = m_paths.isEmpty()
                                  ? -1
                                  : qBound(0, m_incrementalPreviousCurrentRow, m_paths.size() - 1);
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
    ThumbnailPipeline::instance().updateSources(toStdPaths(m_paths));
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
    scrollTo(index, QAbstractItemView::PositionAtTop);
    if (QScrollBar *bar = verticalScrollBar())
    {
        const int currentTop = visualRect(index).top() - viewport()->rect().top();
        bar->setValue(bar->value() + currentTop - anchorOffset);
    }
}

namespace
{

int endOfDigits(QStringView text, int start)
{
    int end = start;
    while (end < text.size() && text.at(end).isDigit())
        ++end;
    return end;
}

int compareDigitRun(QStringView left, QStringView right)
{
    int i = 0;
    int j = 0;
    while (i + 1 < left.size() && left.at(i) == QLatin1Char('0'))
        ++i;
    while (j + 1 < right.size() && right.at(j) == QLatin1Char('0'))
        ++j;
    const int lenLeft = left.size() - i;
    const int lenRight = right.size() - j;
    if (lenLeft != lenRight)
        return lenLeft < lenRight ? -1 : 1;
    const int cmp = left.mid(i).compare(right.mid(j));
    if (cmp != 0)
        return cmp < 0 ? -1 : 1;
    if (left.size() != right.size())
        return left.size() < right.size() ? -1 : 1;
    return 0;
}

QStringView nameExtension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? QStringView(name).mid(dot + 1) : QStringView();
}

} // namespace

int compareNaturalName(QStringView left, QStringView right)
{
    int i = 0;
    int j = 0;
    while (i < left.size() && j < right.size())
    {
        const bool leftDigit = left.at(i).isDigit();
        const bool rightDigit = right.at(j).isDigit();
        if (leftDigit || rightDigit)
        {
            if (leftDigit != rightDigit)
                return leftDigit ? -1 : 1;
            const int endLeft = endOfDigits(left, i);
            const int endRight = endOfDigits(right, j);
            const int cmp = compareDigitRun(left.mid(i, endLeft - i), right.mid(j, endRight - j));
            if (cmp != 0)
                return cmp;
            i = endLeft;
            j = endRight;
            continue;
        }
        int endLeft = i;
        while (endLeft < left.size() && !left.at(endLeft).isDigit())
            ++endLeft;
        int endRight = j;
        while (endRight < right.size() && !right.at(endRight).isDigit())
            ++endRight;
        const int cmp =
            left.mid(i, endLeft - i).compare(right.mid(j, endRight - j), Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0 ? -1 : 1;
        i = endLeft;
        j = endRight;
    }
    if (i == left.size() && j == right.size())
        return 0;
    return i == left.size() ? -1 : 1;
}

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
    return QStringLiteral("同名文件已存在。");
}
