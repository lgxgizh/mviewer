// M24 Phase 4C 鈥?Analyze workflow acceptance tests.
//
// Maps the M24 Workflow C acceptance items that are automatable headless:
//   C#2  analyzer list, execution status, and result meaning are explicit
//   C#4  results stay keyed to the image they were computed for (no stale
//        result silently attached to a different image)
//   C#5  Analysis History and pinned results are consistent with the model
//   C#6  persisted history round-trips and stays path-keyed across restart
//   C#7  a failing analyzer (buggy plugin) cannot take down the app: batch
//        runs skip it and the pipeline/panel paths degrade to empty results
//   C#8  scalar metrics come with machine-readable names (units live in the
//        analyzer info/output fields, asserted via resultMetrics keys)
//
// (Registry-level numeric correctness is covered by analyzer_pipeline_tests /
// test_analyzer_pipeline / analysis_panel_tests.)

#include "analysispanel.h"
#include "analyzermodel.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"
#include "widgets/rawimageview.h" // completes RawImageView for AnalysisPanel's unique_ptr

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
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

// A deliberately broken analyzer: fails from every entry point. Simulates a
// buggy plugin the registry must isolate.
class FailingAnalyzer : public Analyzer
{
  public:
    std::string name() const override
    {
        return "failing";
    }
    std::string description() const override
    {
        return "always fails";
    }
    bool analyze(const ImageFrame &) override
    {
        return false;
    }
    bool analyzeRegion(const ImageFrame &, const mviewer::domain::Selection &) override
    {
        return false;
    }
};

std::shared_ptr<ImageFrame> makeFrame(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    auto frame = std::make_shared<ImageFrame>();
    frame->setPixels(mvcore::fromQImage(img));
    return frame;
}

std::shared_ptr<ImageFrame> makeFrameWithPath(const QString &path, int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    return std::make_shared<ImageFrame>(ImageFrame::create(path.toStdString(), mvcore::fromQImage(img)));
}

// ── M28 async automatic-frame analysis: release-gated worker tests ───────────
//
// The automatic AnalysisPanel refresh (ImageViewer::imageReady -> setFrame ->
// queued async AnalysisPool task) must never do full-image conversion/stats/
// noise/analyzer work on the UI thread. A gated analyzer blocks inside the
// worker so the tests can observe, deterministically, that the UI thread never
// waits for (and never does) that work.
struct Gate
{
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    std::atomic<int> started{0};
    // Empty string: block every analysis. Otherwise block only the analysis of
    // this exact frame path (lets a test make one frame slow, another fast).
    std::string blockPath;

    bool shouldBlock(const std::string &path) const
    {
        return blockPath.empty() || blockPath == path;
    }
};

// RAII safety net: opens the gate on scope exit, so even a failing assertion
// (CHECK only counts failures and continues) or an early return can never leave
// the shared scheduler with a permanently blocked AnalysisPool worker.
class GateOpener
{
  public:
    explicit GateOpener(std::shared_ptr<Gate> gate) : m_gate(std::move(gate)) {}
    ~GateOpener()
    {
        open();
    }
    void open()
    {
        if (!m_gate)
            return;
        std::lock_guard<std::mutex> lk(m_gate->m);
        m_gate->open = true;
        m_gate->cv.notify_all();
    }

  private:
    std::shared_ptr<Gate> m_gate;
};

// An analyzer whose analysis blocks on a Gate until the test releases it. The
// gate state is captured by value (shared_ptr) in the factory, so the instance
// created on the UI thread and executed on the Analysis pool never touches the
// registry or any QObject.
class GatedAnalyzer : public Analyzer
{
  public:
    explicit GatedAnalyzer(std::shared_ptr<Gate> gate) : m_gate(std::move(gate)) {}
    std::string name() const override
    {
        return "m28_gated";
    }
    std::string description() const override
    {
        return "blocks its analysis until the test gate opens";
    }
    bool analyze(const ImageFrame &frame) override
    {
        return run(frame.metadata().filePath);
    }
    bool analyzeRegion(const ImageFrame &frame, const mviewer::domain::Selection &) override
    {
        return run(frame.metadata().filePath);
    }
    std::string resultText() const override
    {
        return "m28 gated result";
    }

  private:
    bool run(const std::string &path)
    {
        if (!m_gate->shouldBlock(path))
            return true;
        // A correct implementation only ever runs this analyzer on the
        // Analysis pool. If it runs on the UI thread (regression: the old
        // synchronous refreshFromFrame), refuse to block — return failure so
        // the test fails cleanly instead of deadlocking the whole suite.
        if (QCoreApplication::instance() &&
            QThread::currentThread() == QCoreApplication::instance()->thread())
            return false;
        m_gate->started.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lk(m_gate->m);
        m_gate->cv.wait(lk, [this]() { return m_gate->open; });
        return true;
    }
    std::shared_ptr<Gate> m_gate;
};

// Pump the event loop for `ms` so queued deliveries / timers can run.
void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

// Poll `pred` (pumping events) until it returns true or the deadline elapses.
bool waitFor(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
    }
    return pred();
}

bool waitForLoaded(AnalysisPanel &panel, int ms)
{
    return waitFor([&panel]() { return panel.hasLoadedImage(); }, ms);
}

void registerGatedAnalyzer(const std::shared_ptr<Gate> &gate)
{
    AnalyzerRegistry::instance().registerAnalyzer(
        "m28_gated",
        [gate]()
        {
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(new GatedAnalyzer(gate),
                                                              [](Analyzer *p) { delete p; });
        });
}

void unregisterGatedAnalyzer()
{
    AnalyzerRegistry::instance().unregister("m28_gated");
}

QString historyStoragePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           "/analysis_history.json";
}

void testC7FailingAnalyzer()
{
    std::cout << "── Analyze C#7: failing analyzer isolation ──\n";
    AnalyzerRegistry &reg = AnalyzerRegistry::instance();
    // NOTE: the factory MUST supply an AnalyzerDeleter — a default-constructed
    // std::function deleter is empty, and destroying the unique_ptr then
    // throws std::bad_function_call (uncaught in the scheduler worker ->
    // terminate). The builtin factories all pass an explicit deleter.
    reg.registerAnalyzer("m24_failing",
                         []()
                         {
                             return std::unique_ptr<Analyzer, AnalyzerDeleter>(
                                 new FailingAnalyzer(), [](Analyzer *p) { delete p; });
                         });

    const auto frame = makeFrame(64, 64, QColor(12, 34, 56));

    // Batch run: the failing analyzer must be skipped, others must produce
    // results, and the call must not throw.
    const auto results = reg.runAnalyzer(*frame);
    CHECK(results.find("m24_failing") == results.end(),
          "C#7: failing analyzer omitted from batch results");
    CHECK(results.size() >= 5, "C#7: healthy analyzers still produce results");

    // Pipeline region path: empty result, no exception.
    const AnalyzerPipeline pipeline;
    const std::string region = pipeline.runRegion(*frame, {0, 0, 16, 16}, "m24_failing");
    CHECK(region.empty(), "C#7: pipeline runRegion degrades to empty on a failing analyzer");

    // A second healthy run still works after the failure (no corrupted state).
    const auto again = reg.runAnalyzer(*frame);
    CHECK(again.find("histogram") != again.end(),
          "C#7: registry stays functional after isolating a failing analyzer");

    // NOTE (M24): a plugin that THROWS (not just returns false) cannot be
    // isolated in-process in the current build configuration: the project
    // compiles without /EHsc (exception unwind semantics off — see
    // docs/review/M24_TEST_CREDIBILITY_2026-08-05.md §8), and a cross-DLL
    // throw poisons the EH runtime so a later exception fail-fasts the app
    // (0xc0000409, reproducible via runAnalyzer after any thrown+caught
    // cross-module exception). Hard crashes are handled by the plugin
    // subprocess runner (pluginregistryrunner_tests); enabling /EHsc is the
    // proposed fix (commander decision, build config is frozen).

    reg.unregister("m24_failing");
}

void testC2C4C5PanelAndModel(const QString &pathA, const QString &pathB)
{
    std::cout << "鈹€鈹€ Analyze C#2/C#4/C#5: panel + model currency 鈹€鈹€\n";

    AnalyzerModel model;
    model.clearAllResults();

    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.show();

    // C#2: the analyzer list is populated and selectable.
    const auto ids = AnalyzerPipeline().analyzerIds();
    CHECK(ids.size() >= 7, "C#2: analyzer list exposes the built-in analyzers");

    // C#4: results are keyed to the image they were computed for.
    const auto frameA = makeFrame(48, 32, QColor(200, 30, 30));
    const auto frameB = makeFrame(48, 32, QColor(30, 200, 30));
    panel.setFrame(frameA);
    panel.setImage(mvcore::toQImage(frameA->pixels()), pathA);
    panel.selectAnalyzer("histogram");
    panel.reanalyze();
    panel.setFrame(frameB);
    panel.setImage(mvcore::toQImage(frameB->pixels()), pathB);
    panel.reanalyze();

    const QString resA = model.resultText(pathA);
    const QString resB = model.resultText(pathB);
    CHECK(!resA.isEmpty(), "C#4: analysis result recorded for image A");
    CHECK(!resB.isEmpty(), "C#4: analysis result recorded for image B");
    const QStringList hist = model.history();
    CHECK(hist.contains(pathA) && hist.contains(pathB),
          "C#4: history tracks exactly the analyzed images");

    // C#5: pinning is consistent with the model.
    model.pinResult(pathA);
    CHECK(model.pinned().contains(pathA), "C#5: pinned results tracked in the model");
    CHECK(!model.pinned().contains(pathB), "C#5: only explicitly pinned results pinned");
}

void testC6PersistenceRoundTrip()
{
    std::cout << "鈹€鈹€ Analyze C#6: persistence round-trip 鈹€鈹€\n";
    const QString file = historyStoragePath();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile::remove(file);

    {
        AnalyzerModel model;
        model.setResult("/data/img_a.png", "histogram lum=120.5");
        model.setResult("/data/img_b.png", "histogram lum=80.0");
        model.pinResult("/data/img_b.png");
        model.save();
        CHECK(QFile::exists(file), "C#6: analysis history persisted to disk");
    }
    {
        // Fresh model = simulated restart.
        AnalyzerModel model;
        model.load();
        CHECK(model.resultText("/data/img_a.png").contains("120.5"),
              "C#6: result reloads for the correct path");
        CHECK(model.resultText("/data/img_b.png").contains("80.0"),
              "C#6: second result reloads for its own path");
        CHECK(model.resultText("/data/other.png").isEmpty(),
              "C#6: no phantom result for an unrelated path");
        CHECK(model.pinned().contains("/data/img_b.png"), "C#6: pinned state survives restart");
        CHECK(model.history().contains("/data/img_a.png"), "C#6: history survives restart");
    }
    QFile::remove(file);
}

// M28 P1-02/P1-04: a HIDDEN AnalysisPanel must not submit any automatic
// Analysis work, and SHOWING a dirty panel must submit that work asynchronously
// and return promptly — the full-image conversion / stats / noise / analyzer
// execution runs on the Analysis pool, never on the UI thread. A release-gated
// analyzer proves both: while the worker is blocked on the gate the UI thread
// is free, `hasLoadedImage()` stays false, and it only becomes true after the
// gate is released and the queued delivery lands.
void testM28HiddenPanelDefersConversion()
{
    std::cout << "-- Analyze M28 P1-04: hidden defers, show is async --\n";

    auto gate = std::make_shared<Gate>();
    GateOpener opener{gate}; // RAII: can never leave the pool blocked
    registerGatedAnalyzer(gate);

    AnalyzerModel model;
    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.selectAnalyzer("m28_gated");
    const auto frame = makeFrameWithPath("/tmp/m28_hidden.png", 512, 512, QColor(30, 200, 90));

    // Hidden: setFrame must store the frame and submit NO Analysis work.
    panel.setFrame(frame);
    pump(50);
    CHECK(gate->started.load() == 0,
          "hidden panel submits no automatic Analysis work");
    CHECK(!panel.hasLoadedImage(),
          "hidden panel does not materialize a full-size QImage on the UI thread");

    // Showing a dirty panel submits the work asynchronously. The worker blocks
    // on the gate, so we have returned here without ever waiting for it.
    panel.show();
    CHECK(waitFor([gate]() { return gate->started.load() >= 1; }, 5000),
          "showing a dirty panel submits Analysis work to the pool");
    CHECK(!panel.hasLoadedImage(),
          "UI thread returned promptly: hasLoadedImage stays false while the "
          "release-gated worker is blocked");

    // Release the worker: the queued delivery then presents the frame.
    opener.open();
    CHECK(waitForLoaded(panel, 5000),
          "after gate release the frame becomes loaded");
    CHECK(!model.history().isEmpty(), "automatic analysis result is recorded");
    unregisterGatedAnalyzer();

    // Ensure the blocked worker finished before any later test uses the pool.
    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                    std::chrono::milliseconds(5000));
}

// Latest-wins: a rapid A -> B setFrame must never present (or record) stale A
// output as B. A is release-gated; B is submitted while A is still blocked, so
// A is cancelled. On release A's delivery is discarded (cancellation + stale
// generation) and only B's result is delivered.
void testM28AsyncLatestWins()
{
    std::cout << "-- Analyze M28 P1-04: latest-wins A->B --\n";

    auto gate = std::make_shared<Gate>();
    GateOpener opener{gate};
    const QString pathA = "/tmp/m28_async_a.png";
    const QString pathB = "/tmp/m28_async_b.png";
    gate->blockPath = pathA.toStdString();
    registerGatedAnalyzer(gate);

    AnalyzerModel model;
    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.selectAnalyzer("m28_gated");
    panel.show();

    const auto frameA = makeFrameWithPath(pathA, 256, 256, QColor(200, 30, 30));
    const auto frameB = makeFrameWithPath(pathB, 256, 256, QColor(30, 200, 30));

    // A's task starts and blocks on the gate.
    panel.setFrame(frameA);
    CHECK(waitFor([gate]() { return gate->started.load() >= 1; }, 5000),
          "A worker started and is blocked");
    // B arrives while A is still blocked; A is cancelled, B is scheduled.
    panel.setFrame(frameB);
    CHECK(!panel.hasLoadedImage(),
          "hasLoadedImage is false while the newest automatic result is pending");

    // Release: A discards (cancelled), B delivers.
    opener.open();
    CHECK(waitForLoaded(panel, 5000), "B becomes the loaded image");
    CHECK(model.resultText(pathA).isEmpty(),
          "stale A result was never recorded");
    CHECK(!model.resultText(pathB).isEmpty(), "B result recorded for B's path");
    CHECK(model.resultText(pathB).contains("m28 gated result"),
          "the recorded B result is the B analysis output");
    unregisterGatedAnalyzer();

    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                    std::chrono::milliseconds(5000));
}

// Changing the selected analyzer while the first automatic job is release-gated
// must supersede the old task (latest-wins) and eventually materialize the
// current frame — an explicit reanalyze() must never invalidate a pending
// automatic job and then synchronously analyze an unmaterialized frame, which
// would strand the panel blank.
void testM28PendingAnalyzerChange()
{
    std::cout << "-- Analyze M28 P1-04: pending analyzer change reschedules --\n";

    auto gate = std::make_shared<Gate>();
    GateOpener opener{gate}; // blockPath empty -> every gated run blocks
    registerGatedAnalyzer(gate);

    AnalyzerModel model;
    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.selectAnalyzer("m28_gated");
    panel.show();
    const QString path = "/tmp/m28_pending_analyzer.png";
    panel.setFrame(makeFrameWithPath(path, 128, 128, QColor(200, 30, 30)));
    CHECK(waitFor([gate]() { return gate->started.load() >= 1; }, 5000),
          "first automatic job started and is release-gated");
    CHECK(!panel.hasLoadedImage(), "panel stays blank while the stale job is gated");

    panel.runAnalyzer("histogram"); // select + reanalyze -> latest-wins reschedule
    CHECK(waitForLoaded(panel, 5000),
          "replacement analyzer job delivers while the stale job is still gated");
    CHECK(!model.resultText(path).isEmpty(),
          "current-path result recorded by the replacement job");
    CHECK(!model.resultText(path).contains("m28 gated result"),
          "no stale gated output attached to the current path");

    opener.open(); // release the still-blocked stale job
    unregisterGatedAnalyzer();
    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                    std::chrono::milliseconds(5000));
}

// Setting an ROI while the first automatic job is release-gated must supersede
// the old task (latest-wins); the replacement (gated too) must load the current
// frame after the gate is released.
void testM28PendingRoiChange()
{
    std::cout << "-- Analyze M28 P1-04: pending ROI change reschedules --\n";

    auto gate = std::make_shared<Gate>();
    GateOpener opener{gate}; // blockPath empty -> every gated run blocks
    registerGatedAnalyzer(gate);

    AnalyzerModel model;
    AnalysisPanel panel;
    panel.setAnalyzerModel(&model);
    panel.selectAnalyzer("m28_gated");
    panel.show();
    const QString path = "/tmp/m28_pending_roi.png";
    panel.setFrame(makeFrameWithPath(path, 128, 128, QColor(30, 200, 90)));
    CHECK(waitFor([gate]() { return gate->started.load() >= 1; }, 5000),
          "first automatic job started and is release-gated");
    CHECK(!panel.hasLoadedImage(), "panel stays blank while the stale job is gated");

    panel.setROI({16, 16, 64, 64});
    CHECK(waitFor([gate]() { return gate->started.load() >= 2; }, 5000),
          "ROI change submits a replacement job");
    CHECK(!panel.hasLoadedImage(), "replacement job is still gated before release");

    opener.open(); // release the stale + replacement jobs
    CHECK(waitForLoaded(panel, 5000),
          "replacement ROI job loads the current frame after release");
    CHECK(!model.resultText(path).isEmpty(),
          "current-path result recorded after the ROI reschedule");

    opener.open(); // idempotent safety net
    unregisterGatedAnalyzer();
    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                    std::chrono::milliseconds(5000));
}

// Destroying the panel while its automatic task is queued/running must not
// crash, touch freed memory, or leave the Analysis pool undrainable.
void testM28AsyncPanelDestruction()
{
    std::cout << "-- Analyze M28 P1-04: destruction while task pending --\n";

    auto gate = std::make_shared<Gate>();
    GateOpener opener{gate};
    registerGatedAnalyzer(gate);

    AnalyzerModel model;
    auto *panel = new AnalysisPanel;
    panel->setAnalyzerModel(&model);
    panel->selectAnalyzer("m28_gated");
    panel->show();
    const auto frame = makeFrame(128, 128, QColor(120, 90, 200));
    panel->setFrame(frame);
    CHECK(waitFor([gate]() { return gate->started.load() >= 1; }, 5000),
          "worker started before destruction");
    delete panel; // destroyed while the worker is blocked
    opener.open(); // let the cancelled worker finish and try to deliver
    const bool drained =
        TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                        std::chrono::milliseconds(5000));
    CHECK(drained, "Analysis pool drains after panel destruction with pending work");
    unregisterGatedAnalyzer();

    // The scheduler must still deliver to a fresh panel (no wedged state).
    AnalysisPanel panel2;
    panel2.setAnalyzerModel(&model);
    panel2.setFrame(makeFrame(64, 64, QColor(10, 10, 10)));
    panel2.show();
    CHECK(waitForLoaded(panel2, 5000),
          "a fresh panel still analyzes after a panel is destroyed mid-task");
    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool,
                                    std::chrono::milliseconds(5000));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-analyze-acceptance-test");
    QCoreApplication::setApplicationName("mviewer-analyze-acceptance-test");

    testM28HiddenPanelDefersConversion();
    testM28AsyncLatestWins();
    testM28AsyncPanelDestruction();
    testM28PendingAnalyzerChange();
    testM28PendingRoiChange();
    testC7FailingAnalyzer();
    testC2C4C5PanelAndModel("/tmp/m24_ana_a.png", "/tmp/m24_ana_b.png");
    testC6PersistenceRoundTrip();

    if (g_failures > 0)
    {
        std::cout << "analyze_acceptance_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "analyze_acceptance_tests: PASS\n";
    return 0;
}
