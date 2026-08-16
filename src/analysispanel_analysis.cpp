#include "analysispanel.h"
#include "analysispanel_analysis_types.h"
#include "analyzermodel.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/image/QtConvert.h"
#include "widgets/rawimageview.h"

#include <QPushButton>
#include <QShowEvent>
#include <QTimer>

#include <algorithm>
#include <string>
#include <utility>

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    setMinimumWidth(360);
    setMinimumHeight(480);
}

AnalysisPanel::~AnalysisPanel()
{
    // Cancel any in-flight analysis job (frame materialization or analyzer-only
    // rerun); its queued delivery is guarded by a QPointer so it can never
    // touch this panel after destruction.
    invalidateAnalysis();
}

// A-7.2: rebuild the analyzer combo from the live registry/pipeline.
// A-7.1 / A-7.3: unified entry — select, run, and surface the Plugin tab.
void AnalysisPanel::runAnalyzer(const QString &id)
{
    if (id.isEmpty())
        return;
    selectAnalyzer(id);
    reanalyze();
    // Surface the Plugin tab so context-menu / combo runs land in one place.
    if (m_tabs)
    {
        const int pluginTab = m_tabs->indexOf(m_pluginResult);
        if (pluginTab >= 0)
            m_tabs->setCurrentIndex(pluginTab);
    }
}

// Run the currently-selected analyzer over the left frame and the active ROI.
// The analyzer consumes a domain Selection, never a QRect. Creation/execution
// routes through the injected AnalyzerPipeline so the panel never touches the
// registry directly (M15 P0#3).
void AnalysisPanel::reanalyze()
{
    const QString id = m_analyzerCombo ? m_analyzerCombo->currentData().toString() : QString();
    if (m_frameDirty && m_frameA && m_frameA->isValid())
    {
        if (isVisible())
            scheduleAnalysis();
        return;
    }
    if (id == "builtin_compare")
    {
        invalidateAnalysis();
        clearAnalyzerResultSurface();
        setReportAnalysisState(ReportAnalysisState::NoResult);
        updateComparePage();
        if (m_tabs && m_compareLabel)
        {
            const int tab = m_tabs->indexOf(m_compareLabel);
            if (tab >= 0)
                m_tabs->setCurrentIndex(tab);
        }
        return;
    }
    if (m_frameA && m_frameA->isValid() && m_hasA && !m_imageA.isNull())
    {
        scheduleAnalyzerRun();
        return;
    }
    if (runLegacyAnalyzer(id))
        return;
    if (m_hasA && m_hasROI)
        runRoiAnalysis();
}

bool AnalysisPanel::runLegacyAnalyzer(const QString &id)
{
    // Legacy path: no valid ImageFrame — a QImage-only panel keeps the old
    // synchronous analyzer/ROI-fallback behavior needed for compatibility
    // (analyzers require an ImageFrame, so this stays on the UI thread).
    if (m_frameA && !m_frameA->pixels().isNull() && !id.isEmpty())
    {
        // M24 (C#7): a failing/throwing analyzer must degrade to an error note
        // instead of taking down the panel or the application.
        bool ran = false;
        try
        {
            auto analyzer = (m_pipeline ? m_pipeline->create(id.toStdString())
                                        : AnalyzerRegistry::instance().create(id.toStdString()));
            if (analyzer)
            {
                // Prefer ROI when set; otherwise analyze the full frame.
                const bool ok = m_hasROI ? analyzer->analyzeRegion(*m_frameA, m_roi)
                                         : analyzer->analyze(*m_frameA);
                if (ok)
                {
                    ran = true;
                    m_statsA.pixelCount = m_hasROI
                                              ? std::max(0, m_roi.width) * std::max(0, m_roi.height)
                                              : m_frameA->width() * m_frameA->height();
                    const std::string text = analyzer->resultText();
                    const auto metrics = analyzer->resultMetrics();
                    const auto *hist = dynamic_cast<const HistogramAnalyzer *>(analyzer.get());
                    if (hist)
                    {
                        const auto &h = hist->result();
                        m_statsA.lumMean = h.lumMean;
                        m_statsA.rMean = h.rMean;
                        m_statsA.gMean = h.gMean;
                        m_statsA.bMean = h.bMean;
                        renderHistogramPixmap(h);
                    }
                    // Unified result surface: Histogram stats + Plugin tab.
                    const QString html = QString("<h3>%1</h3><p>%2</p>")
                                             .arg(QString::fromStdString(analyzer->name()))
                                             .arg(QString::fromStdString(text));
                    m_statsLabel->setText(html);
                    if (m_pluginResult)
                    {
                        QString pluginHtml = html;
                        if (!metrics.empty())
                        {
                            pluginHtml += "<table>";
                            for (const auto &[k, v] : metrics)
                                pluginHtml += QString("<tr><td>%1</td><td>%2</td></tr>")
                                                  .arg(QString::fromStdString(k))
                                                  .arg(v, 0, 'f', 4);
                            pluginHtml += "</table>";
                        }
                        m_pluginResult->setText(pluginHtml);
                    }
                    // M21: publish plain result into AnalyzerModel (history + pin SSOT).
                    publishResult(QString::fromStdString(text));
                    setReportAnalysisState(ReportAnalysisState::Available, id);
                    return true;
                }
            }
        }
        catch (...)
        {
            // M24 (C#7): analyzer failure (e.g. buggy plugin) — surface a
            // graceful note instead of propagating an exception into the UI.
            const QString err = tr("分析器执行失败（%1）。").arg(id);
            m_statsLabel->setText(
                QString("<h3>%1</h3><p>%2</p>").arg(QStringLiteral("分析失败")).arg(err));
            if (m_pluginResult)
                m_pluginResult->setText(err);
            publishResult(err);
            setReportAnalysisState(ReportAnalysisState::Available, id);
            return true;
        }
    }

    return false;
}

void AnalysisPanel::runRoiAnalysis()
{
    if (m_hasA && m_hasROI)
    {
        m_statsA = AnalysisEngine::computeStatsROI(mvcore::fromQImage(m_imageA), m_roi);
        updateHistogramPage();
        // Publish a plain-text ROI summary (never HTML from m_statsLabel).
        const QString plain = QString("ROI %1x%2 @(%3,%4) lum=%5 r=%6 g=%7 b=%8")
                                  .arg(m_roi.width)
                                  .arg(m_roi.height)
                                  .arg(m_roi.x)
                                  .arg(m_roi.y)
                                  .arg(m_statsA.lumMean, 0, 'f', 2)
                                  .arg(m_statsA.rMean, 0, 'f', 2)
                                  .arg(m_statsA.gMean, 0, 'f', 2)
                                  .arg(m_statsA.bMean, 0, 'f', 2);
        publishResult(plain);
        const QString id = m_analyzerCombo ? m_analyzerCombo->currentData().toString() : QString();
        setReportAnalysisState(ReportAnalysisState::Available, id);
    }
}

void AnalysisPanel::setFrame(std::shared_ptr<ImageFrame> frame)
{
    // A new frame must immediately prevent the old result/image from being
    // presented as current: cancel the previous task and bump the generation so
    // any in-flight delivery (materializing OR analyzer-only) is discarded.
    invalidateAnalysis();
    m_frameA = std::move(frame);
    if (!m_frameA || !m_frameA->isValid())
    {
        clear();
        return;
    }
    // Merely selecting/storing a frame does not mean a report-producing job
    // has been accepted. Hidden panels defer materialization until shown, so
    // keep export available until scheduleAnalysis() accepts the worker job.
    setReportAnalysisState(ReportAnalysisState::Unset);
    // M28 P1-02/P1-04: materialization + analysis are deferred AND async. A
    // HIDDEN panel (the common case while browsing) stores the frame only — no
    // full-size QImage conversion and no Analysis task until it is shown. A
    // visible panel schedules exactly one cancellable, latest-wins AnalysisPool
    // task on the next event-loop turn (rapid frames cancel the previous task).
    m_frameDirty = true;
    // While the newest result is pending, the previous image/stats/result must
    // not be presented as current.
    if (m_hasA)
        resetImagePresentation();
    if (isVisible())
    {
        QTimer::singleShot(0, this,
                           [this]()
                           {
                               if (m_frameDirty)
                                   scheduleAnalysis();
                           });
    }
}

void AnalysisPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Submits the deferred materialization job; the worker runs all heavy work,
    // this only schedules (never blocks on conversion/stats/analyzer).
    if (m_frameDirty && m_frameA && m_frameA->isValid())
        scheduleAnalysis();
}

void AnalysisPanel::invalidateAnalysis()
{
    if (m_task)
        TaskScheduler::cancel(m_task);
    m_task.reset();
    ++m_gen;
}

// Presentation-time reset: the previous image/stats/results must not be shown
// as current while a newer automatic result is pending. Keeps the ROI state.
void AnalysisPanel::resetImagePresentation()
{
    m_imageA = m_imageB = QImage();
    m_hasA = m_hasB = false;
    m_statsA = m_statsB = ImageStats();
    m_noiseA = 0.0;
    m_noiseValid = false;
    m_statsLabel->clear();
    m_compareLabel->clear();
    m_diffPreview->clear();
    m_pluginResult->clear();
    m_histogramLabel->clear();
    m_rgbLabel->clear();
    m_rgbStatsLabel->clear();
    m_exposureLabel->clear();
    m_focusLabel->clear();
    m_metaLabel->clear();
}

// Accept a worker-computed result on the UI thread. Only cheap UI rendering
// runs here; the heavy stages all happened in the worker. The result is
// validated against the current generation, frame identity, and (for
// analyzer-only jobs) the publish path before any UI/model state is touched.
void AnalysisPanel::applyAnalysisResult(const AnalysisResult &r)
{
    if (r.generation != m_gen)
        return; // superseded (latest-wins)
    if (m_frameA.get() != r.frame.get())
        return; // stale frame identity
    if (!r.materialize && r.path != publishPath().toStdString())
        return; // analyzer-only result no longer matches the current request path
    m_task.reset(); // release the completed payload handle
    // Only an accepted MATERIALIZING delivery clears the dirty flag. An
    // analyzer-only delivery never runs while the frame is dirty (it is only
    // scheduled on a fully materialized frame), so it must not touch the
    // invariant.
    if (r.materialize)
        m_frameDirty = false;

    // Materializing request: present the frame and render the base pages from
    // the full-image stats, exactly as before. An analyzer-only request keeps
    // the already materialized image and base pages untouched.
    if (r.materialize)
    {
        if (r.image.isNull())
        {
            renderAnalysisUnavailable();
            return; // nothing to present (e.g. materialization failed)
        }
        m_imageA = r.image;
        m_imagePath = QString::fromStdString(r.path);
        m_hasA = true;
        m_hasB = false;
        m_statsA = r.stats; // full-image stats, mirroring applyFrameImage
        m_noiseA = r.noise;
        m_noiseValid = r.noiseValid;

        // Cheap page updates shared by every outcome, rendered from the
        // full-image stats (mirrors applyFrameImage before the legacy
        // reanalyze). The base Histogram page is populated for every accepted
        // frame regardless of the selected analyzer; analyzer-specific
        // branches override it below.
        updateHistogramPage();
        updateRgbPage();
        updateExposurePage();
        updateFocusPage();
        updateMetadataPage();

        if (r.isBuiltinCompare)
        {
            updateComparePage(); // one-frame path: cheap 'Need two images to compare'
            if (m_tabs && m_compareLabel)
            {
                const int tab = m_tabs->indexOf(m_compareLabel);
                if (tab >= 0)
                    m_tabs->setCurrentIndex(tab);
            }
            setReportAnalysisState(ReportAnalysisState::NoResult);
            return;
        }
    }

    if (r.analyzerFailed)
    {
        // M24 (C#7): analyzer failure -> graceful error note (same shape as the
        // legacy synchronous reanalyze() error path).
        const QString id = QString::fromStdString(r.id);
        const QString err = tr("分析器执行失败（%1）。").arg(id);
        m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("分析失败")).arg(err));
        if (m_pluginResult)
            m_pluginResult->setText(err);
        publishResult(err, QString::fromStdString(r.id));
        setReportAnalysisState(ReportAnalysisState::Available, QString::fromStdString(r.id));
        return;
    }

    if (r.analyzerOk)
    {
        // Same result surface as the legacy reanalyze(): Histogram + Plugin tab.
        renderAnalyzerOutcome(r);
        // M21: publish exactly once for the current path.
        publishResult(QString::fromStdString(r.resultText), QString::fromStdString(r.id));
        setReportAnalysisState(ReportAnalysisState::Available, QString::fromStdString(r.id));
        return;
    }

    if (r.roiFallback)
    {
        renderRoiOutcome(r);
        publishResult(QString::fromStdString(r.plainResult), QString::fromStdString(r.id));
        setReportAnalysisState(ReportAnalysisState::Available, QString::fromStdString(r.id));
        return;
    }

    if (r.noResult)
    {
        // Analyzer produced no result and no ROI fallback applies: show an
        // explicit note without fabricating a successful model result.
        renderNoResult();
        return;
    }
    renderAnalysisUnavailable();
}

// The path results are keyed to: the explicit setImage(path) wins, otherwise
// the current frame's metadata path. Mirrors publishResult().
QString AnalysisPanel::publishPath() const
{
    if (!m_imagePath.isEmpty())
        return m_imagePath;
    if (m_frameA)
        return QString::fromStdString(m_frameA->metadata().filePath);
    return QString();
}

// Shared analyzer-result rendering (automatic + manual deliveries). Renders the
// result surface (Histogram stats + Plugin tab) from the worker output WITHOUT
// mutating the base full-image stats that drive RGB/Exposure/Focus/Metadata.
void AnalysisPanel::renderAnalyzerOutcome(const AnalysisResult &r)
{
    if (r.hasHistogram)
        renderHistogramPixmap(r.histogram);
    const QString name = QString::fromStdString(r.analyzerName);
    const QString text = QString::fromStdString(r.resultText);
    const QString html = QString("<h3>%1</h3><p>%2</p>").arg(name).arg(text);
    m_statsLabel->setText(html);
    if (m_pluginResult)
    {
        QString pluginHtml = html;
        if (!r.metrics.empty())
        {
            pluginHtml += "<table>";
            for (const auto &[k, v] : r.metrics)
                pluginHtml += QString("<tr><td>%1</td><td>%2</td></tr>")
                                  .arg(QString::fromStdString(k))
                                  .arg(v, 0, 'f', 4);
            pluginHtml += "</table>";
        }
        m_pluginResult->setText(pluginHtml);
    }
}

// Shared legacy ROI-fallback rendering. Shows the ROI stats on the Histogram
// page surface without replacing the base (full-image) stats that drive
// RGB/Exposure/Focus/Metadata.
void AnalysisPanel::renderRoiOutcome(const AnalysisResult &r)
{
    if (!m_hasA || !r.hasRoiStats)
        return;
    const ImageStats base = m_statsA;
    m_statsA = r.roiStats;
    updateHistogramPage();
    m_statsA = base;
}

// Explicit no-result note (analyzer produced nothing and no ROI fallback
// applies). No model publication — a no-result must never fabricate a
// successful result.
void AnalysisPanel::renderNoResult()
{
    const QString msg = tr("分析器未产生结果。");
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("无结果")).arg(msg));
    if (m_pluginResult)
        m_pluginResult->setText(msg);
    setReportAnalysisState(ReportAnalysisState::NoResult);
}

// Explicit queue-unavailable note for a REJECTED analyzer-only submission (the
// pool is paused / back-pressured). Terminates any pending state left by the
// superseded job with a retryable message and never fabricates a successful
// model result; the image and base pages stay loaded.
void AnalysisPanel::renderAnalysisUnavailable()
{
    const QString msg = tr("分析队列繁忙，请稍后重试。");
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("分析暂不可用")).arg(msg));
    if (m_pluginResult)
        m_pluginResult->setText(msg);
    setReportAnalysisState(ReportAnalysisState::Unavailable);
}

// Clear the single-frame analyzer result surface (stats label + Plugin tab) so
void AnalysisPanel::clearAnalyzerResultSurface()
{
    m_statsLabel->clear();
    if (m_pluginResult)
        m_pluginResult->clear();
}

// Clear localized pending state on the analyzer result surface while a manual
// job runs, so a previous result is never misrepresented as current. Every
// accepted terminal outcome replaces it.
void AnalysisPanel::showAnalysisPending()
{
    const QString msg = tr("分析中…");
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("分析中")).arg(msg));
    if (m_pluginResult)
        m_pluginResult->setText(msg);
    setReportAnalysisState(ReportAnalysisState::Pending);
}

void AnalysisPanel::setReportAnalysisState(ReportAnalysisState state,
                                            const QString &producerAnalyzerId)
{
    m_reportAnalysisState = state;
    m_reportAnalyzerId = state == ReportAnalysisState::Available ? producerAnalyzerId : QString();
    if (m_exportButton)
        m_exportButton->setEnabled(state != ReportAnalysisState::Pending);
}
