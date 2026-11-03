// ADR 032: raw .yuv parameter dialog entry point and load-failure hint.
// The dialog saves per-file parameters into YuvParamStore (and QSettings);
// every cached decode and thumbnail of that file is then dropped so the next
// load uses the new parameters.
#include "mainwindow.h"
#include "mainwindow_p.h"

#include "core/image/ImageLoadingFacade.h"
#include "core/image/yuv/YuvFile.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "thumbnailpanel.h"
#include "thumbnailprovider.h"
#include "yuvparamsdialog.h"

#include <QFileInfo>
#include <QStatusBar>
#include <QString>

#include <string>

void MainWindow::editYuvParams(const QString &path)
{
    if (path.isEmpty())
        return;
    QWidget *parent =
        (m_imageViewer && m_imageViewer->isVisible() && m_imageViewer->currentPath() == path)
            ? static_cast<QWidget *>(m_imageViewer)
            : static_cast<QWidget *>(this);
    if (!editYuvParamsForPath(parent, path))
        return;
    const std::string utf8 = path.toUtf8().toStdString();
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
    for (ThumbnailPanel *panel : {m_thumbnailPanel, m_secondaryThumbnailPanel})
    {
        if (panel)
            panel->invalidateSourceImage(path);
    }
    if (m_previewPanel)
        m_previewPanel->setImage(path);
    if (m_metadataPanel)
        m_metadataPanel->setImage(path);
    if (m_imageViewer && !m_imageViewer->isHidden() && m_imageViewer->currentPath() == path)
        m_imageViewer->refreshSource(path);
    if (statusBar())
        statusBar()->showMessage(tr("YUV parameters updated: %1").arg(QFileInfo(path).fileName()),
                                 4000);
}

bool MainWindow::showYuvLoadHint(const QString &path)
{
    if (!mviewer::core::yuv::isYuvPath(path.toUtf8().toStdString()) || !statusBar())
        return false;
    statusBar()->showMessage(
        tr("Cannot show %1: set its size and pixel format with right-click > YUV Parameters...")
            .arg(QFileInfo(path).fileName()),
        8000);
    return true;
}
