#include "selectionmodel.h"

SelectionModel::SelectionModel(QObject *parent) : QObject(parent)
{
}

const QStringList &SelectionModel::paneSelection(PaneSlot pane) const
{
    return m_paneSelections[static_cast<int>(pane)];
}

QString SelectionModel::paneCurrentImage(PaneSlot pane) const
{
    return m_paneCurrentImage[static_cast<int>(pane)];
}

void SelectionModel::setSplitModeEnabled(bool enabled)
{
    if (m_splitModeEnabled == enabled)
        return;
    m_splitModeEnabled = enabled;
    if (!enabled)
    {
        m_paneSelections[static_cast<int>(PaneSlot::Secondary)].clear();
        m_paneCurrentImage[static_cast<int>(PaneSlot::Secondary)].clear();
        m_activePane = PaneSlot::Primary;
        emit activePaneChanged(m_activePane);
    }
    emit splitModeChanged(m_splitModeEnabled);
    rebuildMergedSelection(true);
}

void SelectionModel::setActivePane(PaneSlot pane)
{
    if (m_activePane == pane)
        return;
    m_activePane = pane;
    emit activePaneChanged(m_activePane);
}

void SelectionModel::setPaneSelection(PaneSlot pane, const QStringList &paths,
                                      const QString &current)
{
    const int idx = static_cast<int>(pane);
    const QString cur = (current.isEmpty() && !paths.isEmpty()) ? paths.first() : current;
    if (!m_splitModeEnabled)
    {
        setSelection(paths, cur);
        return;
    }
    m_paneSelections[idx] = paths;
    m_paneCurrentImage[idx] = cur;
    emit paneSelectionChanged(pane, paths);
    rebuildMergedSelection(true);
}

void SelectionModel::clearPane(PaneSlot pane)
{
    const int idx = static_cast<int>(pane);
    if (m_paneSelections[idx].isEmpty() && m_paneCurrentImage[idx].isEmpty())
        return;
    m_paneSelections[idx].clear();
    m_paneCurrentImage[idx].clear();
    emit paneSelectionChanged(pane, QStringList());
    rebuildMergedSelection(true);
}

void SelectionModel::setCurrentImage(const QString &path)
{
    if (m_current == path && m_selection.size() == 1 && m_selection.first() == path)
        return;
    if (m_current == path && !path.isEmpty() && m_selection.contains(path))
        return;
    m_current = path;
    const int idx = m_splitModeEnabled ? static_cast<int>(m_activePane) : 0;
    m_paneCurrentImage[idx] = path;
    if (!path.isEmpty() && m_selection.size() > 1 && m_selection.contains(path))
    {
        emit currentImageChanged(m_current);
        return;
    }
    if (!m_splitModeEnabled)
    {
        m_selection = path.isEmpty() ? QStringList() : QStringList{path};
        m_paneSelections[0] = m_selection;
        m_paneSelections[1].clear();
        m_paneCurrentImage[1].clear();
        emit currentImageChanged(m_current);
        emit selectionChanged(m_selection);
        return;
    }
    m_paneSelections[idx] = path.isEmpty() ? QStringList() : QStringList{path};
    emit paneSelectionChanged(m_activePane, m_paneSelections[idx]);
    rebuildMergedSelection(true);
}

void SelectionModel::setSelection(const QStringList &paths, const QString &current)
{
    const QString cur = (current.isEmpty() && !paths.isEmpty()) ? paths.first() : current;
    if (!m_splitModeEnabled)
    {
        const bool curChanged = (cur != m_current);
        const bool selChanged = (paths != m_selection);
        if (!curChanged && !selChanged)
            return;
        m_selection = paths;
        m_current = cur;
        m_paneSelections[0] = paths;
        m_paneCurrentImage[0] = cur;
        m_paneSelections[1].clear();
        m_paneCurrentImage[1].clear();
        if (curChanged)
            emit currentImageChanged(m_current);
        if (selChanged)
            emit selectionChanged(m_selection);
        return;
    }

    const int idx = static_cast<int>(m_activePane);
    m_paneSelections[idx] = paths;
    m_paneCurrentImage[idx] = cur;
    emit paneSelectionChanged(m_activePane, paths);
    rebuildMergedSelection(true);
}

void SelectionModel::clear()
{
    if (m_current.isEmpty() && m_selection.isEmpty() && m_focused.isEmpty() && m_compared.isEmpty())
        return;
    m_paneSelections[0].clear();
    m_paneSelections[1].clear();
    m_paneCurrentImage[0].clear();
    m_paneCurrentImage[1].clear();
    m_current.clear();
    m_selection.clear();
    m_focused.clear();
    m_compared.clear();
    emit currentImageChanged(m_current);
    emit selectionChanged(m_selection);
    emit focusedChanged(m_focused);
    emit comparedChanged(m_compared);
    emit paneSelectionChanged(PaneSlot::Primary, QStringList());
    emit paneSelectionChanged(PaneSlot::Secondary, QStringList());
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

void SelectionModel::rebuildMergedSelection(bool emitSignals)
{
    QStringList merged;
    QString cur;
    if (!m_splitModeEnabled)
    {
        merged = m_paneSelections[static_cast<int>(PaneSlot::Primary)];
        cur = m_paneCurrentImage[static_cast<int>(PaneSlot::Primary)];
    }
    else
    {
        merged = m_paneSelections[static_cast<int>(PaneSlot::Primary)];
        const auto &sec = m_paneSelections[static_cast<int>(PaneSlot::Secondary)];
        for (const QString &p : sec)
        {
            if (!merged.contains(p))
                merged.append(p);
        }
        const int activeIdx = static_cast<int>(m_activePane);
        const int otherIdx = 1 - activeIdx;
        if (!m_paneCurrentImage[activeIdx].isEmpty() && merged.contains(m_paneCurrentImage[activeIdx]))
            cur = m_paneCurrentImage[activeIdx];
        else if (!m_paneCurrentImage[otherIdx].isEmpty() && merged.contains(m_paneCurrentImage[otherIdx]))
            cur = m_paneCurrentImage[otherIdx];
        else if (!merged.isEmpty())
            cur = merged.first();
    }

    const bool curChanged = (cur != m_current);
    const bool selChanged = (merged != m_selection);
    m_current = cur;
    m_selection = merged;

    if (emitSignals)
    {
        if (curChanged)
            emit currentImageChanged(m_current);
        if (selChanged)
            emit selectionChanged(m_selection);
    }
}
