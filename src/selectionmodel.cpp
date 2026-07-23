#include "selectionmodel.h"

SelectionModel::SelectionModel(QObject *parent) : QObject(parent)
{
}

void SelectionModel::setCurrentImage(const QString &path)
{
    const bool selMatches = (m_selection.size() == 1 && m_selection.first() == path);
    if (m_current == path && selMatches)
        return;
    m_current = path;
    m_selection = path.isEmpty() ? QStringList() : QStringList{path};
    emit currentImageChanged(m_current);
    emit selectionChanged(m_selection);
}

void SelectionModel::setSelection(const QStringList &paths, const QString &current)
{
    const QString cur = (current.isEmpty() && !paths.isEmpty()) ? paths.first() : current;
    const bool curChanged = (cur != m_current);
    m_selection = paths;
    m_current = cur;
    if (curChanged)
        emit currentImageChanged(m_current);
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
