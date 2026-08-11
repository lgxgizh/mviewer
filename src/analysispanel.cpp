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

// ── Automatic-frame analysis worker payload ────────────────────────────────
// Everything the Analysis worker needs, snapshot BY VALUE on the UI thread.
// The worker only touches these captures (plus the TaskContext) — never the
// panel, model, widgets, registry, pipeline, or any QObject.
struct AutoAnalysisInput
{
    std::shared_ptr<ImageFrame> frame;   // holds the pixel buffer alive
    std::string path;
    bool hasROI = false;
    mviewer::domain::Selection roi;
    std::string id;                      // selected analyzer id (empty = none)
    uint64_t generation = 0;
    std::shared_ptr<Analyzer> analyzer;  // created on the UI thread, worker-owned
    bool creationFailed = false;         // pipeline/registry create() threw (M24 C#7)
};

// ── Worker output, marshalled to the UI thread via qApp ───────────────────
// Copyable by design so the queued delivery lambda can capture it by value.
// (Global scope: AnalysisPanel::applyAutoAnalysisResult is declared with a
// forward-declared `struct AutoAnalysisResult;` in the header.)
struct AutoAnalysisResult
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

    bool isBuiltinCompare = false;
    bool analyzerOk = false;
    bool analyzerFailed = false;         // analyzer threw (M24 C#7 degrade)
    bool roiFallback = false;            // no analyzer result + ROI set (legacy fallback)
    std::string analyzerName;
    std::string resultText;
    std::unordered_map<std::string, double> metrics;
    mviewer::domain::Histogram histogram; // populated for HistogramAnalyzer
    bool hasHistogram = false;
    int pixelCount = 0;
    std::string plainResult;             // plain ROI summary to publish
};

namespace
{
// Run the whole automatic-frame analysis in one AnalysisPool worker job, with
// cancellation checks between the expensive stages. Qt is used here only for
// local image math (QImage/ImageData) exactly as the built-in analyzers do.
AutoAnalysisResult runAutoAnalysis(const AutoAnalysisInput &in,
                                   const TaskScheduler::TaskContext &ctx)
{
    AutoAnalysisResult r;
    r.generation = in.generation;
    r.frame = in.frame;
    r.path = in.path;
    r.id = in.id;
    r.isBuiltinCompare = (in.id == "builtin_compare");

    // 1. Materialize the RGB32 display copy (mirrors the old refreshFromFrame).
    QImage img = mvcore::toQImageRef(in.frame->pixels());
    if (img.isNull())
        img = mvcore::toQImage(in.frame->pixels()); // RGBA32 fallback
    img = img.convertToFormat(QImage::Format_RGB32);
    if (ctx.isCancelled())
        return r;
    if (img.isNull())
        return r; // nothing to present — the panel stays empty
    r.image = img;

    // 2. Base stats + noise estimate (full-image).
    const ImageData data = mvcore::fromQImage(img);
    r.stats = AnalysisEngine::computeStats(data);
    if (ctx.isCancelled())
        return r;
    r.noise = AnalysisEngine::noiseEstimate(data);
    r.noiseValid = true;
    if (ctx.isCancelled())
        return r;

    // 3. builtin_compare: the one-frame path keeps the cheap 'Need two images'
    //    behavior (compare itself stays a legacy synchronous two-image path).
    if (r.isBuiltinCompare)
        return r;

    // 3b. Analyzer creation failure on the UI thread is surfaced as an execution
    //     error (M24 C#7 parity), never silently degraded into the base-stats /
    //     ROI fallback below. Base stats above still reach the delivery, exactly
    //     like the legacy applyFrameImage -> reanalyze error path.
    if (in.creationFailed)
    {
        r.analyzerFailed = true;
        return r;
    }

    // 4. Execute the selected non-builtin analyzer over the frame (full or ROI,
    //    exactly matching the current reanalyze() semantics).
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
                r.pixelCount = in.hasROI
                                   ? std::max(0, in.roi.width) * std::max(0, in.roi.height)
                                   : in.frame->width() * in.frame->height();
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
    if (in.hasROI)
    {
        r.roiFallback = true;
        r.roiStats = AnalysisEngine::computeStatsROI(data, in.roi);
        r.hasRoiStats = true;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "ROI %dx%d @(%d,%d) lum=%.2f r=%.2f g=%.2f b=%.2f",
                      in.roi.width, in.roi.height, in.roi.x, in.roi.y, r.roiStats.lumMean,
                      r.roiStats.rMean, r.roiStats.gMean, r.roiStats.bMean);
        r.plainResult = buf;
    }
    return r;
}

} // namespace

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    setMinimumWidth(360);
    setMinimumHeight(480);
}

AnalysisPanel::~AnalysisPanel()
{
    // Cancel any in-flight automatic-frame task; its queued delivery is guarded
    // by a QPointer so it can never touch this panel after destruction.
    invalidateAutoAnalysis();
}

// A-7.2: rebuild the analyzer combo from the live registry/pipeline.
void AnalysisPanel::refreshAnalyzers()
{
    if (!m_analyzerCombo)
        return;
    const QString prev = m_analyzerCombo->currentData().toString();
    m_analyzerCombo->clear();
    m_pluginIds.clear();
    auto &reg = m_pipeline ? m_pipeline->registry() : AnalyzerRegistry::instance();
    m_pluginIds = reg.availableAnalyzers();
    for (const auto &id : m_pluginIds)
    {
        const auto info = reg.infoFor(id);
        const QString label =
            info ? QString::fromStdString(info->name) : QString::fromStdString(id);
        m_analyzerCombo->addItem(label, QString::fromStdString(id));
    }
    m_analyzerCombo->addItem(tr("Dual Compare (PSNR/SSIM)"), QString("builtin_compare"));
    // Restore previous selection if still present.
    const int idx = m_analyzerCombo->findData(prev);
    if (idx >= 0)
        m_analyzerCombo->setCurrentIndex(idx);
}

void AnalysisPanel::setImage(const QImage &img)
{
    setImage(img, QString());
}

void AnalysisPanel::setImage(const QImage &img, const QString &path)
{
    if (img.isNull())
    {
        clear();
        return;
    }
    // Explicit legacy API: cancel any automatic-frame task so a stale async
    // delivery can never overwrite this explicit image. There is no pending
    // frame left to auto-analyze either.
    invalidateAutoAnalysis();
    m_frameDirty = false;
    applyFrameImage(img.convertToFormat(QImage::Format_RGB32), path);
}

void AnalysisPanel::applyFrameImage(const QImage &rgb32, const QString &path)
{
    // M28 P1-02: caller already provides RGB32 — no second conversion here.
    m_imageA = rgb32;
    m_imagePath = path;
    m_hasA = true;
    m_hasB = false;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    // Cache the noise estimate so the cheap page updates never re-scan the
    // image (updateFocusPage reads this cache).
    m_noiseA = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));
    m_noiseValid = true;
    updateHistogramPage();
    updateRgbPage();
    updateExposurePage();
    updateFocusPage();
    updateMetadataPage();
}

void AnalysisPanel::setImages(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull())
        return;
    // Explicit legacy API: a stale automatic-frame delivery must never overwrite
    // the explicit compare state.
    invalidateAutoAnalysis();
    m_frameDirty = false;
    m_imageA = a.convertToFormat(QImage::Format_RGB32);
    m_imageB = b.convertToFormat(QImage::Format_RGB32);
    m_hasA = m_hasB = true;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    m_statsB = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageB));
    updateComparePage();
}

void AnalysisPanel::clear()
{
    invalidateAutoAnalysis();
    m_frameDirty = false;
    m_imageA = m_imageB = QImage();
    m_hasA = m_hasB = false;
    m_statsA = m_statsB = ImageStats();
    m_noiseA = 0.0;
    m_noiseValid = false;
    m_hasROI = false;
    m_statsLabel->clear();
    m_compareLabel->clear();
    m_diffPreview->clear();
    m_pluginResult->clear();
    m_histogramLabel->clear();
}

void AnalysisPanel::setROI(const mviewer::domain::Selection &roi)
{
    m_roi = roi;
    m_hasROI = !roi.isEmpty();
    reanalyze();
}

// A-7.1: select an analyzer by registry id without running it.
void AnalysisPanel::selectAnalyzer(const QString &id)
{
    if (id.isEmpty() || !m_analyzerCombo)
        return;
    refreshAnalyzers();
    const int idx = m_analyzerCombo->findData(id);
    if (idx < 0)
        return;
    const QSignalBlocker blocker(m_analyzerCombo);
    m_analyzerCombo->setCurrentIndex(idx);
    m_currentPluginIdx = idx;
}

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

// Run the currently-selected analyzer (from the pipeline) over the left frame and
// the active ROI, then render its result. The analyzer consumes a domain
// Selection, never a QRect. Creation/execution routes through the injected
// AnalyzerPipeline so the panel never touches the registry directly (M15 P0#3).
void AnalysisPanel::reanalyze()
{
    // A pending automatic-frame job means the current frame is not yet
    // materialized. Never invalidate it and then synchronously analyze an
    // unmaterialized frame; instead reschedule the automatic latest-wins job
    // with the current analyzer/ROI snapshot (visible) or stay deferred
    // (hidden). Once automatic materialization has landed (m_frameDirty false),
    // explicit reanalyze stays fully synchronous exactly as before.
    if (m_frameDirty && m_frameA && m_frameA->isValid())
    {
        if (isVisible())
            scheduleAutoAnalysis();
        return;
    }
    // Explicit path with a materialized frame: cancel/invalidate any automatic
    // task so a stale async delivery can never overwrite the explicit result.
    invalidateAutoAnalysis();

    const QString id = m_analyzerCombo ? m_analyzerCombo->currentData().toString() : QString();

    // Dual-image comparison is a built-in composite view, not a single registry analyzer.
    if (id == "builtin_compare")
    {
        updateComparePage();
        if (m_tabs && m_compareLabel)
        {
            const int tab = m_tabs->indexOf(m_compareLabel);
            if (tab >= 0)
                m_tabs->setCurrentIndex(tab);
        }
        return;
    }

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
                    return;
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
            return;
        }
    }

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
    }
}

void AnalysisPanel::setFrame(std::shared_ptr<ImageFrame> frame)
{
    // A new frame must immediately prevent the old result/image from being
    // presented as current: cancel the previous automatic task and bump the
    // generation so any in-flight delivery is discarded.
    invalidateAutoAnalysis();
    m_frameA = std::move(frame);
    if (!m_frameA || !m_frameA->isValid())
    {
        clear();
        return;
    }
    // M28 P1-02/P1-04: materialization + analysis are deferred AND async. A
    // HIDDEN panel (the common case while browsing) stores the frame only — no
    // full-size QImage conversion and no Analysis task until it is shown. A
    // visible panel schedules exactly one cancellable, latest-wins AnalysisPool
    // task on the next event-loop turn (rapid frames cancel the previous task).
    m_frameDirty = true;
    // While the newest automatic result is pending, the previous image/stats/
    // result must not be presented as current.
    if (m_hasA)
        resetImagePresentation();
    if (isVisible())
    {
        QTimer::singleShot(0, this,
                           [this]()
                           {
                               if (m_frameDirty)
                                   scheduleAutoAnalysis();
                           });
    }
}

void AnalysisPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Submits the deferred automatic-frame job; the worker runs all heavy work,
    // this only schedules (never blocks on conversion/stats/analyzer).
    if (m_frameDirty && m_frameA && m_frameA->isValid())
        scheduleAutoAnalysis();
}

void AnalysisPanel::invalidateAutoAnalysis()
{
    if (m_autoTask)
        TaskScheduler::cancel(m_autoTask);
    m_autoTask.reset();
    ++m_autoGen;
}

// Schedule exactly one latest-wins AnalysisPool task for the current frame.
// Snapshot only plain/copyable data the worker needs; create any Analyzer on
// the UI thread (never touch the registry from the worker).
void AnalysisPanel::scheduleAutoAnalysis()
{
    if (!m_frameA || !m_frameA->isValid())
        return;
    // Latest-wins: cancel any in-flight task before starting a new generation.
    if (m_autoTask)
        TaskScheduler::cancel(m_autoTask);
    m_autoTask.reset();
    const uint64_t gen = ++m_autoGen;

    const std::shared_ptr<ImageFrame> frame = m_frameA; // holds pixels alive in the worker
    AutoAnalysisInput in;
    in.frame = frame;
    in.path = frame->metadata().filePath;
    in.hasROI = m_hasROI;
    in.roi = m_roi;
    in.generation = gen;
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

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Analysis,
        [in, guard](const TaskScheduler::TaskContext &ctx)
        {
            if (ctx.isCancelled())
                return; // superseded while queued
            AutoAnalysisResult r = runAutoAnalysis(in, ctx);
            if (ctx.isCancelled())
                return; // superseded during an expensive stage
            // Marshal to the UI thread through qApp (outlives the panel). The
            // queued lambda re-checks the guard, generation, and frame identity.
            QMetaObject::invokeMethod(
                qApp,
                [guard, r]()
                {
                    AnalysisPanel *panel = guard.data();
                    if (!panel)
                        return;
                    panel->applyAutoAnalysisResult(r);
                },
                Qt::QueuedConnection);
        });
    if (!handle)
    {
        // Submission rejected (pool paused / back-pressured): stay dirty and
        // retryable — never fall back to synchronous work on the UI thread and
        // never present stale data.
        return;
    }
    m_autoTask = handle;
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

// Accept a worker-computed automatic-frame result on the UI thread. Only cheap
// UI rendering runs here; the heavy stages all happened in the worker.
void AnalysisPanel::applyAutoAnalysisResult(const AutoAnalysisResult &r)
{
    if (r.generation != m_autoGen)
        return; // superseded (latest-wins)
    if (m_frameA.get() != r.frame.get())
        return; // stale frame identity
    m_autoTask.reset(); // release the completed payload handle
    m_frameDirty = false;

    if (r.image.isNull())
        return; // nothing to present (e.g. materialization failed)

    m_imageA = r.image;
    m_imagePath = QString::fromStdString(r.path);
    m_hasA = true;
    m_hasB = false;
    m_statsA = r.stats; // full-image stats, mirroring applyFrameImage
    m_noiseA = r.noise;
    m_noiseValid = r.noiseValid;

    // Cheap page updates shared by every outcome, rendered from the full-image
    // stats (mirrors applyFrameImage before the old reanalyze). The base
    // Histogram page is populated for every accepted frame regardless of the
    // selected analyzer; analyzer-specific branches override it below.
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
        return;
    }

    if (r.analyzerFailed)
    {
        // M24 (C#7): analyzer failure -> graceful error note (same shape as the
        // synchronous reanalyze() error path).
        const QString id = QString::fromStdString(r.id);
        const QString err = tr("分析器执行失败（%1）。").arg(id);
        m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("分析失败")).arg(err));
        if (m_pluginResult)
            m_pluginResult->setText(err);
        publishResult(err);
        return;
    }

    if (r.analyzerOk)
    {
        // Same result surface as reanalyze(): Histogram stats + Plugin tab.
        m_statsA.pixelCount = r.pixelCount;
        if (r.hasHistogram)
        {
            m_statsA.lumMean = r.histogram.lumMean;
            m_statsA.rMean = r.histogram.rMean;
            m_statsA.gMean = r.histogram.gMean;
            m_statsA.bMean = r.histogram.bMean;
            renderHistogramPixmap(r.histogram);
        }
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
        // M21: publish exactly once for the current path.
        publishResult(text);
        return;
    }

    // No analyzer ran successfully: the shared base pages above already show
    // the full-image stats. The legacy ROI fallback changes only the same
    // Histogram/result surfaces as the old applyFrameImage -> reanalyze order.
    if (r.roiFallback)
    {
        if (r.hasRoiStats)
            m_statsA = r.roiStats;
        updateHistogramPage();
        publishResult(QString::fromStdString(r.plainResult));
    }
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>").arg(tr("Region Stats")).arg(text));
}

void AnalysisPanel::showPixel(int x, int y, int leftR, int leftG, int leftB, int leftA, int r16,
                              int g16, int b16, int rawKind, bool valid)
{
    if (m_frozen)
        return; // keep the last inspected pixel frozen
    m_px = x;
    m_py = y;
    m_pR = leftR;
    m_pG = leftG;
    m_pB = leftB;
    m_pA = leftA;
    m_r16 = r16;
    m_g16 = g16;
    m_b16 = b16;
    m_rawKind = rawKind;
    m_rawMax = (rawKind == 2) ? 65535 : 0;
    m_pValid = valid;
    // P0/P1 #⑥: draw a crosshair on the panel image at the inspected pixel so an
    // ISP engineer can screenshot the exact inspection point (Pixel Inspector).
    if (m_imageView)
    {
        if (valid)
            m_imageView->setCrosshair(QPointF(x + 0.5, y + 0.5));
        else
            m_imageView->clearCrosshair();
    }
    updateInspectorPage();
}

int AnalysisPanel::currentPage() const
{
    return m_tabs ? m_tabs->currentIndex() : 0;
}

void AnalysisPanel::setCurrentPage(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

void AnalysisPanel::onAnalyzerSelected(int index)
{
    m_currentPluginIdx = index;
    reanalyze();
}

void AnalysisPanel::updateHistogramPage()
{
    if (!m_hasA)
    {
        m_statsLabel->setText(tr("No image selected"));
        return;
    }
    QString title = m_hasROI ? tr("ROI Stats") : tr("Full Image Stats");
    QString txt = QString("<h3>%1</h3>").arg(title);
    txt += QString("<table>"
                   "<tr><td>%2</td><td>%3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "<tr><td>%10</td><td>%11</td></tr>"
                   "</table>")
               .arg(tr("Lum Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2)
               .arg(tr("R Mean"))
               .arg(m_statsA.rMean, 0, 'f', 2)
               .arg(tr("G Mean"))
               .arg(m_statsA.gMean, 0, 'f', 2)
               .arg(tr("B Mean"))
               .arg(m_statsA.bMean, 0, 'f', 2)
               .arg(tr("Pixels"))
               .arg(m_statsA.pixelCount);
    m_statsLabel->setText(txt);
    renderHistogramPixmap();
}

void AnalysisPanel::renderHistogramPixmap()
{
    if (!m_hasA)
        return;
    const int W = qMax(200, m_histogramLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);
    // Overlaid 4 channels
    auto drawChannel = [&bg, &p](const int *hist, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += hist[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };
    drawChannel(m_statsA.histLum, QColor(220, 220, 220));
    drawChannel(m_statsA.histR, QColor(230, 70, 70));
    drawChannel(m_statsA.histG, QColor(70, 220, 70));
    drawChannel(m_statsA.histB, QColor(70, 130, 230));
    m_histogramLabel->setPixmap(pix);
}

// P1-1: RGB channel page — separate R/G/B histograms + per-channel means.
void AnalysisPanel::updateRgbPage()
{
    if (!m_hasA)
    {
        m_rgbLabel->setText(tr("No image selected"));
        m_rgbStatsLabel->setText(QString());
        return;
    }
    QString txt = QString("<h3>%1</h3>").arg(tr("RGB Channels"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2</td></tr>"
                   "<tr><td>%3</td><td>%4</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("R Mean"))
               .arg(m_statsA.rMean, 0, 'f', 2)
               .arg(tr("G Mean"))
               .arg(m_statsA.gMean, 0, 'f', 2)
               .arg(tr("B Mean"))
               .arg(m_statsA.bMean, 0, 'f', 2);
    m_rgbStatsLabel->setText(txt);

    const int W = qMax(200, m_rgbLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);
    auto drawChannel = [&bg, &p](const int *hist, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += hist[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };
    drawChannel(m_statsA.histR, QColor(230, 70, 70));
    drawChannel(m_statsA.histG, QColor(70, 220, 70));
    drawChannel(m_statsA.histB, QColor(70, 130, 230));
    m_rgbLabel->setPixmap(pix);
}

void AnalysisPanel::updateExposurePage()
{
    if (!m_hasA)
    {
        m_exposureLabel->setText(tr("No image selected"));
        return;
    }
    long long highlights = 0, shadows = 0, total = 0;
    for (int i = 0; i < 256; ++i)
    {
        const long long v = m_statsA.histLum[i];
        total += v;
        if (i >= 240)
            highlights += v;
        if (i <= 15)
            shadows += v;
    }
    const double highlightPct = total ? 100.0 * highlights / total : 0.0;
    const double shadowPct = total ? 100.0 * shadows / total : 0.0;

    QString txt = QString("<h3>%1</h3>").arg(tr("Exposure"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2%</td></tr>"
                   "<tr><td>%3</td><td>%4%</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("Highlights (>=240)"))
               .arg(highlightPct, 0, 'f', 2)
               .arg(tr("Shadows (<=15)"))
               .arg(shadowPct, 0, 'f', 2)
               .arg(tr("Luminance Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2);
    m_exposureLabel->setText(txt);
}

void AnalysisPanel::updateFocusPage()
{
    if (!m_hasA)
    {
        m_focusLabel->setText(tr("No image selected"));
        return;
    }
    // M28 P1-04: noise is precomputed (async worker or legacy applyFrameImage)
    // and cached; updateFocusPage must not re-scan the image. The fallback
    // keeps the page correct for any path that did not populate the cache.
    const double noise =
        m_noiseValid ? m_noiseA : AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));

    QString txt = QString("<h3>%1</h3>").arg(tr("Focus / Sharpness"));
    txt += QString("<table>"
                   "<tr><td>%1</td><td>%2</td></tr>"
                   "<tr><td>%3</td><td>%4</td></tr>"
                   "<tr><td>%5</td><td>%6</td></tr>"
                   "</table>")
               .arg(tr("Luminance Mean"))
               .arg(m_statsA.lumMean, 0, 'f', 2)
               .arg(tr("Noise Estimate"))
               .arg(noiseLevelText(noise))
               .arg(tr("Pixel Count"))
               .arg(m_statsA.pixelCount);
    m_focusLabel->setText(txt);
}

void AnalysisPanel::updateComparePage()
{
    if (!m_hasA || !m_hasB)
    {
        m_compareLabel->setText(tr("Need two images to compare"));
        return;
    }
    QSettings s;
    const bool autoAlign = s.value("autoAlignBeforeDiff", false).toBool();

    ImageData a = mvcore::fromQImage(m_imageA);
    ImageData b = mvcore::fromQImage(m_imageB);
    QPoint offset(0, 0);
    bool aligned = false;
    if (autoAlign)
    {
        // F3 (M22): register B to A before diff so PSNR/SSIM reflect signal,
        // not mis-registration. Default off → no behavior change otherwise.
        mviewer::AlignOffset off = mviewer::Aligner::estimate(a, b, 32);
        offset = QPoint(off.x, off.y);
        if (off.x != 0 || off.y != 0)
        {
            b = mviewer::Aligner::shift(b, off.x, off.y);
            aligned = true;
        }
    }

    double psnr = AnalysisEngine::psnr(a, b);
    double ssim = AnalysisEngine::ssim(a, b);
    double noiseA = AnalysisEngine::noiseEstimate(a);
    double noiseB = AnalysisEngine::noiseEstimate(b);

    QString txt = QString("<h3>%1</h3>").arg(tr("Dual Compare"));
    txt += QString("<table>"
                   "<tr><td>%2</td><td>%3 dB</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "</table>")
               .arg(tr("PSNR"))
               .arg(psnr, 0, 'f', 2)
               .arg(tr("SSIM"))
               .arg(ssim, 0, 'f', 4)
               .arg(tr("Noise(A)"))
               .arg(noiseLevelText(noiseA))
               .arg(tr("Noise(B)"))
               .arg(noiseLevelText(noiseB));
    if (aligned)
        txt += QString("<p><b>%1</b> dx=%2 dy=%3</p>")
                   .arg(tr("Auto-aligned before diff"))
                   .arg(offset.x())
                   .arg(offset.y());
    m_compareLabel->setText(txt);

    QImage diff = computeDifferencePreview(m_imageA, mvcore::toQImage(b));
    if (!diff.isNull())
    {
        m_diffPreview->setPixmap(QPixmap::fromImage(diff).scaled(
            QSize(kPreviewSize, kPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void AnalysisPanel::updatePluginPage()
{
    if (m_pluginIds.empty())
    {
        m_pluginResult->setText(tr("No analyzer plugins available"));
        return;
    }
    int pluginIdx = m_currentPluginIdx - 2;
    if (pluginIdx < 0 || pluginIdx >= static_cast<int>(m_pluginIds.size()))
    {
        m_pluginResult->setText(tr("Select a plugin"));
        return;
    }
    const std::string &id = m_pluginIds[pluginIdx];
    auto analyzer = (m_pipeline ? m_pipeline->create(id) : AnalyzerRegistry::instance().create(id));
    if (!analyzer)
    {
        m_pluginResult->setText(tr("Cannot create: %1").arg(QString::fromStdString(id)));
        return;
    }
    QString txt = QString("<h3>%1</h3><p>%2</p>")
                      .arg(QString::fromStdString(analyzer->name()))
                      .arg(QString::fromStdString(analyzer->description()));
    m_pluginResult->setText(txt);
}

QImage AnalysisPanel::computeDifferencePreview(const QImage &a, const QImage &b)
{
    ImageData diff = AnalysisEngine::differenceMap(mvcore::fromQImage(a), mvcore::fromQImage(b));
    if (diff.isNull())
        return QImage();
    return mvcore::toQImage(diff);
}

QString AnalysisPanel::noiseLevelText(double variance)
{
    if (variance < 50)
        return tr("Very Low (%1)").arg(variance, 0, 'f', 1);
    if (variance < 150)
        return tr("Low (%1)").arg(variance, 0, 'f', 1);
    if (variance < 300)
        return tr("Medium (%1)").arg(variance, 0, 'f', 1);
    if (variance < 500)
        return tr("High (%1)").arg(variance, 0, 'f', 1);
    return tr("Very High (%1)").arg(variance, 0, 'f', 1);
}

void AnalysisPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    // Histogram viz rendered via QPixmap in renderHistogramPixmap()
}

void AnalysisPanel::updateImage(const QImage &img)
{
    if (m_imageView)
    {
        if (img.isNull())
            m_imageView->clear();
        else
            m_imageView->setImage(img.convertToFormat(QImage::Format_RGB32));
    }
}

void AnalysisPanel::updateHistogram(const mviewer::domain::Histogram &hist)
{
    renderHistogramPixmap(hist);
    m_statsLabel->setText(QString("<h3>%1</h3>"
                                  "<table>"
                                  "<tr><td>%2</td><td>%3</td></tr>"
                                  "<tr><td>%4</td><td>%5</td></tr>"
                                  "<tr><td>%6</td><td>%7</td></tr>"
                                  "<tr><td>%8</td><td>%9</td></tr>"
                                  "<tr><td>%10</td><td>%11</td></tr>"
                                  "</table>")
                              .arg(tr("Full Image Stats"))
                              .arg(tr("Lum Mean"))
                              .arg(hist.lumMean, 0, 'f', 2)
                              .arg(tr("R Mean"))
                              .arg(hist.rMean, 0, 'f', 2)
                              .arg(tr("G Mean"))
                              .arg(hist.gMean, 0, 'f', 2)
                              .arg(tr("B Mean"))
                              .arg(hist.bMean, 0, 'f', 2)
                              .arg(tr("Pixels"))
                              .arg(hist.totalPixels()));
}

void AnalysisPanel::renderHistogramPixmap(const mviewer::domain::Histogram &hist)
{
    if (!m_histogramLabel)
        return;
    const int W = qMax(200, m_histogramLabel->width() - 8);
    const int H = 160;
    QPixmap pix(W, H);
    pix.fill(QColor(20, 20, 20));
    QPainter p(&pix);
    const int pad = 4;
    const QRect bg(pad, pad, W - pad * 2, H - pad * 2);

    auto drawChannel = [&bg, &p](const int *histBins, const QColor &color)
    {
        constexpr int srcBins = 256;
        constexpr int drawBins = 64;
        const double binW = static_cast<double>(bg.width()) / drawBins;
        long long agg[drawBins] = {0};
        long long maxV = 1;
        for (int i = 0; i < drawBins; ++i)
        {
            long long sum = 0;
            const int lo = i * srcBins / drawBins;
            const int hi = (i + 1) * srcBins / drawBins;
            for (int j = lo; j < hi && j < srcBins; ++j)
                sum += histBins[j];
            agg[i] = sum;
            if (sum > maxV)
                maxV = sum;
        }
        p.setPen(color);
        for (int i = 0; i < drawBins; ++i)
        {
            const double h = static_cast<double>(agg[i]) / maxV * bg.height();
            const int x = bg.x() + static_cast<int>(i * binW);
            const int hh = qMax(1, static_cast<int>(h));
            p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
        }
    };

    drawChannel(hist.luminance.data(), QColor(220, 220, 220));
    drawChannel(hist.red.data(), QColor(230, 70, 70));
    drawChannel(hist.green.data(), QColor(70, 220, 70));
    drawChannel(hist.blue.data(), QColor(70, 130, 230));
    m_histogramLabel->setPixmap(pix);
}
