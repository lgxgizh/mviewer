// Batch analysis export (M17): pick an analyzer, run it over the selection in
// the background and write CSV/JSON.
//
// This used to live in thumbnailpanel_fileops.cpp, which had grown to 948 lines
// against a 1000-line responsibility ceiling. The split is behaviour-preserving:
// every method below keeps its declaration in thumbnailpanel.h and nothing else
// changed.
#include "thumbnailpanel_p.h"

#include "core/analysis/ExportReport.h"
#include "application/ImageLoadingService.h"
#include "core/analyzer/Analyzer.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>

#include <chrono>
#include <mutex>

void ThumbnailPanel::batchAnalyzeExport()
{
    // Prefer multi-selection; fall back to the currently visible (filtered) set
    // so rating/flag filters flow into batch analysis export (M17).
    QStringList paths = selectedPaths();
    if (paths.isEmpty())
        paths = visiblePaths();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("Please select an image first, or open a filtered directory."));
        return;
    }

    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    const std::vector<std::string> ids = reg.availableAnalyzers();
    if (ids.empty())
    {
        QMessageBox::warning(this, tr("Batch Analysis Export"),
                             tr("No analyzer is currently available."));
        return;
    }

    // Let the user pick which analyzer to run (default: first registered).
    QStringList labels;
    for (const auto &id : ids)
    {
        const auto info = reg.infoFor(id);
        labels << (info ? QString::fromStdString(info->name) : QString::fromStdString(id));
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("Batch Analysis Export"),
                                                 tr("Select an analyzer:"), labels, 0, false, &ok);
    if (!ok || chosen.isEmpty())
        return;
    const int chosenIdx = labels.indexOf(chosen);
    if (chosenIdx < 0 || chosenIdx >= static_cast<int>(ids.size()))
        return;
    const std::string analyzerId = ids[static_cast<size_t>(chosenIdx)];

    const QString out = QFileDialog::getSaveFileName(this, tr("Export analysis results"), QString(),
                                                     tr("CSV (*.csv);;JSON (*.json)"));
    if (out.isEmpty())
        return;

    runBatchAnalyzeExportAsync(paths, analyzerId, out);
    return;
}

void ThumbnailPanel::ensureBatchProgressDialog()
{
    if (m_batchProgress)
        return;
    m_batchProgress =
        new QProgressDialog(tr("Running batch analysis..."), tr("Cancel"), 0, 100, this);
    m_batchProgress->setWindowModality(Qt::WindowModal);
    m_batchProgress->setAutoClose(false);
    m_batchProgress->setMinimumDuration(0);
    connect(m_batchProgress, &QProgressDialog::canceled, this,
            [this]()
            {
                if (m_batchTask)
                    TaskScheduler::cancel(m_batchTask);
            });
}

void ThumbnailPanel::finishBatchAnalyzeExport(bool cancelled, bool writeOk, size_t resultCount,
                                              const QString &output)
{
    m_batchTask.reset();
    if (m_batchProgress)
        m_batchProgress->close();
    if (cancelled)
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("Batch analysis cancelled."));
    }
    else if (!writeOk)
    {
        QMessageBox::critical(this, tr("Batch Analysis Export"),
                              tr("Cannot write: %1").arg(output));
    }
    else
    {
        QMessageBox::information(
            this, tr("Batch Analysis Export"),
            tr("Exported %1 results → %2").arg(static_cast<qlonglong>(resultCount)).arg(output));
    }
}

void ThumbnailPanel::runBatchAnalyzeExportAsync(const QStringList &paths,
                                                const std::string &analyzerId,
                                                const QString &output)
{
    if (m_batchTask)
    {
        QMessageBox::information(this, tr("Batch Analysis Export"),
                                 tr("A batch analysis is already running."));
        return;
    }

    ensureBatchProgressDialog();
    m_batchProgress->setValue(0);
    m_batchProgress->show();

    struct State
    {
        std::mutex mutex;
        QString output;
        std::string analyzerId;
        QStringList paths;
        std::string body;
        size_t resultCount = 0;
        bool cancelled = false;
        bool writeOk = false;
    };
    const auto state = std::make_shared<State>();
    state->output = output;
    state->analyzerId = analyzerId;
    state->paths = paths;
    const QPointer<ThumbnailPanel> guard(this);
    const bool asJson = output.endsWith(".json", Qt::CaseInsensitive);

    m_batchTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [state, asJson](const TaskScheduler::TaskContext &ctx)
        {
            std::vector<mviewer::analyzer::AnalyzerResult> results;
            results.reserve(static_cast<size_t>(state->paths.size()));
            AnalyzerRegistry &registry = AnalyzerRegistry::instance();
            for (int i = 0; i < state->paths.size(); ++i)
            {
                if (ctx.isCancelled())
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->cancelled = true;
                    return;
                }

                const QString &path = state->paths.at(i);
                const auto loaded =
                    mviewer::application::ImageLoadingService::instance().load(path.toStdString());
                // `loaded` and the analyzer are iteration-local. No vector of
                // full-resolution frames is retained across the batch.
                if (loaded.frame)
                {
                    auto analyzer = registry.create(state->analyzerId);
                    if (analyzer && analyzer->analyze(*loaded.frame))
                        results.push_back({path.toStdString(), analyzer->resultMetrics(),
                                           analyzer->resultText()});
                }
                const_cast<TaskScheduler::TaskContext &>(ctx).reportProgress(
                    (i + 1) * 100 / qMax(1, state->paths.size()));
            }
            if (ctx.isCancelled())
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->cancelled = true;
                return;
            }
            const auto report = mviewer::core::buildBatchReport(state->analyzerId, results);
            const size_t resultCount = results.size();
            const std::string body = asJson ? report.toJson() : report.toCsv();
            const bool writeOk =
                mviewer::exportjob::writeTextAtomically(state->output.toUtf8().toStdString(), body);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->resultCount = resultCount;
            state->body = body;
            state->writeOk = writeOk;
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state]()
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, state]()
                {
                    if (!guard)
                        return;
                    bool cancelled = false;
                    bool writeOk = false;
                    size_t resultCount = 0;
                    QString output;
                    {
                        std::lock_guard<std::mutex> lock(state->mutex);
                        cancelled = state->cancelled;
                        writeOk = state->writeOk;
                        resultCount = state->resultCount;
                        output = state->output;
                    }
                    guard->finishBatchAnalyzeExport(cancelled, writeOk, resultCount, output);
                },
                Qt::QueuedConnection);
        },
        [guard](int progress)
        {
            QMetaObject::invokeMethod(
                qApp,
                [guard, progress]()
                {
                    if (guard && guard->m_batchProgress)
                        guard->m_batchProgress->setValue(progress);
                },
                Qt::QueuedConnection);
        });

    if (!m_batchTask)
    {
        m_batchProgress->close();
        QMessageBox::warning(this, tr("Batch Analysis Export"),
                             tr("Background task was rejected by the scheduler."));
    }
}
