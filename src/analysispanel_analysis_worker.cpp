// ── Async analysis worker payload + job scheduling ────────────────────────
// M46: split from analysispanel_analysis.cpp so each TU stays under the
// complexity gate's 800-line file cap. Everything the Analysis worker needs is
// snapshot BY VALUE on the UI thread; the worker only touches its captures
// (plus the TaskContext) — never the panel, model, widgets, registry,
// pipeline, or any QObject.
#include "analysispanel.h"
#include "analysispanel_analysis_types.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/HistogramAnalyzer.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QPointer>

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

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
