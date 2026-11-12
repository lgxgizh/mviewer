// ADR 032: raw .yuv parameter dialog entry point, load-failure hint and the
// pixel-inspector Y/U/V readout, and the two-file YUV PSNR / SSIM compare.
// The dialog saves per-file parameters into YuvParamStore (and QSettings);
// every cached decode and thumbnail of that file is then dropped so the next
// load uses the new parameters.
#include "mainwindow.h"
#include "mainwindow_p.h"

#include "core/image/ImageLoadingFacade.h"
#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvSampleProbe.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "plainrawparamsdialog.h"
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

namespace
{

void appendUniquePlainRaw(QStringList &paths, const QString &path)
{
    if (path.isEmpty() || paths.contains(path))
        return;
    if (!mviewer::core::rawplain::isPlainRawPath(path.toUtf8().toStdString()))
        return;
    paths.push_back(path);
}

void appendPanelPlainRaw(QStringList &paths, const ThumbnailPanel *panel)
{
    if (panel == nullptr)
        return;
    const QStringList listed = panel->pathList();
    for (const QString &path : listed)
        appendUniquePlainRaw(paths, path);
}

void invalidateThumbnails(const QString &path, ThumbnailPanel *primary, ThumbnailPanel *secondary,
                          ThumbnailPanel *tertiary)
{
    const std::string utf8 = path.toUtf8().toStdString();
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
    for (ThumbnailPanel *panel : {primary, secondary, tertiary})
    {
        if (panel)
            panel->invalidateSourceImage(path);
    }
}

void reloadCompareIfShows(CompareWorkspace *view, const QStringList &paths)
{
    if (view == nullptr || !view->isVisible())
        return;
    const QStringList compared = view->comparedImages();
    for (const QString &path : paths)
    {
        if (compared.contains(path))
        {
            view->setImages(compared);
            return;
        }
    }
}

void refreshYuvParamSource(const QString &path, ThumbnailPanel *primary, ThumbnailPanel *secondary,
                           ThumbnailPanel *tertiary, PreviewPanel *preview, MetadataPanel *metadata,
                           ImageViewer *viewer, CompareWorkspace *compare)
{
    invalidateThumbnails(path, primary, secondary, tertiary);
    if (preview)
        preview->setImage(path);
    if (metadata)
        metadata->setImage(path);
    if (viewer && !viewer->isHidden() && viewer->currentPath() == path)
        viewer->refreshSource(path);
    reloadCompareIfShows(compare, {path});
}

} // namespace

void MainWindow::refreshPlainRawSources(const QStringList &paths)
{
    for (const QString &path : paths)
    {
        invalidateThumbnails(path, m_thumbnailPanel, m_secondaryThumbnailPanel,
                             m_tertiaryThumbnailPanel);
    }
    if (m_previewPanel && paths.contains(m_previewPanel->requestedPath()))
        m_previewPanel->setImage(m_previewPanel->requestedPath());
    if (m_metadataPanel && paths.contains(currentImagePath()))
        m_metadataPanel->setImage(currentImagePath());
    if (m_imageViewer && m_imageViewer->isVisible() && paths.contains(m_imageViewer->currentPath()))
        m_imageViewer->refreshSource(m_imageViewer->currentPath());
    reloadCompareIfShows(m_compareView, paths);
}

void MainWindow::editYuvParams(const QString &path)
{
    if (path.isEmpty())
        return;
    QWidget *parent =
        (m_imageViewer && m_imageViewer->isVisible() && m_imageViewer->currentPath() == path)
            ? static_cast<QWidget *>(m_imageViewer)
            : static_cast<QWidget *>(this);
    const std::string utf8 = path.toUtf8().toStdString();
    // ADR 033: plain .raw sensor dumps share this entry point (session override).
    const bool plainRaw = mviewer::core::rawplain::isPlainRawPath(utf8);
    if (!(plainRaw ? editPlainRawParamsForPath(parent, path) : editYuvParamsForPath(parent, path)))
        return;
    if (plainRaw)
        refreshPlainRawSources({path});
    else
        refreshYuvParamSource(path, m_thumbnailPanel, m_secondaryThumbnailPanel,
                              m_tertiaryThumbnailPanel, m_previewPanel, m_metadataPanel,
                              m_imageViewer, m_compareView);
    if (statusBar())
        statusBar()->showMessage(
            (plainRaw ? tr("RAW parameters updated: %1") : tr("YUV parameters updated: %1"))
                .arg(QFileInfo(path).fileName()),
            4000);
}

void MainWindow::onYuvExtensionMapChanged()
{
    // New suffixes appear in (and removed ones leave) the file list; decode and
    // thumbnail keys of unsaved raw files carry the mapping fingerprint, so the
    // relisted files decode with the new pixel format instead of a stale cache.
    refreshActiveBrowse();
    const QString current = m_imageViewer ? m_imageViewer->currentPath() : QString();
    if (!current.isEmpty() && mviewer::core::yuv::isYuvPath(current.toUtf8().toStdString()))
    {
        const std::string utf8 = current.toUtf8().toStdString();
        mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
        if (!m_imageViewer->isHidden())
            m_imageViewer->refreshSource(current);
    }
    if (statusBar())
        statusBar()->showMessage(tr("YUV file extension mapping updated"), 4000);
}

void MainWindow::onPlainRawDefaultsChanged()
{
    // ADR 033: cached thumbnails and compare cells keep the old render until
    // the source is invalidated and the open views are reloaded.
    QStringList paths;
    appendPanelPlainRaw(paths, m_thumbnailPanel);
    appendPanelPlainRaw(paths, m_secondaryThumbnailPanel);
    appendPanelPlainRaw(paths, m_tertiaryThumbnailPanel);
    if (m_imageViewer)
        appendUniquePlainRaw(paths, m_imageViewer->currentPath());
    if (m_compareView)
    {
        const QStringList compared = m_compareView->comparedImages();
        for (const QString &path : compared)
            appendUniquePlainRaw(paths, path);
    }
    refreshPlainRawSources(paths);
    if (statusBar())
        statusBar()->showMessage(tr("RAW default parameters updated"), 4000);
}

bool MainWindow::showYuvLoadHint(const QString &path)
{
    const std::string utf8 = path.toUtf8().toStdString();
    if (mviewer::core::rawplain::isPlainRawPath(utf8) && statusBar())
    {
        const std::string why = mviewer::core::rawplain::resolvePlainRawFile(utf8).resolved.why;
        const QString name = QFileInfo(path).fileName();
        if (why.empty())
        {
            statusBar()->showMessage(
                tr("Cannot show %1: set its size and storage with right-click > RAW Parameters...")
                    .arg(name),
                8000);
        }
        else
        {
            statusBar()->showMessage(
                tr("Cannot show %1: %2. Set its size and storage with right-click > RAW "
                   "Parameters...")
                    .arg(name, QString::fromStdString(why)),
                8000);
        }
        return true;
    }
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
    if (mviewer::core::rawplain::isPlainRawPath(utf8))
        return QString::fromStdString(mviewer::core::rawplain::plainRawSampleText(
            utf8, x, y, mviewer::core::rawplain::SampleRead::CachedOrAsync));
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
