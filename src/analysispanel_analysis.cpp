#include "analysispanel.h"
#include "analyzermodel.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/compare/Aligner.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h"
#include <QPointer>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>

#include "core/image/QtConvert.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>

// ── Async analysis worker payload ─────────────────────────────────────────
// Everything the Analysis worker needs, snapshot BY VALUE on the UI thread.
// The worker only touches these captures (plus the TaskContext) — never the
// panel, model, widgets, registry, pipeline, or any QObject.
struct AnalysisInput
{
    std::shared_ptr<ImageFrame> frame;   // holds the pixel buffer alive
    std::string path;                    // publish path (setImage path wins, else frame metadata)
    bool hasROI = false;
    mviewer::domain::Selection roi;
    std::string id;                      // selected analyzer id (empty = none)
    uint64_t generation = 0;
    std::shared_ptr<Analyzer> analyzer;  // created on the UI thread, worker-owned
    bool creationFailed = false;         // pipeline/registry create() threw (M24 C#7)
    bool materialize = false;            // full frame materialization (vs analyzer-only rerun)
    QImage image; // already materialized RGB32, captured only for the legacy
                  // ROI fallback (analyzer-only rerun; implicit sharing)
};

// ── Worker output, marshalled to the UI thread via qApp ───────────────────
// Copyable by design so the queued delivery lambda can capture it by value.
// (Global scope: AnalysisPanel::applyAnalysisResult is declared with a
// forward-declared `struct AnalysisResult;` in the header.)
struct AnalysisResult
{
    uint64_t generation = 0;
    std::shared_ptr<ImageFrame> frame;   // identity check on the UI thread
    QImage image;                        // RGB32 (null = nothing to present)
    std::string path;
    std::string id;
    ImageStats stats;
    ImageStats roiStats;   // legacy ROI-fallback stats, kept separate from stats
    bool hasRoiStats = false;
    double noise = 0.0;
    bool noiseValid = false;

    bool materialize = false;            // mirrors the request mode
    bool isBuiltinCompare = false;
    bool analyzerOk = false;
    bool analyzerFailed = false;         // analyzer threw (M24 C#7 degrade)
    bool roiFallback = false;            // no analyzer result + ROI set (legacy fallback)
    bool noResult = false;               // no analyzer result and no ROI — explicit note
    std::string analyzerName;
    std::string resultText;
    std::unordered_map<std::string, double> metrics;
    mviewer::domain::Histogram histogram; // populated for HistogramAnalyzer
    bool hasHistogram = false;
    std::string plainResult;             // plain ROI summary to publish
};

namespace
{
// Run the whole analysis in one AnalysisPool worker job, with cancellation
// checks between the expensive stages. Qt is used here only for local image
// math (QImage/ImageData) exactly as the built-in analyzers do.
//
// A materializing request creates the RGB32 display copy and computes the
// full-image base stats + noise estimate exactly once. An analyzer-only
// request skips all of that — the image is already materialized and the base
// pages were already rendered when the frame was loaded — and only executes
// the selected analyzer (plus, on a no-result, the legacy ROI fallback over
// the shared materialized image).
AnalysisResult runAnalysis(const AnalysisInput &in, const TaskScheduler::TaskContext &ctx)
{
    AnalysisResult r;
    r.generation = in.generation;
    r.frame = in.frame;
    r.path = in.path;
    r.id = in.id;
    r.isBuiltinCompare = (in.id == "builtin_compare");
    r.materialize = in.materialize;

    // 1. Materializing request: create the RGB32 display copy and compute the
    //    full-image base stats + noise estimate (mirrors the old
    //    refreshFromFrame).
    ImageData data; // base stats input; valid only for materializing requests
    if (in.materialize)
    {
        QImage img = mvcore::toQImageRef(in.frame->pixels());
        if (img.isNull())
            img = mvcore::toQImage(in.frame->pixels()); // RGBA32 fallback
        img = img.convertToFormat(QImage::Format_RGB32);
        if (ctx.isCancelled())
            return r;
        if (img.isNull())
            return r; // nothing to present — the panel stays empty
        r.image = img;
        data = mvcore::fromQImage(img);
        r.stats = AnalysisEngine::computeStats(data);
        if (ctx.isCancelled())
            return r;
        r.noise = AnalysisEngine::noiseEstimate(data);
        r.noiseValid = true;
        if (ctx.isCancelled())
            return r;
    }

    // 2. builtin_compare: the one-frame path keeps the cheap 'Need two images'
    //    behavior (compare itself stays a legacy synchronous two-image path).
    if (r.isBuiltinCompare)
        return r;

    // 3. Analyzer creation failure on the UI thread is surfaced as an execution
    //    error (M24 C#7 parity), never silently degraded into the base-stats /
    //    ROI fallback below. Base stats above still reach the delivery, exactly
    //    like the legacy applyFrameImage -> reanalyze error path.
    if (in.creationFailed)
    {
        r.analyzerFailed = true;
        return r;
    }

    // 4. Execute the selected non-builtin analyzer over the frame (full or ROI,
    //    exactly matching the legacy reanalyze() semantics).
    if (in.analyzer)
    {
        try
        {
            const bool ok = in.hasROI ? in.analyzer->analyzeRegion(*in.frame, in.roi)
                                      : in.analyzer->analyze(*in.frame);
            if (ctx.isCancelled())
                return r;
            if (ok)
            {
                r.analyzerOk = true;
                r.analyzerName = in.analyzer->name();
                r.resultText = in.analyzer->resultText();
                r.metrics = in.analyzer->resultMetrics();
                const auto *hist = dynamic_cast<const HistogramAnalyzer *>(in.analyzer.get());
                if (hist)
                {
                    r.histogram = hist->result();
                    r.hasHistogram = true;
                }
                return r;
            }
        }
        catch (...)
        {
            if (ctx.isCancelled())
                return r;
            // M24 (C#7): a failing/throwing analyzer degrades to an error note
            // instead of taking down the panel or the application. The
            // localized message is built on the UI thread at delivery.
            r.analyzerFailed = true;
            return r;
        }
    }

    // 5. Legacy ROI fallback: no analyzer ran successfully and an ROI is set.
    //    stats stays the FULL-image stats (the delivery renders RGB/Exposure/
    //    base pages from it first); only roiStats feeds the Histogram page, so
    //    the fallback changes exactly the same surfaces as the legacy path.
    //    The ImageData source is the already materialized RGB32 (analyzer-only)
    //    or the freshly created one (materializing) — never a UI-thread scan.
    if (in.hasROI)
    {
        const ImageData roiData = in.materialize ? data : mvcore::fromQImage(in.image);
        r.roiFallback = true;
        r.roiStats = AnalysisEngine::computeStatsROI(roiData, in.roi);
        r.hasRoiStats = true;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "ROI %dx%d @(%d,%d) lum=%.2f r=%.2f g=%.2f b=%.2f",
                      in.roi.width, in.roi.height, in.roi.x, in.roi.y, r.roiStats.lumMean,
                      r.roiStats.rMean, r.roiStats.gMean, r.roiStats.bMean);
        r.plainResult = buf;
        return r;
    }

    // 6. No analyzer result and no ROI: the outcome is explicitly a no-result.
    //    The delivery shows a note without fabricating a model result.
    r.noResult = true;
    return r;
}

} // namespace

// Submit a snapshot to the Analysis pool and marshal the worker result back to
// the UI thread through qApp (outlives the panel). The queued lambda re-checks
// the guard, generation, and frame identity. Returns the accepted handle or
// nullptr when the pool rejects the submission (paused / back-pressured).
// Defined as a static member so the queued delivery can reach the private
// applyAnalysisResult.
TaskScheduler::TaskHandle
AnalysisPanel::submitAnalysisJob(const AnalysisInput &in, QPointer<AnalysisPanel> guard)
{
    return TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [in, guard](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return; // superseded while queued
            AnalysisResult r = runAnalysis(in, ctx);
            if (ctx.isCancelled())
                return; // superseded during an expensive stage
            QMetaObject::invokeMethod(
                qApp,
                [guard, r]()
                {
                    AnalysisPanel *panel = guard.data();
                    if (!panel)
                        return;
                    panel->applyAnalysisResult(r);
                },
                Qt::QueuedConnection);
        });
}

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

// Schedule exactly one latest-wins materializing AnalysisPool task for the
// current frame. Snapshot only plain/copyable data the worker needs; create any
// Analyzer on the UI thread (never touch the registry from the worker).
void AnalysisPanel::scheduleAnalysis()
{
    if (!m_frameA || !m_frameA->isValid())
        return;
    // Latest-wins: cancel any in-flight task before starting a new generation.
    if (m_task)
        TaskScheduler::cancel(m_task);
    m_task.reset();
    const uint64_t gen = ++m_gen;

    const std::shared_ptr<ImageFrame> frame = m_frameA; // holds pixels alive in the worker
    AnalysisInput in;
    in.frame = frame;
    in.path = frame->metadata().filePath;
    in.hasROI = m_hasROI;
    in.roi = m_roi;
    in.generation = gen;
    in.materialize = true;
    if (m_analyzerCombo)
        in.id = m_analyzerCombo->currentData().toString().toStdString();
    QPointer<AnalysisPanel> guard(this);

    // Create the selected analyzer on the UI thread, preserving its custom
    // AnalyzerDeleter for safe (plugin-module) destruction on the worker.
    if (!in.id.empty() && in.id != "builtin_compare")
    {
        try
        {
            auto created = m_pipeline ? m_pipeline->create(in.id)
                                      : AnalyzerRegistry::instance().create(in.id);
            if (created)
                in.analyzer = std::move(created); // shared_ptr adopts the deleter
        }
        catch (...)
        {
            // M24 (C#7): a throwing create() is carried into the worker result
            // and surfaced as an execution error at delivery — it must never
            // silently degrade into the base-stats / ROI fallback.
            in.creationFailed = true;
        }
    }

    auto handle = submitAnalysisJob(in, guard);
    if (!handle)
    {
        // Submission rejected (pool paused / back-pressured): stay dirty and
        // retryable — never fall back to synchronous work on the UI thread and
        // never present stale data.
        renderAnalysisUnavailable();
        return;
    }
    m_task = handle;
    setReportAnalysisState(ReportAnalysisState::Pending, QString::fromStdString(in.id));
}

// Schedule an analyzer-only rerun over the ALREADY materialized frame. The
// worker only executes the selected analyzer (and, on a no-result, the legacy
// ROI fallback over the shared materialized image) — it never re-materializes
// the full frame nor recomputes the base stats/noise. On an accepted
// submission the analyzer result surface shows a pending state; a rejected
// submission (pool paused / back-pressured) replaces any pending state with an
// explicit retryable unavailable message and never falls back to synchronous
// UI work. Returns true when a job was accepted.
bool AnalysisPanel::scheduleAnalyzerRun()
{
    if (!m_frameA || !m_frameA->isValid() || !m_hasA || m_imageA.isNull())
        return false;

    // Capture and validate the selected analyzer id BEFORE any cancellation or
    // generation bump: a defensive empty / builtin_compare id must not cancel a
    // valid current job or leave pending state. builtin_compare is handled by
    // reanalyze(), never here.
    const std::string id =
        m_analyzerCombo ? m_analyzerCombo->currentData().toString().toStdString() : std::string();
    if (id.empty() || id == "builtin_compare")
        return false;

    // Latest-wins: cancel any in-flight task before starting a new generation.
    if (m_task)
        TaskScheduler::cancel(m_task);
    m_task.reset();
    const uint64_t gen = ++m_gen;

    AnalysisInput in;
    in.frame = m_frameA;
    in.path = publishPath().toStdString();
    in.hasROI = m_hasROI;
    in.roi = m_roi;
    in.generation = gen;
    in.materialize = false;
    in.id = id;
    // The materialized image is captured (implicit sharing — a refcount bump,
    // not a pixel copy) ONLY for the legacy ROI fallback, and only when an ROI
    // is set: an analyzer-only request without ROI never reads it, so a stale
    // manual task holds no large RGB32 buffer while navigating. The ROI
    // conversion itself runs on the worker.
    if (m_hasROI)
        in.image = m_imageA;
    QPointer<AnalysisPanel> guard(this);

    // Create the selected analyzer on the UI thread (the registry/pipeline is
    // not synchronized), preserving its custom AnalyzerDeleter for safe
    // (plugin-module) destruction on the worker.
    try
    {
        auto created = m_pipeline ? m_pipeline->create(in.id)
                                  : AnalyzerRegistry::instance().create(in.id);
        if (created)
            in.analyzer = std::move(created);
    }
    catch (...)
    {
        // M24 (C#7): carried into the worker result as an execution error.
        in.creationFailed = true;
    }

    auto handle = submitAnalysisJob(in, guard);
    if (!handle)
    {
        // Rejected (pool paused / back-pressured): the superseded accepted job
        // was already cancelled and its generation bumped, so its pending state
        // must never survive. Terminate it with an explicit retryable
        // unavailable message; never run synchronously and never publish a
        // fabricated success.
        renderAnalysisUnavailable();
        return false;
    }
    m_task = handle;
    // A previous analyzer result must never be misrepresented as current while
    // the new job is pending.
    showAnalysisPending();
    return true;
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
