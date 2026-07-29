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
    if (m_current.isEmpty() && m_selection.isEmpty() && m_focused.isEmpty() && m_compared.isEmpty())
        return;
    m_current.clear();
    m_selection.clear();
    m_focused.clear();
    m_compared.clear();
    emit currentImageChanged(m_current);
    emit selectionChanged(m_selection);
    emit focusedChanged(m_focused);
    emit comparedChanged(m_compared);
}

void SelectionModel::setFocused(const QString &path)
{
    if (m_focused == path)
        return;
    m_focused = path;
    emit focusedChanged(m_focused);
}

void SelectionModel::setHovered(const QString &path)
{
    if (m_hovered == path)
        return;
    m_hovered = path;
    emit hoveredChanged(m_hovered);
}

void SelectionModel::setCompared(const QStringList &paths)
{
    if (m_compared == paths)
        return;
    m_compared = paths;
    emit comparedChanged(m_compared);
}
