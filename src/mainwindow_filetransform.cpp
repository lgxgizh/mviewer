// Batch rotate / flip of the current selection. Synchronous for the caller:
// the UI waits on a local event loop until the in-flight file finishes.
#include "mainwindow_p.h"

#include "core/image/ImageFileRotate.h"
#include "core/image/ImageLoadingFacade.h"
#include "imageviewer.h"
#include "metadatapanel.h"
#include "previewpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "thumbnailprovider.h"

#include <QApplication>
#include <QCheckBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QObject>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

#include <cstdint>
#include <memory>
#include <string>

namespace
{

enum class FileTransformKind : std::uint8_t
{
    Rotate,
    Flip
};

struct FileTransformBatch
{
    int successCount = 0;
    bool canceled = false;
    QStringList failedPaths;
    QStringList failedErrors;
};

// setValue() pumps events only after shownOnce. forceShow() is protected and
// latches that flag; calling it after construction makes Cancel work between
// files. The extra processEvents pass excludes user input so offscreen tests
// do not re-enter QProgressDialog's private cancel-button connection.
class ShownProgressDialog final : public QProgressDialog
{
  public:
    using QProgressDialog::QProgressDialog;
    void reveal()
    {
        forceShow();
    }
};

class BatchFileProgress
{
  public:
    BatchFileProgress(QWidget *parent, int total, FileTransformKind kind)
        : m_total(total), m_kind(kind)
    {
        if (total < 2 || parent == nullptr)
            return;
        const QString label = kind == FileTransformKind::Rotate ? MainWindow::tr("Rotating…")
                                                                : MainWindow::tr("Flipping…");
        m_dialog = std::make_unique<ShownProgressDialog>(label, QStringLiteral("Cancel"), 0, total,
                                                         parent);
        m_dialog->setObjectName(QStringLiteral("batchRotateFlipProgress"));
        m_dialog->setWindowModality(Qt::WindowModal);
        m_dialog->setAutoClose(false);
        m_dialog->setAutoReset(false);
        m_dialog->setMinimumDuration(0);
        m_dialog->setValue(0);
        m_dialog->reveal();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    ~BatchFileProgress()
    {
        dismiss();
    }
    BatchFileProgress(const BatchFileProgress &) = delete;
    BatchFileProgress &operator=(const BatchFileProgress &) = delete;

    bool wasCanceled() const
    {
        return m_dialog && m_dialog->wasCanceled();
    }

    void advance(int finished)
    {
        if (!m_dialog || m_dialog->wasCanceled())
            return;
        m_dialog->setLabelText(finishedLabel(finished));
        m_dialog->setValue(finished);
        if (!m_dialog || m_dialog->wasCanceled())
            return;
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    void dismiss()
    {
        if (!m_dialog)
            return;
        m_dialog->hide();
        m_dialog.reset();
    }

  private:
    QString finishedLabel(int finished) const
    {
        return (m_kind == FileTransformKind::Rotate)
                   ? MainWindow::tr("Rotated %1 / %2").arg(finished).arg(m_total)
                   : MainWindow::tr("Flipped %1 / %2").arg(finished).arg(m_total);
    }

    std::unique_ptr<ShownProgressDialog> m_dialog;
    int m_total = 0;
    FileTransformKind m_kind = FileTransformKind::Rotate;
};

QStringList transformTargetPaths(SelectionModel *selection, ThumbnailPanel *panel,
                                 const QString &currentPath)
{
    if (selection && !selection->selection().isEmpty())
        return selection->selection();
    if (panel && !panel->selectedPaths().isEmpty())
        return panel->selectedPaths();
    QStringList paths;
    if (!currentPath.isEmpty())
        paths.append(currentPath);
    return paths;
}

void releaseSourceForRewrite(ImageViewer *viewer, PreviewPanel *preview, const QString &path,
                             const std::string &utf8)
{
    if (viewer)
        viewer->releaseSourceHandles(path);
    if (preview)
        preview->releaseSourceHandles(path);
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
}

void noteTransformSuccess(ThumbnailPanel *panel, const QString &path, const std::string &utf8)
{
    mviewer::core::ImageLoadingFacade::instance().invalidateSource(utf8);
    ThumbnailProvider::invalidateSource(utf8);
    if (panel)
        panel->invalidateSourceImage(path);
}

void refreshOpenImage(ImageViewer *viewer, PreviewPanel *preview, MetadataPanel *metadata,
                      const QString &path)
{
    if (path.isEmpty())
        return;
    if (preview)
        preview->setImage(path);
    if (metadata)
        metadata->setImage(path);
    if (viewer && !viewer->isHidden() && viewer->currentPath() == path)
        viewer->refreshSource(path);
}

void restoreMultiSelection(ThumbnailPanel *panel, SelectionModel *selection,
                           const QStringList &paths, const QString &current)
{
    if (!panel || paths.size() <= 1)
        return;
    const QString focus = (!current.isEmpty() && paths.contains(current)) ? current : paths.first();
    panel->selectPaths(paths, focus);
    if (selection)
        selection->setSelection(paths, focus);
}

inline void showStatus(QStatusBar *bar, const QString &text, int ms)
{
    if (bar)
        bar->showMessage(text, ms);
}

QStringList transformFailureLines(const FileTransformBatch &batch)
{
    QStringList lines;
    const int count = static_cast<int>(batch.failedPaths.size());
    for (int i = 0; i < count; ++i)
    {
        lines.append(QStringLiteral("%1: %2").arg(QFileInfo(batch.failedPaths.at(i)).fileName(),
                                                  batch.failedErrors.at(i)));
    }
    return lines;
}

// Connect finished before setFuture. QEventLoop::quit() before exec() does not
// stick, so a future that is already finished must skip exec().
template <typename T> T waitFuture(const QFuture<T> &future, const BatchFileProgress &progress)
{
    QFutureWatcher<T> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<T>::finished, &loop, &QEventLoop::quit);
    QTimer cancelTimer;
    cancelTimer.setInterval(50);
    QObject::connect(&cancelTimer, &QTimer::timeout, &loop,
                     [&loop, &progress]()
                     {
                         if (progress.wasCanceled())
                             loop.quit();
                     });
    watcher.setFuture(future);
    if (!future.isFinished())
    {
        cancelTimer.start();
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        cancelTimer.stop();
    }
    QFuture<T> pending = future; // waitForFinished() is non-const
    if (!pending.isFinished())
        pending.waitForFinished();
    return pending.result();
}

void recordTransformResult(FileTransformBatch &batch, ThumbnailPanel *panel, const QString &path,
                           const std::string &utf8,
                           const mviewer::core::ImageFileRotateResult &result)
{
    if (!result.ok)
    {
        batch.failedPaths.append(path);
        batch.failedErrors.append(ImageViewer::rotateFailureUserMessage(result));
        return;
    }
    noteTransformSuccess(panel, path, utf8);
    ++batch.successCount;
}

template <typename Fn>
FileTransformBatch runTransformBatch(QWidget *parent, ImageViewer *viewer, PreviewPanel *preview,
                                     ThumbnailPanel *panel, const QStringList &paths,
                                     FileTransformKind kind, Fn &&transform)
{
    BatchFileProgress progress(parent, static_cast<int>(paths.size()), kind);
    FileTransformBatch batch;
    const int total = static_cast<int>(paths.size());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int i = 0; i < total; ++i)
    {
        if (progress.wasCanceled())
            break;
        const QString &path = paths.at(i);
        const std::string utf8 = path.toUtf8().toStdString();
        releaseSourceForRewrite(viewer, preview, path, utf8);
        const auto future = QtConcurrent::run([&transform, utf8]() { return transform(utf8); });
        recordTransformResult(batch, panel, path, utf8, waitFuture(future, progress));
        progress.advance(i + 1);
    }
    QApplication::restoreOverrideCursor();
    const int attempted = batch.successCount + static_cast<int>(batch.failedPaths.size());
    // A cancel that arrives while painting the last item has nothing left to skip.
    batch.canceled = progress.wasCanceled() && attempted < total;
    progress.dismiss();
    return batch;
}

qsizetype skippedCount(const QStringList &paths, const FileTransformBatch &batch)
{
    return paths.size() - batch.successCount - batch.failedPaths.size();
}

int normalizedDegrees(int degrees)
{
    int norm = degrees % 360;
    if (norm < 0)
        norm += 360;
    return norm;
}

void showRotateSuccess(QStatusBar *bar, const QStringList &paths, const FileTransformBatch &batch,
                       int degrees)
{
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("Canceled: successfully rotated %1 / %2 images")
                       .arg(batch.successCount)
                       .arg(paths.size()),
                   3000);
        return;
    }
    const int norm = normalizedDegrees(degrees);
    if (paths.size() == 1)
        showStatus(bar, MainWindow::tr("Rotated and overwrote the original file (%1°)").arg(norm),
                   2000);
    else
        showStatus(bar,
                   MainWindow::tr("Rotated %1 images and overwrote the original files (%2°)")
                       .arg(paths.size())
                       .arg(norm),
                   3000);
}

void warnRotateFailures(QWidget *parent, const QStringList &paths, const FileTransformBatch &batch)
{
    const QStringList lines = transformFailureLines(batch);
    if (paths.size() == 1)
    {
        QMessageBox::warning(parent, MainWindow::tr("Rotation failed"),
                             MainWindow::tr("Unable to rotate image: %1\n%2")
                                 .arg(batch.failedPaths.first(), batch.failedErrors.first()));
        return;
    }
    const QString title = batch.canceled
                              ? MainWindow::tr("Batch rotate canceled (including failures)")
                              : MainWindow::tr("Batch rotation complete (including failures)");
    const QString msg = MainWindow::tr("Successfully rotated %1 images, %2 failed:\n%3")
                            .arg(batch.successCount)
                            .arg(batch.failedPaths.size())
                            .arg(lines.join(QStringLiteral("\n")));
    QMessageBox::warning(parent, title, msg);
}

void showRotateFailureStatus(QStatusBar *bar, const QStringList &paths,
                             const FileTransformBatch &batch)
{
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("Rotate canceled: %1 succeeded, %2 failed, %3 unprocessed")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size())
                       .arg(skippedCount(paths, batch)),
                   3000);
        return;
    }
    showStatus(bar,
               MainWindow::tr("Rotation complete: %1 succeeded, %2 failed")
                   .arg(batch.successCount)
                   .arg(batch.failedPaths.size()),
               3000);
}

void reportRotateBatch(QWidget *parent, QStatusBar *bar, const QStringList &paths,
                       const FileTransformBatch &batch, int degrees)
{
    if (batch.failedPaths.isEmpty())
    {
        showRotateSuccess(bar, paths, batch, degrees);
        return;
    }
    warnRotateFailures(parent, paths, batch);
    showRotateFailureStatus(bar, paths, batch);
}

void showFlipSuccess(QStatusBar *bar, const QStringList &paths, const FileTransformBatch &batch,
                     bool horizontal)
{
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("Cancelled: successfully flipped %1 / %2")
                       .arg(batch.successCount)
                       .arg(paths.size()),
                   3000);
        return;
    }
    if (paths.size() == 1)
    {
        showStatus(bar,
                   horizontal
                       ? MainWindow::tr("Flipped horizontally and overwrote the original file")
                       : MainWindow::tr("Flipped vertically and overwrote the original file"),
                   2000);
        return;
    }
    showStatus(
        bar,
        horizontal
            ? MainWindow::tr("Horizontally flipped %1 images and overwrote the original files")
                  .arg(paths.size())
            : MainWindow::tr("Vertically flipped %1 images and overwrote the original files")
                  .arg(paths.size()),
        3000);
}

void warnFlipFailures(QWidget *parent, const QStringList &paths, const FileTransformBatch &batch)
{
    const QStringList lines = transformFailureLines(batch);
    if (paths.size() == 1)
    {
        QMessageBox::warning(parent, MainWindow::tr("Flip failed"),
                             MainWindow::tr("Unable to flip image: %1\n%2")
                                 .arg(batch.failedPaths.first(), batch.failedErrors.first()));
        return;
    }
    const QString title = batch.canceled
                              ? MainWindow::tr("Batch flip canceled (including failures)")
                              : MainWindow::tr("Batch flip complete (including failures)");
    const QString msg = MainWindow::tr("Successfully flipped %1 images, %2 failed:\n%3")
                            .arg(batch.successCount)
                            .arg(batch.failedPaths.size())
                            .arg(lines.join(QStringLiteral("\n")));
    QMessageBox::warning(parent, title, msg);
}

void showFlipFailureStatus(QStatusBar *bar, const QStringList &paths,
                           const FileTransformBatch &batch)
{
    if (batch.canceled)
    {
        showStatus(bar,
                   MainWindow::tr("Flip canceled: %1 succeeded, %2 failed, %3 unprocessed")
                       .arg(batch.successCount)
                       .arg(batch.failedPaths.size())
                       .arg(skippedCount(paths, batch)),
                   3000);
        return;
    }
    showStatus(bar,
               MainWindow::tr("Flip complete: %1 succeeded, %2 failed")
                   .arg(batch.successCount)
                   .arg(batch.failedPaths.size()),
               3000);
}

void reportFlipBatch(QWidget *parent, QStatusBar *bar, const QStringList &paths,
                     const FileTransformBatch &batch, bool horizontal)
{
    if (batch.failedPaths.isEmpty())
    {
        showFlipSuccess(bar, paths, batch, horizontal);
        return;
    }
    warnFlipFailures(parent, paths, batch);
    showFlipFailureStatus(bar, paths, batch);
}

bool confirmTransform(QWidget *parent, const QStringList &paths, FileTransformKind kind)
{
    QSettings settings;
    if (!settings.value(QStringLiteral("confirmModifyFile"), true).toBool())
        return true;

    const bool rotate = (kind == FileTransformKind::Rotate);
    const QString prompt =
        paths.size() == 1 ? (rotate ? MainWindow::tr("Rotation will directly modify and overwrite "
                                                     "the original image file.\nContinue?")
                                    : MainWindow::tr("Flipping will directly modify and overwrite "
                                                     "the original image file.\nContinue?"))
                          : (rotate ? MainWindow::tr("Rotation will directly modify and overwrite "
                                                     "the %1 selected image files.\nContinue?")
                                          .arg(paths.size())
                                    : MainWindow::tr("Flipping will directly modify and overwrite "
                                                     "the selected %1 image files.\nContinue?")
                                          .arg(paths.size()));

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(MainWindow::tr("Confirm original file modification"));
    box.setText(prompt);
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    auto *cb = new QCheckBox(MainWindow::tr("Don't show again"), &box);
    box.setCheckBox(cb);

    const int res = box.exec();
    const bool accepted =
        (res == QMessageBox::Ok || res == QMessageBox::Yes || res == QDialog::Accepted ||
         (res == QMessageBox::NoButton && box.result() == QDialog::Accepted));
    if (!accepted)
        return false;

    if (cb->isChecked())
        settings.setValue(QStringLiteral("confirmModifyFile"), false);

    return true;
}

} // namespace

void MainWindow::rotateCurrentImage(int degrees)
{
    if (m_compareView && m_compareView->isVisible())
    {
        m_compareView->rotateCurrentCell(degrees);
        return;
    }

    const QStringList paths =
        transformTargetPaths(m_selection, m_thumbnailPanel, currentImagePath());
    if (paths.isEmpty())
    {
        showStatus(statusBar(), tr("No images available to rotate"), 2000);
        return;
    }

    if (!confirmTransform(this, paths, FileTransformKind::Rotate))
        return;

    const FileTransformBatch batch =
        runTransformBatch(this, m_imageViewer, m_previewPanel, m_thumbnailPanel, paths,
                          FileTransformKind::Rotate, [degrees](const std::string &utf8)
                          { return mviewer::core::rotateImageFile(utf8, degrees); });

    const QString cur = currentImagePath();
    if (!cur.isEmpty() && paths.contains(cur))
        refreshOpenImage(m_imageViewer, m_previewPanel, m_metadataPanel, cur);
    reportRotateBatch(this, statusBar(), paths, batch, degrees);
    if (m_secondaryThumbnailPanel)
    {
        for (const QString &path : paths)
            m_secondaryThumbnailPanel->invalidateSourceImage(path);
    }
    // Keep the pre-rotate multi-selection after thumbnail invalidate / rebuild.
    restoreMultiSelection(m_thumbnailPanel, m_selection, paths, cur);
}

void MainWindow::flipCurrentImage(bool horizontal)
{
    if (m_compareView && m_compareView->isVisible())
    {
        m_compareView->flipCurrentCell(horizontal);
        return;
    }

    const QStringList paths =
        transformTargetPaths(m_selection, m_thumbnailPanel, currentImagePath());
    if (paths.isEmpty())
    {
        showStatus(statusBar(), tr("No images to flip"), 2000);
        return;
    }

    if (!confirmTransform(this, paths, FileTransformKind::Flip))
        return;

    const FileTransformBatch batch =
        runTransformBatch(this, m_imageViewer, m_previewPanel, m_thumbnailPanel, paths,
                          FileTransformKind::Flip, [horizontal](const std::string &utf8)
                          { return mviewer::core::flipImageFile(utf8, horizontal); });

    const QString cur = currentImagePath();
    if (!cur.isEmpty() && paths.contains(cur))
        refreshOpenImage(m_imageViewer, m_previewPanel, m_metadataPanel, cur);
    reportFlipBatch(this, statusBar(), paths, batch, horizontal);
    if (m_secondaryThumbnailPanel)
    {
        for (const QString &path : paths)
            m_secondaryThumbnailPanel->invalidateSourceImage(path);
    }
    restoreMultiSelection(m_thumbnailPanel, m_selection, paths, cur);
}
