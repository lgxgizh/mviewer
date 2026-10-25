// Batch rotate / flip of the current selection. Synchronous for the caller:
// the UI waits on a local event loop until the in-flight file finishes.
#include "mainwindow_p.h"

#include "core/command/RotateFilesCommand.h"
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
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QWidget>

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

FileTransformBatch batchFromRotateCommand(const RotateFilesCommand &command)
{
    FileTransformBatch batch;
    batch.successCount = command.successCount();
    batch.canceled = command.canceled();
    const std::vector<std::string> &failed = command.failedPaths();
    const std::vector<std::string> &errors = command.failedErrors();
    const std::vector<int> &codes = command.failedErrorCodes();
    for (size_t i = 0; i < failed.size(); ++i)
    {
        mviewer::core::ImageFileRotateResult synthetic;
        synthetic.error = i < errors.size() ? errors[i] : std::string();
        const int code = i < codes.size() ? codes[i] : 0;
        synthetic.errorCode = static_cast<mviewer::core::ImageRotateError>(code);
        batch.failedPaths.append(QString::fromStdString(failed[i]));
        batch.failedErrors.append(ImageViewer::rotateFailureUserMessage(synthetic));
    }
    return batch;
}

void showUndoLimitStatus(QStatusBar *bar, const RotateFilesCommand &command)
{
    if (command.blockedByPerFileLimit())
    {
        showStatus(bar,
                   MainWindow::tr("Undo is unavailable: a file is larger than the 200 MB undo "
                                  "backup limit."),
                   4000);
    }
    if (command.droppedOldestBackup())
    {
        showStatus(bar,
                   MainWindow::tr("The oldest undo backup was discarded to stay within the 1 GB "
                                  "limit."),
                   4000);
    }
}

} // namespace

void MainWindow::refreshTransformedPaths(const QStringList &paths)
{
    for (const QString &path : paths)
    {
        const std::string utf8 = path.toUtf8().toStdString();
        noteTransformSuccess(m_thumbnailPanel, path, utf8);
        if (m_secondaryThumbnailPanel)
            m_secondaryThumbnailPanel->invalidateSourceImage(path);
    }
    const QString current = currentImagePath();
    if (!current.isEmpty() && paths.contains(current))
        refreshOpenImage(m_imageViewer, m_previewPanel, m_metadataPanel, current);
    // Do not refresh() the panes. That calls setDirectory(), which clears the
    // gallery model and publishes an empty selection. The async rescan then
    // completes after applyTransformSelection and drops the batch multi-select.
    // Thumbnails are already invalidated above. A later directory-monitor delta
    // updates size and mtime while keeping the current selection.
}

void MainWindow::runListedTransform(const QStringList &paths, bool rotate, int degrees,
                                    bool horizontal)
{
    if (paths.isEmpty())
        return;
    const FileTransformKind kind = rotate ? FileTransformKind::Rotate : FileTransformKind::Flip;
    if (!confirmTransform(this, paths, kind))
        return;

    const SplitSelectionRestorePlan selectionPlan = captureTransformSelection();
    std::vector<std::string> utf8Paths;
    utf8Paths.reserve(static_cast<size_t>(paths.size()));
    for (const QString &path : paths)
        utf8Paths.push_back(path.toUtf8().toStdString());

    const RotateFilesCommand::Op op =
        rotate ? RotateFilesCommand::Op::Rotate
               : (horizontal ? RotateFilesCommand::Op::FlipH : RotateFilesCommand::Op::FlipV);
    auto command = std::make_unique<RotateFilesCommand>(std::move(utf8Paths), op, degrees);
    BatchFileProgress progress(this, static_cast<int>(paths.size()), kind);
    command->setProgressCallback(
        [&progress](int finished, int)
        {
            if (progress.wasCanceled())
                return false;
            progress.advance(finished);
            return !progress.wasCanceled();
        });
    const QPointer<MainWindow> guard(this);
    command->setPathsChangedCallback(
        [guard](const std::vector<std::string> &changed)
        {
            if (!guard)
                return;
            QStringList refreshed;
            refreshed.reserve(static_cast<int>(changed.size()));
            for (const std::string &path : changed)
                refreshed.append(QString::fromStdString(path));
            guard->refreshTransformedPaths(refreshed);
        });
    for (const QString &path : paths)
    {
        releaseSourceForRewrite(m_imageViewer, m_previewPanel, path, path.toUtf8().toStdString());
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    command->execute();
    QApplication::restoreOverrideCursor();
    progress.dismiss();

    const FileTransformBatch batch = batchFromRotateCommand(*command);
    applyTransformSelection(selectionPlan);
    if (rotate)
        reportRotateBatch(this, statusBar(), paths, batch, degrees);
    else
        reportFlipBatch(this, statusBar(), paths, batch, horizontal);
    showUndoLimitStatus(statusBar(), *command);

    command->setProgressCallback({});
    if (command->lastError().empty() && command->canUndo())
        m_cmdStack.recordExecuted(std::move(command));
}

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
    runListedTransform(paths, true, degrees, false);
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
    runListedTransform(paths, false, 0, horizontal);
}

SplitSelectionRestorePlan MainWindow::captureTransformSelection() const
{
    const bool split = m_selection && m_selection->isSplitModeEnabled();
    using Pane = SelectionModel::PaneSlot;
    const QStringList primary =
        m_selection ? m_selection->paneSelection(Pane::Primary) : QStringList();
    const QStringList secondary =
        m_selection ? m_selection->paneSelection(Pane::Secondary) : QStringList();
    const QString primaryCurrent =
        m_selection ? m_selection->paneCurrentImage(Pane::Primary) : QString();
    const QString secondaryCurrent =
        m_selection ? m_selection->paneCurrentImage(Pane::Secondary) : QString();
    const QStringList single = m_selection ? m_selection->selection() : QStringList();
    const QString singleCurrent = m_selection ? m_selection->currentImage() : QString();
    const QStringList primaryList = m_thumbnailPanel ? m_thumbnailPanel->pathList() : QStringList();
    const QStringList secondaryList =
        m_secondaryThumbnailPanel ? m_secondaryThumbnailPanel->pathList() : QStringList();
    return splitSelectionRestorePlan(split, primary, primaryCurrent, secondary, secondaryCurrent,
                                     primaryList, secondaryList, single, singleCurrent,
                                     currentImagePath());
}

void MainWindow::applyTransformSelection(const SplitSelectionRestorePlan &plan)
{
    if (!plan.split)
    {
        restoreMultiSelection(m_thumbnailPanel, m_selection, plan.singleSelection,
                              plan.singleCurrent);
        return;
    }
    using Pane = SelectionModel::PaneSlot;
    if (m_selection)
    {
        m_selection->setPaneSelection(Pane::Primary, plan.primaryModel, plan.primaryCurrent);
        m_selection->setPaneSelection(Pane::Secondary, plan.secondaryModel, plan.secondaryCurrent);
    }
    const auto focusOf = [](const QStringList &view, const QString &current)
    { return view.contains(current) ? current : QString(); };
    if (m_thumbnailPanel)
        m_thumbnailPanel->selectPaths(plan.primaryView,
                                      focusOf(plan.primaryView, plan.primaryCurrent), false);
    if (m_secondaryThumbnailPanel)
    {
        m_secondaryThumbnailPanel->selectPaths(
            plan.secondaryView, focusOf(plan.secondaryView, plan.secondaryCurrent), false);
    }
    if (m_selection && !plan.globalCurrent.isEmpty())
        m_selection->setCurrentImage(plan.globalCurrent);
}
