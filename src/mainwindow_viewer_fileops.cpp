// Delete, rename, and move for the image on screen. These bypass the browse
// selection union so a multi-select left behind in the gallery is not the target.
#include "mainwindow_p.h"

#include "fileop_targets.h"
#include "thumbnailpanel.h"

namespace
{

QStringList viewerFileTargets(SelectionModel *selection, const QString &viewerPath)
{
    const QStringList browse = selection ? selection->selection() : QStringList();
    return resolveFileOpTargets(FileOpContext::Viewer, viewerPath, browse);
}

} // namespace

void MainWindow::deleteViewerImage()
{
    const QStringList targets = viewerFileTargets(m_selection, currentImagePath());
    if (targets.isEmpty())
        return;
    if (ThumbnailPanel *panel = thumbnailPanelForPath(targets.first()))
        panel->moveToTrashPaths(targets);
}

void MainWindow::renameViewerImage()
{
    const QStringList targets = viewerFileTargets(m_selection, currentImagePath());
    if (targets.isEmpty())
        return;
    if (ThumbnailPanel *panel = thumbnailPanelForPath(targets.first()))
        panel->renamePath(targets.first());
}

void MainWindow::moveViewerImage()
{
    const QStringList targets = viewerFileTargets(m_selection, currentImagePath());
    if (targets.isEmpty())
        return;
    if (ThumbnailPanel *panel = thumbnailPanelForPath(targets.first()))
        panel->movePathsTo(targets);
}
