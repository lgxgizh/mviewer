#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

// Pure plan for restoring per-pane selection after a rotate/flip. Split mode
// must not pour both panes into the active slot (SelectionModel::setSelection
// does that). The model keeps each snapshot; the view shows that snapshot
// intersected with the paths still listed in the pane.

struct SplitSelectionRestorePlan
{
    bool split = false;
    QStringList primaryModel;
    QStringList secondaryModel;
    QStringList primaryView;
    QStringList secondaryView;
    QString primaryCurrent;
    QString secondaryCurrent;
    QString globalCurrent;
    QStringList singleSelection;
    QString singleCurrent;
};

inline QStringList orderPreservingIntersect(const QStringList &selected, const QStringList &listed)
{
    QSet<QString> have;
    have.reserve(listed.size());
    for (const QString &path : listed)
        have.insert(path);

    QStringList out;
    out.reserve(selected.size());
    for (const QString &path : selected)
    {
        if (have.contains(path))
            out.append(path);
    }
    return out;
}

inline SplitSelectionRestorePlan
splitSelectionRestorePlan(bool split, const QStringList &primaryPaths,
                          const QString &primaryCurrent, const QStringList &secondaryPaths,
                          const QString &secondaryCurrent, const QStringList &primaryPathList,
                          const QStringList &secondaryPathList, const QStringList &singleSelection,
                          const QString &singleCurrent, const QString &globalCurrent)
{
    SplitSelectionRestorePlan plan;
    plan.split = split;
    plan.globalCurrent = globalCurrent;
    plan.singleSelection = singleSelection;
    plan.singleCurrent = singleCurrent;
    if (!split)
        return plan;

    plan.primaryModel = primaryPaths;
    plan.secondaryModel = secondaryPaths;
    plan.primaryCurrent = primaryCurrent;
    plan.secondaryCurrent = secondaryCurrent;
    plan.primaryView = orderPreservingIntersect(primaryPaths, primaryPathList);
    plan.secondaryView = orderPreservingIntersect(secondaryPaths, secondaryPathList);
    return plan;
}
