// ADR 032: raw .yuv parameter dialog entry point, load-failure hint and the
// pixel-inspector Y/U/V readout, and the two-file YUV PSNR / SSIM compare.
// The dialog saves per-file parameters into YuvParamStore (and QSettings);
// every cached decode and thumbnail of that file is then dropped so the next
// load uses the new parameters.
#include "mainwindow.h"
#include "mainwindow_p.h"

#include "core/image/ImageLoadingFacade.h"
#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvSampleProbe.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "thumbnailpanel.h"
#include "thumbnailprovider.h"
#include "yuvcomparedialog.h"
#include "yuvparamsdialog.h"

#include <QFileInfo>
#include <QStatusBar>
#include <QString>
#include <QStringList>

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

QString MainWindow::yuvSampleText(const QString &path, int frameIndex, int x, int y)
{
    const std::string utf8 = path.toUtf8().toStdString();
    if (!mviewer::core::yuv::isYuvPath(utf8))
        return {};
    // One unpacked frame is cached, so moving the cursor does not re-read the file.
    const mviewer::core::yuv::YuvSample sample =
        mviewer::core::yuv::YuvSampleProbe::instance().sample(utf8, frameIndex, x, y);
    return QString::fromStdString(mviewer::core::yuv::formatYuvSample(sample));
}

void MainWindow::onYuvToolRequested(const QStringList &paths)
{
    if (paths.size() == 1)
        editYuvParams(paths.front());
    else if (paths.size() == 2)
        compareYuv(paths.at(0), paths.at(1));
}

void MainWindow::compareYuv(const QString &pathA, const QString &pathB)
{
    // Per-plane metrics on code values (ADR 032); the dialog explains when the
    // pair is not comparable as YUV and points at the RGB compare view.
    runYuvCompare(this, pathA, pathB);
}
