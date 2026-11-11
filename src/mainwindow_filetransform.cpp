// Batch rotate / flip of the current selection. Filtering, confirmation, and
// source-handle release stay on the UI thread. execute() runs on TaskScheduler.
#include "mainwindow_p.h"

#include "core/command/RotateFilesCommand.h"
#include "core/image/FrameSequence.h"
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
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QWidget>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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

// forceShow() is protected and latches shownOnce so the dialog appears
// immediately. Cancel is delivered by the dialog's own event loop; this type
// does not pump events.
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
        m_dialog = new ShownProgressDialog(label, MainWindow::tr("Cancel"), 0, total, parent);
        m_dialog->setObjectName(QStringLiteral("batchRotateFlipProgress"));
        m_dialog->setWindowModality(Qt::WindowModal);
        m_dialog->setAutoClose(false);
        m_dialog->setAutoReset(false);
        m_dialog->setMinimumDuration(0);
        m_dialog->setValue(0);
        m_dialog->reveal();
    }

    ~BatchFileProgress()
    {
        dismiss();
    }
    BatchFileProgress(const BatchFileProgress &) = delete;
    BatchFileProgress &operator=(const BatchFileProgress &) = delete;

    QProgressDialog *dialog() const
    {
        return m_dialog.data();
    }

    void advance(int finished)
    {
        if (!m_dialog || m_dialog->wasCanceled())
            return;
        m_dialog->setLabelText(finishedLabel(finished));
        m_dialog->setValue(finished);
    }

    void dismiss()
    {
        if (!m_dialog)
            return;
        m_dialog->hide();
        delete m_dialog.data();
        m_dialog.clear();
    }

  private:
    QString finishedLabel(int finished) const
    {
        return (m_kind == FileTransformKind::Rotate)
                   ? MainWindow::tr("Rotated %1 / %2").arg(finished).arg(m_total)
                   : MainWindow::tr("Flipped %1 / %2").arg(finished).arg(m_total);
    }

    QPointer<ShownProgressDialog> m_dialog;
    int m_total = 0;
    FileTransformKind m_kind = FileTransformKind::Rotate;
};

struct ListedTransformJob
{
    std::unique_ptr<RotateFilesCommand> command;
    std::unique_ptr<BatchFileProgress> progress;
    std::atomic<int> finished{0};
    std::atomic_bool cancel{false};
    SplitSelectionRestorePlan selectionPlan;
    QStringList supported;
    int skippedUnsupported = 0;
    bool rotate = false;
    int degrees = 0;
    bool horizontal = false;
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
    mviewer::core::FrameSequenceReader::releaseFile(utf8);
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

void mentionSkippedUnsupported(QStatusBar *bar, int skipped)
{
    if (skipped <= 0 || !bar)
        return;
    const QString extra = MainWindow::tr("%n unsupported file(s) skipped", nullptr, skipped);
    const QString current = bar->currentMessage();
    if (current.isEmpty())
        bar->showMessage(extra, 3000);
    else
        bar->showMessage(current + QStringLiteral(" — ") + extra, 3000);
}

QStringList filterListedTransformPaths(QWidget *parent, const QStringList &paths, bool rotate)
{
    const QStringList supported = rotatableFiles(paths);
    if (!supported.isEmpty() || paths.isEmpty())
        return supported;
    const QString reason =
        MainWindow::tr("Rotate/flip rewrites the file and supports only JPEG, PNG, BMP and WebP");
    QMessageBox::information(
        parent, rotate ? MainWindow::tr("Rotation failed") : MainWindow::tr("Flip failed"), reason);
    return {};
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

// Cancel flag + worker progress callback; UI updates are posted to the UI
// thread. `alive` only gates the queued progress update.
void wireListedTransformProgress(const std::shared_ptr<ListedTransformJob> &state,
                                 const QPointer<QObject> &alive)
{
    if (QProgressDialog *dialog = state->progress->dialog())
    {
        std::atomic_bool *cancelFlag = &state->cancel;
        QObject::connect(dialog, &QProgressDialog::canceled, dialog,
                         [cancelFlag]() { cancelFlag->store(true, std::memory_order_relaxed); });
    }
    state->command->setProgressCallback(
        [state, alive](int finished, int)
        {
            if (state->cancel.load(std::memory_order_relaxed))
                return false;
            state->finished.store(finished, std::memory_order_relaxed);
            QMetaObject::invokeMethod(
                qApp,
                [state, alive]()
                {
                    if (!alive || !state->progress)
                        return;
                    state->progress->advance(state->finished.load(std::memory_order_relaxed));
                },
                Qt::QueuedConnection);
            return !state->cancel.load(std::memory_order_relaxed);
        });
}

// Undo/Redo run on the UI thread: refresh synchronously as before. execute()
// runs on a worker: hop to the UI thread.
void wireListedTransformRefresh(RotateFilesCommand &command,
                                std::function<void(const QStringList &)> refresh)
{
    command.setPathsChangedCallback(
        [refresh = std::move(refresh)](const std::vector<std::string> &changed)
        {
            QStringList refreshed;
            refreshed.reserve(static_cast<int>(changed.size()));
            for (const std::string &path : changed)
                refreshed.append(QString::fromStdString(path));
            if (QThread::currentThread() == qApp->thread())
            {
                refresh(refreshed);
                return;
            }
            QMetaObject::invokeMethod(
                qApp, [refresh, refreshed]() { refresh(refreshed); }, Qt::QueuedConnection);
        });
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
        if (m_tertiaryThumbnailPanel)
            m_tertiaryThumbnailPanel->invalidateSourceImage(path);
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
    if (paths.isEmpty() || m_transformBusy)
        return;
    const QStringList supported = filterListedTransformPaths(this, paths, rotate);
    if (supported.isEmpty())
        return;
    const int skippedUnsupported = static_cast<int>(paths.size() - supported.size());
    const FileTransformKind kind = rotate ? FileTransformKind::Rotate : FileTransformKind::Flip;
    if (!confirmTransform(this, supported, kind))
        return;

    const SplitSelectionRestorePlan selectionPlan = captureTransformSelection();
    std::vector<std::string> utf8Paths;
    utf8Paths.reserve(static_cast<size_t>(supported.size()));
    for (const QString &path : supported)
        utf8Paths.push_back(path.toUtf8().toStdString());

    const RotateFilesCommand::Op op =
        rotate ? RotateFilesCommand::Op::Rotate
               : (horizontal ? RotateFilesCommand::Op::FlipH : RotateFilesCommand::Op::FlipV);
    auto command = std::make_unique<RotateFilesCommand>(std::move(utf8Paths), op, degrees);
    const QPointer<MainWindow> guard(this);
    auto state = std::make_shared<ListedTransformJob>();
    state->command = std::move(command);
    state->progress =
        std::make_unique<BatchFileProgress>(this, static_cast<int>(supported.size()), kind);
    state->selectionPlan = selectionPlan;
    state->supported = supported;
    state->skippedUnsupported = skippedUnsupported;
    state->rotate = rotate;
    state->degrees = degrees;
    state->horizontal = horizontal;
    wireListedTransformProgress(state, guard);
    wireListedTransformRefresh(*state->command,
                               [guard](const QStringList &refreshed)
                               {
                                   if (guard)
                                       guard->refreshTransformedPaths(refreshed);
                               });
    for (const QString &path : supported)
    {
        releaseSourceForRewrite(m_imageViewer, m_previewPanel, path, path.toUtf8().toStdString());
    }

    m_transformBusy = true;
    m_transformTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::UI, [state](const TaskScheduler::TaskContext &)
        { state->command->execute(); }, {}, std::chrono::steady_clock::time_point::max(),
        [guard, state]()
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, state]()
                {
                    if (!guard)
                    {
                        // Break the state -> command -> progress callback ->
                        // state cycle when the window is already gone.
                        if (state->command)
                            state->command->setProgressCallback({});
                        return;
                    }
                    MainWindow *window = guard.data();
                    window->m_transformBusy = false;
                    window->m_transformTask.reset();
                    if (state->progress)
                        state->progress->dismiss();

                    RotateFilesCommand *command = state->command.get();
                    const FileTransformBatch batch = batchFromRotateCommand(*command);
                    window->applyTransformSelection(state->selectionPlan);
                    if (state->rotate)
                        reportRotateBatch(window, window->statusBar(), state->supported, batch,
                                          state->degrees);
                    else
                        reportFlipBatch(window, window->statusBar(), state->supported, batch,
                                        state->horizontal);
                    showUndoLimitStatus(window->statusBar(), *command);
                    mentionSkippedUnsupported(window->statusBar(), state->skippedUnsupported);

                    command->setProgressCallback({});
                    if (command->lastError().empty() && command->canUndo())
                        window->m_cmdStack.recordExecuted(std::move(state->command));
                },
                Qt::QueuedConnection);
        });
    if (!m_transformTask)
    {
        m_transformBusy = false;
        if (state->progress)
            state->progress->dismiss();
    }
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
    auto plan = splitSelectionRestorePlan(split, primary, primaryCurrent, secondary,
                                          secondaryCurrent, primaryList, secondaryList, single,
                                          singleCurrent, currentImagePath());
    if (m_tripleBrowseEnabled && m_selection && m_tertiaryThumbnailPanel)
    {
        plan.tertiaryModel = m_selection->paneSelection(Pane::Tertiary);
        plan.tertiaryCurrent = m_selection->paneCurrentImage(Pane::Tertiary);
        plan.tertiaryView =
            orderPreservingIntersect(plan.tertiaryModel, m_tertiaryThumbnailPanel->pathList());
    }
    return plan;
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
        if (m_tripleBrowseEnabled)
            m_selection->setPaneSelection(Pane::Tertiary, plan.tertiaryModel, plan.tertiaryCurrent);
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
    if (m_tripleBrowseEnabled && m_tertiaryThumbnailPanel)
        m_tertiaryThumbnailPanel->selectPaths(
            plan.tertiaryView, focusOf(plan.tertiaryView, plan.tertiaryCurrent), false);
    if (m_selection && !plan.globalCurrent.isEmpty())
        m_selection->setCurrentImage(plan.globalCurrent);
}
