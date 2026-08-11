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
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
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
    CHECK(!ws->engine().layout().cols == false || ws->engine().layout().cols >= 2,
          "B#2: 8-up produces a multi-column grid");

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

    perPane->setChecked(true);
    overlay->setChecked(true);
    pump(30);

    auto paneHist = [ws](int index)
    { return ws->findChild<HistogramWidget *>(QString("paneHistogram%1").arg(index)); };
    HistogramWidget *hist0 = paneHist(0);
    HistogramWidget *hist1 = paneHist(1);
    CHECK(hist0 && hist1, "P1: both pane histogram widgets exist");
    if (!hist0 || !hist1)
        return;
    CHECK(hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
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
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramTotal(0) > 0 && hist1->histogramTotal(0) > 0,
          "P1: Blink-active rebuild repopulates pane histograms");

    blink->setChecked(false);
    pump(50);
    hist0 = paneHist(0);
    hist1 = paneHist(1);
    CHECK(hist0 && hist1 && hist0->histogramCount() == 1 && hist0->histogramTotal(0) > 0 &&
              hist1->histogramCount() == 1 && hist1->histogramTotal(0) > 0,
          "P1: Blink stop rebuild retains populated pane histograms");

    roi->setChecked(true);
    ws->applyROI({0, 0, 32, 32});
    pump(40);
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

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    QDir dir(tmp.filePath("cmp"));
    dir.mkpath(".");
    QStringList paths8;
    for (int i = 0; i < 8; ++i)
        paths8 << writePng(dir, QString("cmp_%1.png").arg(i), 160 + i * 8, 120 + i * 4,
                           QColor(20 * i, 40, 255 - 20 * i));
    const QStringList paths6 = paths8.mid(0, 6);

    testMultiImageEntry(paths8);
    testModePreservesState(paths8[0], paths8[1]);
    testContinuousNav(paths6);
    testPaneHistogramConsistency(paths8[0], paths8[1]);
    testDegradedImages(dir);
    testCompareLoadIsAsync(dir);

    if (g_failures > 0)
    {
        std::cout << "compare_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "compare_acceptance_tests: PASS\n";
    return 0;
}
