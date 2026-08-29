// M56: incremental live-folder application for the Browse gallery.
#include "thumbnailpanel_p.h"
#include "selectionmodel.h"

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

bool pathEqual(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
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
        m_sourceRowByPath.insert(m_allEntries.at(i).path, i);

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

void ThumbnailPanel::applyDirectoryDeltaEntries(const mviewer::core::DirectoryDelta &delta,
                                                 QList<Entry> &next,
                                                 QStringList &removedPaths,
                                                 QStringList &renamedFrom,
                                                 QStringList &renamedTo)
{
    auto findPath = [&](const QString &path)
    {
        return std::find_if(next.begin(), next.end(),
                            [&](const Entry &entry) { return pathEqual(entry.path, path); });
    };
    auto invalidate = [](const QString &path)
    {
        if (path.isEmpty())
            return;
        ThumbnailPipeline::instance().invalidatePath(path.toUtf8().toStdString());
        ThumbnailProvider::invalidateSource(path.toUtf8().toStdString());
    };
    for (const auto &entry : delta.removed)
    {
        const QString path = qpath(entry.path);
        const auto it = findPath(path);
        if (it != next.end())
            next.erase(it);
        removedPaths.append(path);
        invalidate(path);
    }
    for (const auto &rename : delta.renamed)
    {
        const QString oldPath = qpath(rename.before.path);
        const QString newPath = qpath(rename.after.path);
        const auto it = findPath(oldPath);
        if (it != next.end())
        {
            Entry replacement = toPanelEntry(rename.after);
            replacement.width = it->width;
            replacement.height = it->height;
            *it = replacement;
        }
        else
            next.append(toPanelEntry(rename.after));
        renamedFrom.append(oldPath);
        renamedTo.append(newPath);
        ThumbnailPipeline::instance().invalidatePath(oldPath.toUtf8().toStdString());
        ThumbnailProvider::invalidateSource(oldPath.toUtf8().toStdString());
    }
    for (const auto &entry : delta.modified)
    {
        const QString path = qpath(entry.path);
        const auto it = findPath(path);
        if (it != next.end())
        {
            Entry replacement = toPanelEntry(entry);
            replacement.width = it->width;
            replacement.height = it->height;
            *it = replacement;
        }
        else
            next.append(toPanelEntry(entry));
        invalidate(path);
    }
    for (const auto &entry : delta.added)
        next.append(toPanelEntry(entry));

    for (const auto &rename : delta.renamed)
    {
        const QString oldPath = qpath(rename.before.path);
        const QString newPath = qpath(rename.after.path);
        m_metaIndex.insert(newPath, m_metaIndex.take(oldPath));
        m_metaIso.insert(newPath, m_metaIso.take(oldPath));
        m_metaCamera.insert(newPath, m_metaCamera.take(oldPath));
        m_metaLens.insert(newPath, m_metaLens.take(oldPath));
        if (m_pendingSelect == oldPath)
            m_pendingSelect = newPath;
    }
}

void ThumbnailPanel::sortDirectoryDeltaEntries(QList<Entry> &entries) const
{
    auto compareEntries = [this](const Entry &a, const Entry &b)
    {
        int order = 0;
        switch (m_sortMode)
        {
        case SortName:
            order = QString::compare(a.path, b.path, Qt::CaseSensitive);
            break;
        case SortDate:
            if (a.date != b.date)
                order = a.date > b.date ? -1 : 1;
            break;
        case SortSize:
            if (a.size != b.size)
                order = a.size > b.size ? -1 : 1;
            break;
        case SortResolution:
        {
            const qint64 ar = static_cast<qint64>(a.width) * a.height;
            const qint64 br = static_cast<qint64>(b.width) * b.height;
            if (ar != br)
                order = ar > br ? -1 : 1;
            break;
        }
        case SortType:
        {
            const QString ae = QFileInfo(a.path).suffix().toLower();
            const QString be = QFileInfo(b.path).suffix().toLower();
            order = QString::compare(ae, be, Qt::CaseInsensitive);
            break;
        }
        case SortRating:
        {
            const int ar = mviewer::core::RatingStore::instance().rating(a.path.toStdString());
            const int br = mviewer::core::RatingStore::instance().rating(b.path.toStdString());
            if (ar != br)
                order = ar < br ? -1 : 1;
            break;
        }
        case SortCamera:
            order = QString::compare(m_metaCamera.value(a.path), m_metaCamera.value(b.path),
                                     Qt::CaseInsensitive);
            break;
        case SortLens:
            order = QString::compare(m_metaLens.value(a.path), m_metaLens.value(b.path),
                                     Qt::CaseInsensitive);
            break;
        }
        if (order == 0)
            order = QString::compare(a.path, b.path, Qt::CaseSensitive);
        return m_sortAscending ? order < 0 : order > 0;
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

void ThumbnailPanel::applyDisplayedEntriesIncremental(const QList<Entry> &entries,
                                                       const QStringList &previousSelection,
                                                       const QString &previousCurrent,
                                                       const QString &anchorPath, int anchorOffset)
{
    QStringList working = m_paths;
    QSet<QString> desired;
    for (const Entry &entry : entries)
        desired.insert(entry.path);

    // Remove only rows that disappeared or no longer match the active filter.
    for (int row = working.size() - 1; row >= 0; --row)
    {
        if (desired.contains(working.at(row)))
            continue;
        m_model->removeRows(row, 1);
        working.removeAt(row);
    }

    // Reorder through row-local remove/insert operations. This preserves the
    // model object and avoids beginResetModel()/setStringList() on every hint.
    for (int target = 0; target < entries.size(); ++target)
    {
        const Entry &entry = entries.at(target);
        if (target < working.size() && working.at(target) == entry.path)
        {
            m_model->setData(m_model->index(target, 0), entry.name);
            continue;
        }
        const int existing = working.indexOf(entry.path, target);
        if (existing >= 0)
        {
            const QString moved = working.takeAt(existing);
            m_model->removeRows(existing, 1);
            m_model->insertRows(target, 1);
            m_model->setData(m_model->index(target, 0), entry.name);
            working.insert(target, moved);
        }
        else
        {
            m_model->insertRows(target, 1);
            m_model->setData(m_model->index(target, 0), entry.name);
            working.insert(target, entry.path);
        }
    }
    while (working.size() > entries.size())
    {
        const int row = working.size() - 1;
        m_model->removeRows(row, 1);
        working.removeAt(row);
    }

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
        m_rowByPath.insert(entry.path, i);
        m_displayEntryRow.insert(entry.path, i);
        m_sizeByPath.insert(entry.path, entry.size);
        names.append(entry.name);
        m_totalBytes += entry.size;
    }

    selectionModel()->clearSelection();
    QItemSelection restored;
    for (const QString &path : previousSelection)
    {
        const int row = m_rowByPath.value(path, -1);
        if (row >= 0)
            restored.select(m_model->index(row, 0), m_model->index(row, 0));
    }
    if (!restored.isEmpty())
        selectionModel()->select(restored, QItemSelectionModel::ClearAndSelect);

    QString nextCurrent = previousCurrent;
    if (!m_rowByPath.contains(nextCurrent))
    {
        const int candidate = m_paths.isEmpty()
                                  ? -1
                                  : qBound(0, m_incrementalPreviousCurrentRow, m_paths.size() - 1);
        nextCurrent = candidate >= 0 ? m_paths.at(candidate) : QString();
        if (m_selection)
        {
            QStringList validSelection;
            for (const QString &path : previousSelection)
                if (m_rowByPath.contains(path))
                    validSelection.append(path);
            m_selection->setSelection(validSelection, nextCurrent);
        }
    }
    if (!nextCurrent.isEmpty() && m_rowByPath.contains(nextCurrent))
        setCurrentIndex(m_model->index(m_rowByPath.value(nextCurrent), 0));
    else
        setCurrentIndex(QModelIndex());

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
    const int row = m_rowByPath.value(anchorPath, -1);
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
