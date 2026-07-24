#include "selectionmodel.h"

SelectionModel::SelectionModel(QObject *parent) : QObject(parent)
{
}

void SelectionModel::setCurrentImage(const QString &path)
{
    // Already current and already the sole selection — nothing to do.
    if (m_current == path && m_selection.size() == 1 && m_selection.first() == path)
        return;
    // Already current and part of a multi-selection — keep multi-select intact
    // (keyboard focus / gallery currentChanged must not collapse Ctrl/Shift
    // multi-select into a single item).
    if (m_current == path && !path.isEmpty() && m_selection.contains(path))
        return;
    m_current = path;
    if (!path.isEmpty() && m_selection.size() > 1 && m_selection.contains(path))
    {
        // Move current within an existing multi-selection without collapsing it.
        emit currentImageChanged(m_current);
        return;
    }
    m_selection = path.isEmpty() ? QStringList() : QStringList{path};
    emit currentImageChanged(m_current);
    emit selectionChanged(m_selection);
}

void SelectionModel::setSelection(const QStringList &paths, const QString &current)
{
    const QString cur = (current.isEmpty() && !paths.isEmpty()) ? paths.first() : current;
    const bool curChanged = (cur != m_current);
    const bool selChanged = (paths != m_selection);
    if (!curChanged && !selChanged)
        return;
    m_selection = paths;
    m_current = cur;
    if (curChanged)
        emit currentImageChanged(m_current);
    if (selChanged)
        emit selectionChanged(m_selection);
}

void SelectionModel::clear()
{
    if (m_current.isEmpty() && m_selection.isEmpty())
        return;
    m_current.clear();
    m_selection.clear();
    emit currentImageChanged(m_current);
    emit selectionChanged(m_selection);
}
