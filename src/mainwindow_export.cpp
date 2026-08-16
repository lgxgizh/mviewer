// MainWindow report and image export (M20 P0#1).
#include "mainwindow_p.h"

namespace
{

struct ReportExportState
{
    mviewer::core::ReportContext context;
    QImage histogram;
    std::string output;
    std::string suffix;
    std::string body;
    std::string error;
    bool cancelled = false;
    bool success = false;
};

void encodeReportImages(ReportExportState &state, const TaskScheduler::TaskContext &ctx)
{
    if (!state.histogram.isNull())
    {
        QByteArray bytes;
        QBuffer stream(&bytes);
        if (state.histogram.save(&stream, "PNG"))
            state.context.histogramPng = bytes.toBase64().toStdString();
    }
    if (state.context.hasCompareBundle)
    {
        state.context.compareDiffPngs.resize(state.context.compareBundle.targets.size());
        for (size_t i = 0; i < state.context.compareBundle.targets.size(); ++i)
        {
            if (ctx.isCancelled())
            {
                state.cancelled = true;
                return;
            }
            const ImageData &diff = state.context.compareBundle.targets[i].diffHeatmap;
            if (diff.isNull())
                continue;
            const QImage image = mvcore::toQImage(diff);
            if (image.isNull())
                continue;
            QByteArray bytes;
            QBuffer stream(&bytes);
            if (image.save(&stream, "PNG"))
                state.context.compareDiffPngs[i] = bytes.toBase64().toStdString();
            const_cast<TaskScheduler::TaskContext &>(ctx).reportProgress(
                static_cast<int>((i + 1) * 70 / qMax<size_t>(1, state.context.compareBundle.targets.size())));
        }
    }
}

} // namespace

void MainWindow::cancelReportExport()
{
    ++m_reportGeneration;
    TaskScheduler::cancel(m_reportTask);
    m_reportTask.reset();
    if (m_reportProgress)
    {
        m_reportProgress->close();
        m_reportProgress->deleteLater();
        m_reportProgress = nullptr;
    }
}

void MainWindow::exportReport()
{
    const bool hasCompare = m_compareView && m_compareView->comparedImageCount() >= 2;
    // The AnalyzerModel intentionally retains the previous result for history
    // when a newer request is pending.  A single-image export must not expose
    // that retained text as if it belonged to the in-flight request. Compare
    // reports are an independent snapshot and remain exportable meanwhile.
    if (!hasCompare && m_analysisPanel && m_analysisPanel->reportAnalysisPending())
    {
        QMessageBox::information(this, tr("导出报告"), tr("分析仍在进行，请完成后再导出报告。"));
        return;
    }

    mviewer::core::ReportContext ctx;
    ctx.title = "MViewer Analysis Report";

    if (hasCompare)
    {
        // Capture the compare state once. Every output format reads this snapshot.
        ctx.compareBundle = m_compareView->buildReportBundle();
        ctx.hasCompareBundle = true;
    }

    if (!ctx.hasCompareBundle)
    {
        if (currentImagePath().isEmpty())
        {
            QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
            return;
        }

        const QString path = currentImagePath();
        ctx.imagePath = path.toStdString();
        if (m_analyzer)
        {
            const AnalysisPanel::ReportAnalysisState state =
                m_analysisPanel ? m_analysisPanel->reportAnalysisState()
                                 : AnalysisPanel::ReportAnalysisState::Unset;
            const bool mayUseStoredResult =
                state == AnalysisPanel::ReportAnalysisState::Unset ||
                state == AnalysisPanel::ReportAnalysisState::Available;
            const QString result = mayUseStoredResult ? m_analyzer->resultText(path) : QString();
            const QString producerId = mayUseStoredResult ? m_analyzer->resultAnalyzerId(path)
                                                          : QString();
            const bool producerMatches =
                state != AnalysisPanel::ReportAnalysisState::Available ||
                !m_analysisPanel || m_analysisPanel->reportAnalyzerId() == producerId;
            if (!result.isEmpty() && producerMatches)
            {
                ctx.analysis.analyzerId = producerId.toStdString();
                ctx.analysis.resultText = result.toStdString();
                ctx.hasAnalysis = true;
            }
        }
    }
    else
    {
        const bool validReference = ctx.compareBundle.referenceIndex >= 0 &&
                                    ctx.compareBundle.referenceIndex <
                                        static_cast<int>(ctx.compareBundle.images.size());
        if (validReference)
        {
            ctx.imagePath =
                ctx.compareBundle.images[static_cast<size_t>(ctx.compareBundle.referenceIndex)];
        }
        else if (m_compareView)
        {
            ctx.imagePath = m_compareView->focusImagePath().toStdString();
        }
    }
    if (ctx.imagePath.empty())
    {
        QMessageBox::information(this, tr("导出报告"), tr("请先打开一张图片。"));
        return;
    }

    const QString out = QFileDialog::getSaveFileName(
        this, tr("导出报告"), QString(),
        tr("HTML 文件 (*.html);;Markdown 文件 (*.md);;JSON 文件 (*.json);;CSV 文件 (*.csv)"));
    if (out.isEmpty())
        return;
    const QFileInfo fi(out);
    const std::string suffix = fi.suffix().toLower().toUtf8().toStdString();
    startReportExport(std::move(ctx), out, suffix);
}

void MainWindow::startReportExport(mviewer::core::ReportContext ctx, const QString &out,
                                      std::string suffix)
{
    cancelReportExport();
    auto state = std::make_shared<ReportExportState>();
    state->context = std::move(ctx);
    state->histogram = m_analysisPanel ? m_analysisPanel->histogramPixmap().toImage() : QImage();
    state->output = out.toUtf8().toStdString();
    state->suffix = suffix;
    const uint64_t generation = ++m_reportGeneration;
    QPointer<MainWindow> guard(this);
    m_reportProgress = new QProgressDialog(tr("正在生成报告…"), tr("取消"), 0, 100, this);
    m_reportProgress->setWindowModality(Qt::WindowModal);
    m_reportProgress->setAutoClose(false);
    m_reportProgress->setMinimumDuration(0);
    connect(m_reportProgress, &QProgressDialog::canceled, this, &MainWindow::cancelReportExport);
    m_reportProgress->show();
    m_reportTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [state](const TaskScheduler::TaskContext &task)
        {
            if (task.isCancelled())
            {
                state->cancelled = true;
                return;
            }
            encodeReportImages(*state, task);
            if (state->cancelled || task.isCancelled())
            {
                state->cancelled = true;
                return;
            }
            if (state->suffix == "json")
                state->body = mviewer::core::buildReportJson(state->context);
            else if (state->suffix == "csv")
                state->body = mviewer::core::buildReportCsv(state->context);
            else if (state->suffix == "md")
                state->body = mviewer::core::buildReportMarkdown(state->context);
            else
                state->body = mviewer::core::buildReportHtml(state->context);
            if (state->body.empty())
            {
                state->error = "报告内容为空。";
                return;
            }
            if (task.isCancelled())
            {
                state->cancelled = true;
                return;
            }
            if (!mviewer::exportjob::writeTextAtomically(state->output, state->body))
            {
                state->error = "无法原子写入目标文件。";
                return;
            }
            state->success = true;
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [guard, state, generation, out]()
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, state, generation, out]()
                {
                    MainWindow *window = guard.data();
                    if (!window || generation != window->m_reportGeneration)
                        return;
                    if (window->m_reportProgress)
                    {
                        window->m_reportProgress->close();
                        window->m_reportProgress->deleteLater();
                        window->m_reportProgress = nullptr;
                    }
                    window->m_reportTask.reset();
                    if (state->cancelled)
                        return;
                    if (!state->success)
                    {
                        QMessageBox::critical(window, QObject::tr("导出报告"),
                                              QString::fromUtf8(state->error.c_str()));
                        return;
                    }
                    QMessageBox::information(window, QObject::tr("导出报告"),
                                             QObject::tr("已导出：%1").arg(out));
                },
                Qt::QueuedConnection);
        },
        [guard, generation](int progress)
        {
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [guard, generation, progress]()
                {
                    MainWindow *window = guard.data();
                    if (window && generation == window->m_reportGeneration &&
                        window->m_reportProgress)
                        window->m_reportProgress->setValue(progress);
                },
                Qt::QueuedConnection);
        });
    if (!m_reportTask)
    {
        cancelReportExport();
        QMessageBox::warning(this, tr("导出报告"), tr("后台任务被调度器拒绝。"));
    }
}
void MainWindow::exportImages()
{
    // A-3 / M17: SelectionModel → gallery selection → filtered (rating/flag) set
    // → full directory. Prefer the filtered visible set over the unfiltered
    // directory so "export what I see" matches the rating/flag filters.
    QStringList paths = resolveSelectedPaths(true);
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->visiblePaths();
    if (paths.isEmpty() && m_thumbnailPanel)
        paths = m_thumbnailPanel->pathList();
    if (paths.isEmpty())
    {
        QMessageBox::information(this, tr("导出图片"), tr("请先打开一个图片目录。"));
        return;
    }

    ExportDialog dlg(this);
    dlg.setSources(paths);
    dlg.setWindowTitle(tr("导出图片 — %1 张").arg(paths.size()));
    dlg.exec();
}
