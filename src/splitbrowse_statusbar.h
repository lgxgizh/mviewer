#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

// Status-bar decisions for the active browse pane. The image index comes from
// that pane's own path list, and split mode keeps both selection counts visible.

inline int activePaneImageIndex(const QStringList &panePaths, const QString &current)
{
    if (current.isEmpty())
        return -1;
    return panePaths.indexOf(current);
}

enum class PaneSelectionStatusKind
{
    Images,
    Selected,
    SplitUnion
};

inline PaneSelectionStatusKind paneSelectionStatusKind(bool splitActive, int selected)
{
    if (splitActive)
        return PaneSelectionStatusKind::SplitUnion;
    return selected > 0 ? PaneSelectionStatusKind::Selected : PaneSelectionStatusKind::Images;
}

inline QString splitSelectionStatusText(int leftSelected, int rightSelected)
{
    return QCoreApplication::translate("MainWindow", "Left %1 + Right %2 selected")
        .arg(leftSelected)
        .arg(rightSelected);
}
