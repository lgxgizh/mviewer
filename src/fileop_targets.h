#pragma once

#include <QKeyEvent>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

// Pure decisions for Delete / F2 / Ctrl+M. UI-layer header (Qt containers are
// intentional) so tests can lock the target without constructing widgets.
// Viewer operations always name the image on screen. Browse operations keep
// the caller's list, including a split-browse union built by the caller.

enum class FileOpContext
{
    Browse,
    Viewer
};

enum class ViewerFileOp
{
    None,
    Rename,
    Delete,
    Move
};

inline QStringList resolveFileOpTargets(FileOpContext ctx, const QString &viewerPath,
                                        const QStringList &browseTargets)
{
    if (ctx == FileOpContext::Browse)
        return browseTargets;
    if (viewerPath.isEmpty())
        return {};
    return QStringList{viewerPath};
}

// Index to show after `removedIndices` are deleted from a sequence of
// `oldCount`, or -1 when nothing remains. `oldIndex` is the image that was
// on screen. A removed current image prefers the next survivor; if it was
// the last survivor, the new last image is used (no wrap to the first).
inline int nextIndexAfterRemoval(int oldIndex, int oldCount, const QList<int> &removedIndices)
{
    if (oldCount <= 0 || oldIndex < 0 || oldIndex >= oldCount)
        return -1;

    QList<int> removed;
    removed.reserve(removedIndices.size());
    for (int index : removedIndices)
    {
        if (index < 0 || index >= oldCount || removed.contains(index))
            continue;
        removed.append(index);
    }
    if (removed.size() >= oldCount)
        return -1;

    const auto shiftBefore = [&removed](int index)
    {
        int shift = 0;
        for (int removedIndex : removed)
        {
            if (removedIndex < index)
                ++shift;
        }
        return index - shift;
    };

    if (!removed.contains(oldIndex))
        return shiftBefore(oldIndex);

    for (int index = oldIndex + 1; index < oldCount; ++index)
    {
        if (!removed.contains(index))
            return shiftBefore(index);
    }
    for (int index = oldIndex - 1; index >= 0; --index)
    {
        if (!removed.contains(index))
            return shiftBefore(index);
    }
    return -1;
}

// Row F2 renames. `currentRow` wins when it is part of the selection;
// otherwise the lowest selected row. Duplicate rows (details view, several
// columns) are ignored. No selection returns -1.
inline int renameTargetRow(const QList<int> &selectedRows, int currentRow)
{
    QList<int> rows;
    rows.reserve(selectedRows.size());
    for (int row : selectedRows)
    {
        if (row < 0 || rows.contains(row))
            continue;
        rows.append(row);
    }
    if (rows.isEmpty())
        return -1;
    if (rows.contains(currentRow))
        return currentRow;
    return *std::min_element(rows.cbegin(), rows.cend());
}

// Steal Delete / F2 / Ctrl+M only when the event is already in the viewer,
// or the viewer window itself is the active visible window. A viewer that is
// merely open behind the browser must not take browse-pane keys.
inline bool viewerOwnsFileOpKeys(bool eventOnViewer, bool viewerVisible, bool viewerActive)
{
    return eventOnViewer || (viewerVisible && viewerActive);
}

inline ViewerFileOp viewerFileOpForKey(int key, Qt::KeyboardModifiers modifiers)
{
    const auto mods = modifiers & ~Qt::KeyboardModifiers(Qt::KeypadModifier);
    if (mods == Qt::NoModifier && key == static_cast<int>(Qt::Key_F2))
        return ViewerFileOp::Rename;
    if (mods == Qt::NoModifier && key == static_cast<int>(Qt::Key_Delete))
        return ViewerFileOp::Delete;
    if (mods == Qt::ControlModifier && key == static_cast<int>(Qt::Key_M))
        return ViewerFileOp::Move;
    return ViewerFileOp::None;
}
