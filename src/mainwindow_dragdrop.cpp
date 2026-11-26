// M15: drag & drop - accept files/folders dropped onto the window.
//
// Extracted from mainwindow_view.cpp. Behaviour-preserving: the handlers keep
// their declarations in mainwindow.h.
#include "mainwindow_p.h"

#include "application/ExternalOpen.h"

#include <QDragEnterEvent>
#include <QDropEvent>

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        m_dragHighlight = true;
        update();
    }
    else
        QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    // Accept moves anywhere on the window (including splitter handles and
    // status-bar edges) so the drop cursor never flickers to "forbidden".
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        if (!m_dragHighlight)
        {
            m_dragHighlight = true;
            update();
        }
    }
    else
        QMainWindow::dragMoveEvent(event);
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    QMainWindow::dragLeaveEvent(event);
    if (m_dragHighlight)
    {
        m_dragHighlight = false;
        update();
    }
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    // Draw a translucent accent border while a drag-hover is active so the
    // user gets visual confirmation that a drop is accepted.
    if (m_dragHighlight)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = palette().color(QPalette::Highlight);
        QPen pen(accent, 4);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(2, 2, -2, -2));
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // Drop received — turn off the drag highlight regardless of outcome.
    if (m_dragHighlight)
    {
        m_dragHighlight = false;
        update();
    }
    if (!event->mimeData()->hasUrls())
    {
        QMainWindow::dropEvent(event);
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    QStringList paths;
    for (const QUrl &url : urls)
    {
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            paths.append(local);
    }
    if (paths.isEmpty())
    {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    handleDroppedPaths(paths);
}

void MainWindow::openExternalTargets(const QStringList &paths)
{
    const auto plan = mviewer::application::planExternalOpen(paths);
    if (!plan.isValid())
    {
        const QString message =
            plan.error.isEmpty() ? QStringLiteral("Unable to open target") : plan.error;
        statusBar()->showMessage(message, 5000);
        qWarning().noquote() << message;
        return;
    }

    switch (plan.kind)
    {
    case mviewer::application::ExternalOpenKind::Image:
        if (plan.isCompare())
            openCompare(plan.paths);
        else
            onImageOpen(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Directory:
        changeDirectory(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Workspace:
        openWorkspaceFile(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Project:
        openProjectFile(plan.paths.front());
        break;
    case mviewer::application::ExternalOpenKind::Invalid:
        break;
    }
}

void MainWindow::handleDroppedPaths(const QStringList &paths)
{
    openExternalTargets(paths);
}