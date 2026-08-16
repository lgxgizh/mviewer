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
#include "runtime_storage.h"
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

// RAII safety net for pausing a pool: resumes it on scope exit, so even a
// failing CHECK (CHECK only counts failures and continues) or an early return
// can never leave a pool refusing submissions for later tests.
class PoolPauseGuard
{
  public:
    explicit PoolPauseGuard(TaskScheduler::PoolType pool) : m_pool(pool)
    {
        TaskScheduler::instance().pause(m_pool);
    }
    ~PoolPauseGuard()
    {
        TaskScheduler::instance().resume(m_pool);
    }

  private:
    TaskScheduler::PoolType m_pool;
};

// An analyzer whose analysis blocks on a Gate until the test releases it. The
// gate state is captured by value (shared_ptr) in the factory, so the instance
// created on the UI thread and executed on the Analysis pool never touches the
// registry or any QObject. `result` selects what analyze()/analyzeRegion()
// return after the gate opens — a false-returning variant lets a test drive
// the legacy ROI-fallback branch that runs after a no-result analyzer.
class GatedAnalyzer : public Analyzer
{
  public:
    explicit GatedAnalyzer(std::shared_ptr<Gate> gate, bool result = true)
        : m_gate(std::move(gate)), m_result(result)
    {
    }
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
            return m_result;
        // A correct implementation only ever runs this analyzer on the
        // Analysis pool. If it runs on the UI thread (regression: the old
        // synchronous refreshFromFrame/reanalyze), refuse to block — return
        // failure so the test fails cleanly instead of deadlocking the whole
        // suite.
        if (QCoreApplication::instance() &&
            QThread::currentThread() == QCoreApplication::instance()->thread())
            return false;
        m_gate->started.fetch_add(1, std::memory_order_relaxed);
        std::unique_lock<std::mutex> lk(m_gate->m);
        m_gate->cv.wait(lk, [this]() { return m_gate->open; });
        return m_result;
    }
    std::shared_ptr<Gate> m_gate;
    bool m_result = true;
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

void registerGatedAnalyzer(const std::shared_ptr<Gate> &gate, const std::string &id = "m28_gated",
                           bool result = true)
{
    AnalyzerRegistry::instance().registerAnalyzer(
        id,
        [gate, result]()
        {
            return std::unique_ptr<Analyzer, AnalyzerDeleter>(new GatedAnalyzer(gate, result),
                                                              [](Analyzer *p) { delete p; });
        });
}

void unregisterGatedAnalyzer(const std::string &id = "m28_gated")
{
    AnalyzerRegistry::instance().unregister(id);
}

QString historyStoragePath()
{
    return mviewer::runtime::filePath(QStandardPaths::AppDataLocation,
                                      QStringLiteral("analysis_history.json"));
}

#include "test_analyze_acceptance_cases.inc"
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-analyze-acceptance-test");
    QCoreApplication::setApplicationName("mviewer-analyze-acceptance-test");
    mviewer::runtime::configureSettings();

    testM28HiddenPanelDefersConversion();
    testM28AsyncLatestWins();
    testM28AsyncPanelDestruction();
    testM28PendingAnalyzerChange();
    testM28PendingRoiChange();
    testManualAnalyzerRunAsync();
    testManualAnalyzerLatestWins();
    testManualRoiChangeAsync();
    testManualRejectedReplacement();
    testManualNoResultTerminal();
    testManualDestructionPending();
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
