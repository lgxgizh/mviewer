// M24 Phase 4B — Compare workflow acceptance tests.
//
// Maps the M24 Workflow B acceptance items not covered elsewhere:
//   B#1  enter Compare with 2 / 4 / 8 images from a selection
//   B#2  layout / focus / reference-image clarity (n-up presets)
//   B#4  Blink/Swipe/Overlay/Diff mode switches preserve zoom + ROI state
//   B#6  continuous Next/Prev pair navigation keeps the user's mode
//   B#7  mismatched resolutions and corrupt images degrade cleanly
//   B#8  controls unavailable for the current layout explain why (tooltip)
//
// (2-image B/S/O/K mode semantics, Space, lock-reference, Esc exit and report
// bundles are already covered by workflow_ux_tests / compare_session_tests.)

#include "compareworkspace.h"
#include "core/compare/CompareEngine.h"
#include "core/perf/MemoryTracker.h"
#include "core/scheduler/TaskScheduler.h"
#include "selectionmodel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void pump(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

// M28 P1-01: Compare loads are asynchronous - pump until the engine reports the
// requested frame count (or the timeout expires). Deterministic waits replace
// bare pump() calls that could race the async delivery.
void waitForCompareCount(CompareWorkspace *ws, int expected, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (ws->comparedImageCount() != expected && t.elapsed() < timeoutMs)
        pump(25);
}

void waitForCompareAtLeast(CompareWorkspace *ws, int minimum, int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (ws->comparedImageCount() < minimum && t.elapsed() < timeoutMs)
        pump(25);
}

// P1 (async histogram contract): wait until a histogram widget carries at least
// one non-empty histogram. Polls widget state while pumping events so queued
// Analysis delivery reaches the UI; never uses fixed sleeps as correctness
// synchronization.
void waitForHistogramPopulated(CompareWorkspace *ws, const QString &objectName,
                               int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        HistogramWidget *hw = ws->findChild<HistogramWidget *>(objectName);
        if (hw && hw->histogramCount() > 0 && hw->histogramTotal(0) > 0)
            return;
        pump(25);
    }
}

// P1 (async histogram contract): wait until a histogram widget's first entry
// reports exactly the expected pixel total (e.g. an ROI-restricted 32x32).
void waitForHistogramTotal(CompareWorkspace *ws, const QString &objectName, long expectedTotal,
                           int timeoutMs = 15000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        HistogramWidget *hw = ws->findChild<HistogramWidget *>(objectName);
        if (hw && hw->histogramCount() > 0 && hw->histogramTotal(0) == expectedTotal)
            return;
        pump(25);
    }
}

QString writePng(const QDir &dir, const QString &name, int w, int h, QColor c)
{
    const QString path = dir.filePath(name);
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    img.save(path, "PNG");
    return path;
}

QCheckBox *findChk(QWidget *root, const QString &textPrefix)
{
    const auto boxes = root->findChildren<QCheckBox *>();
    for (QCheckBox *c : boxes)
        if (c->text().startsWith(textPrefix))
            return c;
    return nullptr;
}

// ─── B#1/B#2/B#8: 4-image and 8-image entry + layout-aware enablement ───────
void testMultiImageEntry(const QStringList &paths8)
{
    std::cout << "── Compare B#1/B#2/B#8: 4/8-image entry + controls ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();
    pump(50);

    // 4-up from a 6-image selection.
    ws->setImagePool(paths8.mid(0, 6));
    ws->applyLayoutPreset(4);
    waitForCompareCount(ws, 4);
    CHECK(ws->comparedImageCount() == 4, "B#1: 4-image compare entry loads 4 images");

    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *swipe = findChk(ws, QStringLiteral("滑动"));
    CHECK(split && overlay, "B#8: split/overlay controls exist");
    if (split && overlay)
    {
        CHECK(!split->isEnabled() && !overlay->isEnabled(),
              "B#8: split/overlay disabled for 4 images");
        const bool hasExplain = !split->toolTip().isEmpty() && split->toolTip().contains("2");
        CHECK(hasExplain, "B#8: disabled split control explains its 2-image requirement");
    }

    // 8-up (4×2) from the full selection.
    ws->setImagePool(paths8);
    ws->applyLayoutPreset(8);
    waitForCompareCount(ws, 8);
    CHECK(ws->comparedImageCount() == 8, "B#1: 8-image compare entry loads 8 images");
    CHECK(ws->engine().layout().cols >= 2 && ws->engine().layout().rows >= 2,
          "B#2: 8-up produces a multi-row, multi-column grid");

    // Reference/focus must stay well-defined at any count.
    CHECK(sel.focused().isEmpty() || !ws->focusImagePath().isEmpty(),
          "B#2: focus image remains defined after layout switches");

    // 2-up back.
    ws->applyLayoutPreset(2);
    waitForCompareCount(ws, 2);
    CHECK(ws->comparedImageCount() == 2, "B#1: back to 2-image compare");
    if (split && overlay)
    {
        CHECK(split->isEnabled() && overlay->isEnabled(),
              "B#8: split/overlay re-enabled for exactly 2 images");
    }
}

// ─── B#4: mode switches preserve zoom + ROI ─────────────────────────────────
void testModePreservesState(const QString &a, const QString &b)
{
    std::cout << "── Compare B#4: mode switches preserve zoom/ROI ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareCount(ws, 2);

    // Establish a non-trivial zoom + a ROI.
    const double zoom = 2.5;
    ws->engine().setScale(zoom);
    ws->applyROI({40, 30, 200, 120});
    pump(30);

    QCheckBox *blink = findChk(ws, QStringLiteral("闪烁对比"));
    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    QCheckBox *split = findChk(ws, QStringLiteral("左右分割"));
    CHECK(blink && overlay && split, "B#4: blink/overlay/split controls present");
    if (!blink || !overlay || !split)
        return;

    // Cycle through every exclusive mode and back; zoom/ROI must survive.
    // (Blink is an independent axis — not part of the split/overlay family.)
    for (int i = 0; i < 3; ++i)
    {
        split->setChecked(true);
        pump(20);
        overlay->setChecked(true); // auto-unchecks split
        pump(20);
        overlay->setChecked(false);
        pump(20);
    }
    CHECK(qAbs(ws->engine().syncTransform().scale - zoom) < 1e-9,
          "B#4: zoom survives blink/overlay mode cycling");
    const auto roi = ws->currentROI();
    CHECK(roi.width == 200 && roi.height == 120 && roi.x == 40 && roi.y == 30,
          "B#4: ROI survives mode switches");
    CHECK(!split->isChecked() && !overlay->isChecked(),
          "B#4: no exclusive mode left stuck on after the cycle");

    // Diff-highlight toggling also preserves state.
    QCheckBox *diffHl = findChk(ws, QStringLiteral("差异高亮"));
    if (diffHl)
    {
        diffHl->setChecked(true);
        pump(30);
        diffHl->setChecked(false);
        pump(30);
        CHECK(qAbs(ws->engine().syncTransform().scale - zoom) < 1e-9,
              "B#4: zoom survives diff-highlight toggling");
    }
}

// ─── B#6: continuous pair navigation preserves mode ─────────────────────────
void testContinuousNav(const QStringList &paths6)
{
    std::cout << "── Compare B#6: continuous pair navigation ──\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImagePool(paths6);
    ws->setNavWindow(2);
    dlg.resize(1100, 750);
    dlg.show();
    pump(80);

    QCheckBox *overlay = findChk(ws, QStringLiteral("叠加对比"));
    CHECK(overlay != nullptr, "B#6: overlay control present");
    if (!overlay)
        return;
    overlay->setChecked(true);
    pump(30);

    // Walk forward through the pool; the overlay mode must persist.
    int pairs = 0;
    while (ws->hasNextPair())
    {
        ws->nextPair();
        waitForCompareCount(ws, 2);
        ++pairs;
        if (!overlay->isChecked())
            break;
    }
    CHECK(pairs >= 2, "B#6: next-pair walks at least two pairs");
    CHECK(overlay->isChecked(), "B#6: overlay mode persists across pair navigation");

    // Walk backward; mode + focus stay coherent.
    int back = 0;
    while (ws->hasPrevPair() && back < 4)
    {
        ws->prevPair();
        waitForCompareCount(ws, 2);
        ++back;
    }
    CHECK(back >= 2, "B#6: prev-pair walks back");
    CHECK(overlay->isChecked(), "B#6: mode persists walking backward");
    CHECK(!ws->focusImagePath().isEmpty(), "B#6: focus image defined after navigation");
}

// P1: pane histogram consistency across ROI, rebuilds, and Blink.
void testPaneHistogramConsistency(const QString &a, const QString &b)
{
    std::cout << "--- Compare P1: pane histogram consistency ---\n";
    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareCount(ws, 2);

    auto *perPane = ws->findChild<QCheckBox *>("perPaneHistogramToggle");
    auto *overlay = ws->findChild<QCheckBox *>("paneHistogramOverlayToggle");
    auto *roi = ws->findChild<QCheckBox *>("roiHistogramToggle");
    auto *blink = findChk(ws, QStringLiteral("闪烁对比"));
    CHECK(perPane && overlay && roi && blink, "P1: histogram and blink controls are discoverable");
    if (!perPane || !overlay || !roi || !blink)
        return;

    // P1 async histogram contract: histogram delivery is an Analysis batch,
    // never a synchronous compute on the UI thread. Drain whatever the load
    // generation queued so the release-gated blocker below is the only work in
    // front of the histogram submissions.
    auto &sched = TaskScheduler::instance();
    sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(10));
    pump(50);

    auto paneHist = [ws](int index)
    { return ws->findChild<HistogramWidget *>(QString("paneHistogram%1").arg(index)); };
    HistogramWidget *hist0 = paneHist(0);
    HistogramWidget *hist1 = paneHist(1);
    CHECK(hist0 && hist1, "P1: both pane histogram widgets exist");
    if (!hist0 || !hist1)
        return;

    // perPane populates the main analysis histogram asynchronously. The pane
    // overlays must stay empty while their own toggle is off.
    perPane->setChecked(true);
    waitForHistogramPopulated(ws, QStringLiteral("analysisHistogram"));
    CHECK(hist0->histogramCount() == 0 && hist1->histogramCount() == 0,
          "P1: pane overlays stay empty while the overlay toggle is off");

    // Occupy the single Analysis worker with a release-gated blocker (the M29
    // gate pattern from test_workflow_ux.cpp) so the overlay toggle's one
    // histogram batch stays observable instead of racing the UI thread.
    sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
    struct RestoreAnalysisThreads
    {
        ~RestoreAnalysisThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Analysis, std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreAnalysis;

    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool gateReleased = false;
    auto blocker = sched.submit(
        TaskScheduler::Priority::Analysis,
        [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
        {
            std::unique_lock<std::mutex> lk(gateMtx);
            gateCv.wait(lk, [&gateReleased] { return gateReleased; });
        });
    struct ReleaseGateGuard
    {
        std::mutex &mtx;
        std::condition_variable &cv;
        bool &released;
        ~ReleaseGateGuard()
        {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
            cv.notify_all();
        }
    } releaseGate{gateMtx, gateCv, gateReleased};
    {
        QElapsedTimer wt;
        wt.start();
        while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
               wt.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
          "P1: release-gated blocker occupies the Analysis worker");
    const uint64_t submittedBefore =
        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;

    // The overlay toggle submits exactly ONE Analysis batch for all pane
    // histograms; while the worker is blocked the panes stay empty because the
    // delivery is asynchronous, never a synchronous UI-thread refresh.
    overlay->setChecked(true);
    const uint64_t submittedAfter =
        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
    CHECK(submittedAfter == submittedBefore + 1,
          "P1: overlay toggle submits exactly one Analysis histogram batch");
    CHECK(hist0->histogramCount() == 0 && hist1->histogramCount() == 0,
          "P1: pane histograms stay empty while the Analysis worker is blocked");

    // Release the gate and drain: both pane overlays must be delivered.
    {
        std::lock_guard<std::mutex> lk(gateMtx);
        gateReleased = true;
    }
    gateCv.notify_all();
    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(15)),
          "P1: Analysis pool drains after the histogram gate release");
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
              hist1->histogramCount() == 1 && hist1->histogramTotal(0) > 0,
          "P1: independent pane overlays are populated");

    blink->setChecked(true);
    pump(30);
    auto *pane0 = ws->findChild<QWidget *>("comparePane0");
    auto *pane1 = ws->findChild<QWidget *>("comparePane1");
    auto *frame0 = ws->findChild<QWidget *>("paneHistogramFrame0");
    auto *frame1 = ws->findChild<QWidget *>("paneHistogramFrame1");
    auto *caption0 = ws->findChild<QLabel *>("paneCaption0");
    auto *caption1 = ws->findChild<QLabel *>("paneCaption1");
    CHECK(pane0 && pane1 && pane0->isVisible() && !pane1->isVisible(),
          "P1: Blink shows only the active pane container");
    CHECK(frame0 && frame1 && frame0->isVisible() && !frame1->isVisible(),
          "P1: Blink hides the inactive pane histogram frame");
    CHECK(caption0 && caption1 && caption0->isVisible() && !caption1->isVisible(),
          "P1: Blink hides the inactive pane caption");

    // Exercise a timer tick too: exactly one whole pane remains visible when the
    // active image flips, and its overlay/caption visibility follows the pane.
    pump(170);
    CHECK(pane0->isVisible() != pane1->isVisible(),
          "P1: repeated Blink ticks keep exactly one pane container visible");
    CHECK(frame0->isVisible() == pane0->isVisible() && frame1->isVisible() == pane1->isVisible(),
          "P1: pane histogram frames follow the active Blink pane");
    CHECK(caption0->isVisible() == pane0->isVisible() &&
              caption1->isVisible() == pane1->isVisible(),
          "P1: pane captions follow the active Blink pane");

    // A rebuild while Blink has detached one pane must delete both old panes,
    // preserve Blink visibility, and populate the new overlay widgets.
    ws->setImages({a, b});
    waitForCompareCount(ws, 2);
    const auto panes0 = ws->findChildren<QWidget *>("comparePane0");
    const auto panes1 = ws->findChildren<QWidget *>("comparePane1");
    CHECK(panes0.size() == 1 && panes1.size() == 1,
          "P1: Blink-active rebuild leaves no stale pane containers");
    CHECK(!panes0.isEmpty() && !panes1.isEmpty() &&
              panes0.front()->isVisible() != panes1.front()->isVisible(),
          "P1: Blink-active rebuild retains one active pane");
    CHECK(ws->engine().blinkController().isBlinking(),
          "P1: Blink-active rebuild restores engine Blink state");
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramTotal(0) > 0 && hist1->histogramTotal(0) > 0,
          "P1: Blink-active rebuild repopulates pane histograms");

    blink->setChecked(false);
    pump(50);
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram0"));
    waitForHistogramPopulated(ws, QStringLiteral("paneHistogram1"));
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
              hist1->histogramCount() == 1 && hist1->histogramTotal(0) > 0,
          "P1: Blink stop rebuild retains populated pane histograms");

    roi->setChecked(true);
    ws->applyROI({0, 0, 32, 32});
    waitForHistogramTotal(ws, QStringLiteral("analysisHistogram"), 1024);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram0"), 1024);
    waitForHistogramTotal(ws, QStringLiteral("paneHistogram1"), 1024);
    auto *mainHist = ws->findChild<HistogramWidget *>("analysisHistogram");
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(mainHist && mainHist->histogramCount() > 0 && mainHist->histogramTotal(0) == 1024,
          "P1: main histogram uses the 32x32 ROI total");
    CHECK(hist0 && hist1 && hist0->histogramTotal(0) == 1024 && hist1->histogramTotal(0) == 1024,
          "P1: pane histograms use the same 32x32 ROI total");
}

// B#7: mismatched resolutions and corrupt image handling.
void testDegradedImages(const QDir &dir)
{
    std::cout << "── Compare B#7: mismatched + corrupt images ──\n";
    const QString small = writePng(dir, "cmp_small.png", 64, 48, QColor(10, 10, 10));
    const QString large = writePng(dir, "cmp_large.png", 1024, 768, QColor(20, 20, 20));
    const QString portrait = writePng(dir, "cmp_portrait.png", 48, 64, QColor(30, 30, 30));
    const QString corrupt = dir.filePath("cmp_bad.png");
    QFile f(corrupt);
    const bool opened = f.open(QIODevice::WriteOnly);
    Q_UNUSED(opened); // C4834: QIODevice::open is [[nodiscard]] on Qt 6.10 headers
    const qint64 written = f.write("\x89PNG\r\n\x1a\n definitely not a png", 32);
    Q_UNUSED(written); // C4834: QIODevice::write is [[nodiscard]] on Qt 6.10 headers
    f.close();

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    QString warning;
    QObject::connect(ws, &CompareWorkspace::loadWarning, &dlg,
                     [&warning](const QString &text) { warning = text; });
    ws->setImages({small, large, portrait, corrupt});
    dlg.resize(1100, 750);
    dlg.show();
    waitForCompareCount(ws, 3);

    CHECK(ws->comparedImageCount() == 3,
          "B#7: corrupt image is excluded from compare without crashing");
    CHECK(warning.contains("无法加载") || warning.contains("无法加载"),
          "B#7: the load failure is surfaced to the user (loadWarning)");
    ws->engine().setScale(2.0);
    pump(30);
    CHECK(ws->engine().syncTransform().scale > 0, "B#7: zooming mixed sizes is functional");

    // A diff between mismatched sizes must report a sane result, not crash.
    ws->applyROI({0, 0, 32, 32});
    pump(60);

    // Next/prev navigation across the mixed pool stays safe.
    ws->setImagePool({small, large, portrait, corrupt});
    ws->setNavWindow(2);
    ws->nextPair();
    waitForCompareAtLeast(ws, 1);
    ws->nextPair();
    waitForCompareAtLeast(ws, 1);
    CHECK(ws->comparedImageCount() >= 1, "B#7: navigation over degraded pool keeps a grid");
    (void)corrupt;
}

// --- M28 P1-01: Compare open must NOT decode on the UI thread ---------------
// Regression: CompareWorkspace::setImages() used to call
// CompareEngine::setImages() -> ImageRepository::load() synchronously, so
// opening Compare froze the UI for every decoded frame. The async contract:
//   * setImages() returns immediately (submits decode work to the pool);
//   * no frames are applied synchronously (count == 0 right after the call);
//   * frames arrive asynchronously and the engine ends up fully populated.
//
// Contract: the panes themselves materialize asynchronously too.
// rebuildCells() submits ONE AnalysisPool display-materialization batch per
// load generation, alongside the existing diff-analysis batch. While Analysis
// work is deliberately blocked the decoded panes stay blank, and each load
// generation submits exactly those two Analysis batches.
void testCompareLoadIsAsync(const QDir &dir)
{
    std::cout << "-- Compare M28 P1-01: async load (no UI-thread decode) --\n";
    // A 6000x4000 PNG takes far longer than 250ms to decode on CI hardware;
    // the synchronous implementation froze the UI for that long (or worse).
    const QString big = writePng(dir, "cmp_async_big.png", 6000, 4000, QColor(200, 120, 40));
    const QString small = writePng(dir, "cmp_async_small.png", 8, 8, QColor(40, 120, 200));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1100, 750);
    dlg.show();

    // ── P1-01: pane materialization is asynchronous. Occupy the single
    // Analysis worker with a release-gated blocker (the M29 gate pattern from
    // test_workflow_ux.cpp) so every Analysis submission of this load queues
    // and stays observable instead of racing the UI thread. Drain first so the
    // blocker deterministically is the only task in front of this load.
    auto &sched = TaskScheduler::instance();
    sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(10));
    sched.setQueueMaxThreads(TaskScheduler::Priority::Analysis, 1);
    struct RestoreAnalysisThreads
    {
        ~RestoreAnalysisThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Analysis, std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreAnalysis;

    std::mutex gateMtx;
    std::condition_variable gateCv;
    bool gateReleased = false;
    auto blocker = sched.submit(
        TaskScheduler::Priority::Analysis,
        [&gateMtx, &gateCv, &gateReleased](const TaskScheduler::TaskContext &)
        {
            std::unique_lock<std::mutex> lk(gateMtx);
            gateCv.wait(lk, [&gateReleased] { return gateReleased; });
        });
    struct ReleaseGateGuard
    {
        std::mutex &mtx;
        std::condition_variable &cv;
        bool &released;
        ~ReleaseGateGuard()
        {
            std::lock_guard<std::mutex> lk(mtx);
            released = true;
            cv.notify_all();
        }
    } releaseGate{gateMtx, gateCv, gateReleased};
    {
        QElapsedTimer wt;
        wt.start();
        while (sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks < 1 &&
               wt.elapsed() < 5000)
            pump(10);
    }
    CHECK(sched.metrics(TaskScheduler::PoolType::AnalysisPool).active_tasks >= 1,
          "P1-01: release-gated blocker occupies the Analysis worker");
    const uint64_t submittedBefore =
        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;

    QElapsedTimer t;
    t.start();
    ws->setImages({big, small});
    const qint64 setupMs = t.elapsed();
    CHECK(setupMs < 250, "setImages returns without decoding on the UI thread");
    CHECK(ws->comparedImageCount() == 0,
          "frames are delivered asynchronously, never synchronously applied");

    waitForCompareCount(ws, 2);
    CHECK(ws->comparedImageCount() == 2, "async compare load delivers all frames");

    // Decode has reached the UI (finishLoad -> rebuildCells ran on the UI
    // thread) but the Analysis worker is still gate-blocked: both panes must
    // be blank, because their materialization is an Analysis batch, never a
    // synchronous ImageData -> QImage conversion inside rebuildCells().
    RawImageView *view0 = nullptr;
    RawImageView *view1 = nullptr;
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (!v)
            continue;
        if (v->cellIndex() == 0)
            view0 = v;
        else if (v->cellIndex() == 1)
            view1 = v;
    }
    CHECK(view0 && view1, "P1-01: two decoded panes exist after the async load");
    if (view0 && view1)
    {
        CHECK(view0->image().isNull() && view1->image().isNull(),
              "P1-01: decoded panes stay blank while Analysis materialization is blocked");
    }

    // Exactly two Analysis submissions for this load generation: the future
    // display-materialization batch plus the existing diff-analysis batch.
    const uint64_t submittedAfter =
        sched.metrics(TaskScheduler::PoolType::AnalysisPool).submitted;
    CHECK(submittedAfter == submittedBefore + 2,
          "P1-01: load generation submits one display batch + one diff batch");

    // Release the gate and drain: both panes must materialize.
    {
        std::lock_guard<std::mutex> lk(gateMtx);
        gateReleased = true;
    }
    gateCv.notify_all();
    CHECK(sched.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(15)),
          "P1-01: Analysis pool drains after the gate release");
    {
        QElapsedTimer wt;
        wt.start();
        while ((!view0 || view0->image().isNull() || !view1 || view1->image().isNull()) &&
               wt.elapsed() < 5000)
            pump(25);
    }
    CHECK(view0 && view1 && !view0->image().isNull() && !view1->image().isNull(),
          "P1-01: panes materialize once Analysis work drains");
    dlg.close();
}

// M42 P0/P1: real Compare display materialization must be viewport/LOD bounded.
// The source frames remain full resolution for diff/metrics, but a Fit-state
// pane must not retain a second QImage at the source dimensions.
void testLargeCompareDisplayBounded(const QDir &dir)
{
    std::cout << "-- Compare M42: 8-image display memory bounded --\n";
    QStringList largePaths;
    for (int i = 0; i < 8; ++i)
        largePaths << writePng(dir, QString("m42_large_%1.png").arg(i), 2400, 1600,
                               QColor(20 + i * 20, 40 + i * 10, 180 - i * 10));

    SelectionModel sel;
    QDialog dlg;
    auto *lay = new QVBoxLayout(&dlg);
    auto *ws = new CompareWorkspace(&dlg);
    lay->addWidget(ws);
    ws->setSelectionModel(&sel);
    dlg.resize(1200, 800);
    dlg.show();
    const auto rssBefore = mviewer::perf::MemoryTracker::instance().sample();
    ws->setImages(largePaths);
    waitForCompareCount(ws, 8, 30000);

    QElapsedTimer paneTimer;
    paneTimer.start();
    QList<RawImageView *> panes;
    while (paneTimer.elapsed() < 30000)
    {
        panes = ws->findChildren<RawImageView *>();
        if (panes.size() >= 8)
        {
            bool ready = true;
            for (RawImageView *pane : panes)
                ready = ready && pane && !pane->image().isNull() && pane->image().width() < 2400 &&
                        pane->image().height() < 1600;
            if (ready)
                break;
        }
        pump(25);
    }
    CHECK(ws->comparedImageCount() == 8 && panes.size() >= 8,
          "M42: 8-image Compare reaches the real pane path");

    size_t sourceBytes = 0;
    for (int i = 0; i < ws->engine().imageCount(); ++i)
    {
        const ImageFrame *frame = ws->engine().imageAt(i);
        if (frame)
            sourceBytes += frame->pixels().byteSize();
    }
    size_t displayBytes = 0;
    bool everyPaneIsLod = true;
    for (RawImageView *pane : panes)
    {
        if (!pane)
            continue;
        displayBytes += static_cast<size_t>(pane->image().sizeInBytes());
        everyPaneIsLod = everyPaneIsLod && pane->image().width() < 2400 &&
                         pane->image().height() < 1600;
    }
    CHECK(everyPaneIsLod, "M42: Fit panes use a display LOD below source dimensions");
    CHECK(sourceBytes > 0 && displayBytes * 2 < sourceBytes,
          "M42: 8-pane display bytes are materially below a second full-resolution copy");
    const auto rss = mviewer::perf::MemoryTracker::instance().sample();
    std::cout << "   source bytes=" << sourceBytes << " display bytes=" << displayBytes
              << " working-set KB before=" << rssBefore.processWorkingSetKB
              << " after=" << rss.processWorkingSetKB << "\n";

    // Exercise the real continuation path: zoom/pan, explicit diff, then close.
    if (!panes.isEmpty() && panes.first())
        panes.first()->zoom(2.0, QPointF(panes.first()->width() / 2.0,
                                         panes.first()->height() / 2.0));
    if (QCheckBox *diff = findChk(ws, QStringLiteral("显示差异")))
        diff->setChecked(true);
    pump(100);
    dlg.close();
    auto &scheduler = TaskScheduler::instance();
    CHECK(scheduler.drain(TaskScheduler::PoolType::AnalysisPool, std::chrono::seconds(20)),
          "M42: Compare zoom/diff/exit drains Analysis work");
    const auto analysisMetrics = scheduler.metrics(TaskScheduler::PoolType::AnalysisPool);
    const auto graphMetrics = scheduler.graphMetrics();
    CHECK(analysisMetrics.pending == 0 && analysisMetrics.active_tasks == 0 &&
              analysisMetrics.queue_depth == 0,
          "M42: Compare Analysis pending/active/queue counters converge to zero");
    CHECK(graphMetrics.handles == 0 && graphMetrics.deferred == 0 &&
              graphMetrics.dep_graph_entries == 0 && graphMetrics.dependents_entries == 0,
          "M42: Compare scheduler handles and dependency graph converge to zero");
}

// ─── M30: Pixel Inspector hover coalescing ──────────────────────
// The Compare inspector table is fed by up to two hover signals per mouse move
// when the synced crosshair is enabled (RawImageView::pixelInfo plus
// crosshairMoved), and every hover used to re-render the whole table by
// destroying and reallocating its QTableWidgetItems. The coalescer contract:
//   (a) a burst of hovers inside one event-loop turn renders exactly once,
//       with the latest coordinate;
//   (b) the pixelInfo + crosshairMoved pair of one physical hover renders
//       once, never twice;
//   (c) ordinary re-renders reuse the existing QTableWidgetItem objects;
//   (d) color-space / kernel / focus semantic changes refresh the current
//       coordinate correctly;
//   (e) a queued render is receiver-bound to the workspace, so destroying the
//       workspace with work pending is safe.
// The actual render count is surfaced as the dynamic QObject property
// "inspectorRenderCount" (narrowly scoped internal diagnostic, documented in
// compareworkspace.h) so the tests observe coalescing without widening the
// public API.
// M40: a superseded Compare batch must cancel queued repository requests and
// still converge its completion bookkeeping. This uses a release gate instead
// of wall-clock ordering so A -> B and scheduler rejection are deterministic.
void testCompareLoadCancellation(const QDir &dir)
{
    std::cout << "-- Compare M40: cancellable load batches --\n";
    const QString a = writePng(dir, "m40_cancel_a.png", 120, 80, QColor(220, 40, 40));
    const QString b = writePng(dir, "m40_cancel_b.png", 120, 80, QColor(40, 220, 40));
    const QString c = writePng(dir, "m40_cancel_c.png", 120, 80, QColor(40, 40, 220));
    const QString d = writePng(dir, "m40_cancel_d.png", 120, 80, QColor(220, 220, 40));

    auto &scheduler = TaskScheduler::instance();
    scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(10));
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    struct RestoreDecodeThreads
    {
        ~RestoreDecodeThreads()
        {
            TaskScheduler::instance().setQueueMaxThreads(
                TaskScheduler::Priority::Decode,
                std::max(1, QThread::idealThreadCount() / 2));
        }
    } restoreThreads;

    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool released = false;
    auto blocker = scheduler.submit(
        TaskScheduler::Priority::Decode,
        [&gateMutex, &gateCv, &released](const TaskScheduler::TaskContext &)
        {
            std::unique_lock<std::mutex> lk(gateMutex);
            gateCv.wait(lk, [&released] { return released; });
        });
    while (scheduler.activeTaskCount(TaskScheduler::DecodePool) < 1)
        pump(10);

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    host.show();
    ws->setImages({a, b});
    ws->setImages({c, d});

    {
        std::lock_guard<std::mutex> lk(gateMutex);
        released = true;
    }
    gateCv.notify_all();
    CHECK(scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(15)),
          "M40: gated DecodePool drains after superseding a batch");
    QElapsedTimer wait;
    wait.start();
    while (ws->comparedImageCount() != 2 && wait.elapsed() < 5000)
        pump(25);
    CHECK(ws->comparedImages() == QStringList({c, d}),
          "M40: newest compare set wins after A -> B queued cancellation");

    scheduler.pause(TaskScheduler::DecodePool);
    ws->setImages({a, b});
    pump(80);
    CHECK(ws->comparedImageCount() == 0,
          "M40: scheduler rejection does not hang batch completion");
    scheduler.resume(TaskScheduler::DecodePool);
    (void)blocker;
}

void testInspectorCoalescing(const QString &a, const QString &b)
{
    std::cout << "-- Compare M30: inspector hover coalescing --\n";

    QWidget host;
    auto *hostLay = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    hostLay->addWidget(ws);
    SelectionModel sel;
    ws->setSelectionModel(&sel);
    ws->setImages({a, b});
    host.resize(1100, 750);
    host.show();
    waitForCompareCount(ws, 2);

    auto *analysisToggle = ws->findChild<QCheckBox *>("analysisPanelToggle");
    auto *inspector = ws->findChild<QTableWidget *>("pixelInspectorTable");
    CHECK(analysisToggle && inspector, "M30: analysis toggle and inspector are discoverable");
    if (!analysisToggle || !inspector)
        return;
    analysisToggle->setChecked(true);
    pump(50);

    RawImageView *view0 = nullptr;
    RawImageView *view1 = nullptr;
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (!v)
            continue;
        if (v->cellIndex() == 0)
            view0 = v;
        else if (v->cellIndex() == 1)
            view1 = v;
    }
    {
        QElapsedTimer t;
        t.start();
        while ((!view0 || view0->image().isNull() || !view1 || view1->image().isNull()) &&
               t.elapsed() < 8000)
            pump(25);
    }
    CHECK(view0 && !view0->image().isNull() && view1 && !view1->image().isNull(),
          "M30: both panes materialize before hover testing");
    if (!view0 || view0->image().isNull() || !view1 || view1->image().isNull())
        return;

    // Deterministic transform so widget->image mapping is exact: at scale 1.0
    // with zero offset, a widget point near the center maps to a known image
    // pixel. All burst points stay inside the decoded pane image.
    ws->engine().setScale(1.0);
    ws->engine().setOffset(0.0, 0.0);
    ws->update();
    pump(30);

    auto renderCount = [ws]() { return ws->property("inspectorRenderCount").toULongLong(); };
    auto hoverAt = [view0](int dx, int dy)
    {
        const QPoint pt = view0->rect().center() + QPoint(dx, dy);
        QMouseEvent event(QEvent::MouseMove, QPointF(pt), Qt::NoButton, Qt::NoButton,
                          Qt::NoModifier);
        QApplication::sendEvent(view0, &event);
    };

    // (a) A burst of hovers inside one event-loop turn: exactly one render,
    // using the latest coordinate.
    const quint64 burstBase = renderCount();
    const int burstDx[] = {0, 3, 6, 9, 2, 7};
    const int burstDy[] = {0, 1, 2, 3, 5, 4};
    for (int k = 0; k < 6; ++k)
        hoverAt(burstDx[k], burstDy[k]);
    CHECK(renderCount() == burstBase,
          "M30(a): the hover burst performs no synchronous render");
    pump(60);
    CHECK(renderCount() == burstBase + 1,
          "M30(a): a hover burst renders the inspector exactly once");
    QLabel *coord = ws->findChild<QLabel *>("pixelInspectorCoordLabel");
    if (coord)
    {
        const QPointF lastImg = view0->widgetToImage(view0->rect().center() + QPoint(7, 4));
        const QString expectedLast = QStringLiteral("(%1, %2)")
                                         .arg(static_cast<int>(std::floor(lastImg.x())))
                                         .arg(static_cast<int>(std::floor(lastImg.y())));
        CHECK(coord->text() == expectedLast,
              "M30(a): the single render uses the latest hover coordinate");
    }
    else
    {
        CHECK(false, "M30(a): coordinate label is discoverable");
    }

    // (b) One physical hover with the synced crosshair ON emits both pixelInfo
    // and crosshairMoved; the pair must render the inspector exactly once.
    QCheckBox *crosshair = findChk(ws, QStringLiteral("同步准星"));
    CHECK(crosshair != nullptr, "M30: synced crosshair toggle is discoverable");
    if (crosshair)
    {
        const quint64 beforeToggle = renderCount();
        crosshair->setChecked(true);
        pump(20);
        CHECK(renderCount() == beforeToggle,
              "M30(b): enabling the crosshair alone does not render");
        const quint64 beforePair = renderCount();
        hoverAt(4, 4);
        pump(60);
        CHECK(renderCount() == beforePair + 1,
              "M30(b): the pixelInfo + crosshairMoved pair renders once, not twice");
        crosshair->setChecked(false);
        pump(20);
    }

    // (c) Ordinary re-renders reuse the existing QTableWidgetItem objects.
    {
        const QTableWidgetItem *cell00 = inspector->item(0, 0);
        const QTableWidgetItem *cell12 = inspector->item(1, 2);
        CHECK(cell00 && cell12 && !cell12->text().isEmpty(),
              "M30(c): inspector cells are populated before the re-render");
        const quint64 before = renderCount();
        hoverAt(1, 1);
        pump(60);
        CHECK(renderCount() == before + 1, "M30(c): a new hover re-renders");
        CHECK(inspector->item(0, 0) == cell00 && inspector->item(1, 2) == cell12,
              "M30(c): table item objects are reused across ordinary renders");
    }

    // (d) Semantic control changes refresh the current coordinate correctly.
    QComboBox *csCombo = ws->findChild<QComboBox *>("pixelInspectorColorSpaceCombo");
    QComboBox *kernelCombo = ws->findChild<QComboBox *>("pixelInspectorKernelCombo");
    QLabel *statsLabel = ws->findChild<QLabel *>("pixelInspectorStatsLabel");
    CHECK(csCombo && kernelCombo && statsLabel,
          "M30: color-space / kernel / stats controls are discoverable");
    if (csCombo)
    {
        const QString beforeText = inspector->item(1, 2)->text();
        csCombo->setCurrentIndex(2); // HSV
        pump(60);
        const QString hsvText = inspector->item(1, 2)->text();
        bool numeric = false;
        hsvText.toDouble(&numeric);
        CHECK(hsvText != beforeText && numeric,
              "M30(d): color-space change refreshes the current pixel readout");
        CHECK(inspector->horizontalHeaderItem(2) &&
                  inspector->horizontalHeaderItem(2)->text() == QStringLiteral("H"),
              "M30(d): header labels follow the selected color space");
        csCombo->setCurrentIndex(0); // RGB
        pump(60);
        CHECK(inspector->item(1, 2)->text() == beforeText,
              "M30(d): returning to RGB restores the original readout");
    }
    if (kernelCombo && statsLabel)
    {
        const QString beforeStats = statsLabel->text();
        CHECK(!beforeStats.isEmpty(), "M30(d): stats label is populated at the hover point");
        kernelCombo->setCurrentIndex(2); // 5×5
        pump(60);
        const QString afterStats = statsLabel->text();
        CHECK(afterStats != beforeStats && afterStats.contains(QStringLiteral("5×5")),
              "M30(d): kernel change refreshes the neighborhood stats");
        kernelCombo->setCurrentIndex(1); // back to 3×3
        pump(60);
    }
    {
        // Focus/reference change via the real pane double-click path (M30
        // review): RawImageView::mouseDoubleClickEvent emits focusRequested, and
        // onFocusRequested must lock the double-clicked pane as the reference
        // exactly once — the button is synchronized without re-entering through
        // its toggled handler and immediately toggling the lock back off.
        const QPoint hovPt = view1->rect().center();
        QMouseEvent hov(QEvent::MouseMove, QPointF(hovPt), Qt::NoButton, Qt::NoButton,
                        Qt::NoModifier);
        QApplication::sendEvent(view1, &hov);
        pump(60);
        QPushButton *lockReference = nullptr;
        for (QPushButton *b : ws->findChildren<QPushButton *>())
            if (b->isCheckable() && b->text().startsWith(QStringLiteral("锁定基准")))
                lockReference = b;
        CHECK(lockReference != nullptr, "M30: lock-reference button is discoverable");
        if (!lockReference)
            return;
        auto doubleClick = [](RawImageView *v, const QPoint &pt)
        {
            QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(pt), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(v, &event);
        };
        // First double-click locks pane 1 (m_focusIndex 1, button checked, and
        // the inspector refreshes with a zero delta on the reference row).
        const quint64 before = renderCount();
        doubleClick(view1, hovPt);
        pump(60);
        CHECK(renderCount() > before,
              "M30(d): pane double-click re-renders the inspector at the current coordinate");
        CHECK(lockReference->isChecked(),
              "M30(d): pane double-click locks the reference button");
        CHECK(ws->focusImagePath() == b,
              "M30(d): the workspace reference path is the locked pane");
        CHECK(sel.focused() == b,
              "M30(d): the SelectionModel focused path follows the locked reference");
        CHECK(inspector->item(1, 5) && inspector->item(1, 5)->text() == QStringLiteral("0"),
              "M30(d): the locked reference pane shows a zero delta");
        CHECK(inspector->rowCount() == 2 && inspector->item(0, 2) &&
                  !inspector->item(0, 2)->text().isEmpty(),
              "M30(d): the inspector stays fully populated after the focus change");
        // Second double-click on the same pane unlocks it and the button follows.
        doubleClick(view1, hovPt);
        pump(60);
        CHECK(!lockReference->isChecked(),
              "M30(d): double-clicking the locked pane again unlocks the reference");
        CHECK(sel.focused().isEmpty(),
              "M30(d): unlocking clears the SelectionModel focused path");
    }

    // (e) A queued render must be lifetime-safe: destroying the workspace with
    // a pending inspector render never acts on the destroyed object.
    {
        auto *sub = new QWidget;
        auto *subLay = new QVBoxLayout(sub);
        auto *subWs = new CompareWorkspace(sub);
        subLay->addWidget(subWs);
        SelectionModel subSel;
        subWs->setSelectionModel(&subSel);
        subWs->setImages({a, b});
        sub->resize(900, 600);
        sub->show();
        waitForCompareCount(subWs, 2);
        QCheckBox *subToggle = subWs->findChild<QCheckBox *>("analysisPanelToggle");
        RawImageView *subView = nullptr;
        for (RawImageView *v : subWs->findChildren<RawImageView *>())
            if (v && v->cellIndex() == 0)
                subView = v;
        if (subToggle && subView)
        {
            subToggle->setChecked(true);
            pump(30);
            {
                QElapsedTimer t;
                t.start();
                while (subView->image().isNull() && t.elapsed() < 8000)
                    pump(25);
            }
            const QPoint pt = subView->rect().center();
            QMouseEvent event(QEvent::MouseMove, QPointF(pt), Qt::NoButton, Qt::NoButton,
                              Qt::NoModifier);
            QApplication::sendEvent(subView, &event); // schedules a queued render
            QPointer<CompareWorkspace> destroyed = subWs;
            delete sub; // destroys subWs while the queued render is still pending
            CHECK(destroyed.isNull(), "M30(e): workspace is destroyed before queued render drains");
        }
        else
        {
            delete sub;
        }
        pump(80); // the dropped queued render must not touch the destroyed workspace
        CHECK(QApplication::closingDown() == false,
              "M30(e): pending inspector render does not close the application");
    }
}

} // namespace

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    QDir dir(tmp.filePath("cmp"));
    dir.mkpath(".");
    QStringList paths8;
    for (int i = 0; i < 8; ++i)
    {
        paths8 << writePng(dir, QString("cmp_%1.png").arg(i), 160 + i * 8, 120 + i * 4,
                           QColor(20 * i, 40, 255 - 20 * i));
    }
    const QStringList paths6 = paths8.mid(0, 6);

    testMultiImageEntry(paths8);
    testModePreservesState(paths8[0], paths8[1]);
    testContinuousNav(paths6);
    testPaneHistogramConsistency(paths8[0], paths8[1]);
    testDegradedImages(dir);
    testCompareLoadIsAsync(dir);
    testLargeCompareDisplayBounded(dir);
    testCompareLoadCancellation(dir);
    testInspectorCoalescing(paths8[0], paths8[1]);

    if (g_failures > 0)
    {
        std::cout << "compare_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_acceptance_tests: PASS\n";
    return 0;
}
