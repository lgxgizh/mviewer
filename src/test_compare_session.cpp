// M15 P0#1 — Compare Workspace session round-trip test.
//
// Acceptance: "保存 Workspace → 关闭软件 → 重新打开 → Compare 完全恢复".
//
// This test exercises the REAL capture/restore path that previously hid a bug:
// CompareEngine::session() used to leave the ROI/selection empty, so it was
// serialized as [0,0,0,0] and could never be restored. The test goes through
//   compareSession() -> serialize -> deserialize -> applySession()
// on two independent CompareWorkspace instances and asserts every persisted
// field (ROI, layout, threshold, side panel, blink, zoom) survives the trip.

#include "compareworkspace.h"
#include "core/EventBus.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/workspace/WorkspaceSerializer.h"
#include "domain/CompareSession.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{
// Make a tiny valid PNG on disk so CompareWorkspace::setImages() can load it.
QString writeTempPng(const std::string &name)
{
    const QString path = QDir::tempPath() + "/" + QString::fromStdString(name) + ".png";
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(0xFF336699);
    img.save(path, "PNG");
    return path;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QString p1 = writeTempPng("cmp_a");
    const QString p2 = writeTempPng("cmp_b");
    const QStringList paths = {p1, p2};

    // ---- Producer: configure a non-trivial Compare state via the public
    //      applySession() path (the same path a Workspace load uses). ----
    CompareWorkspace producer;
    producer.setImages(paths);

    mviewer::domain::CompareSession s;
    s.imageIds = {p1.toStdString(), p2.toStdString()};
    s.selection = {1, 2, 4, 3, true, true}; // ROI (x,y,w,h,active,synced)
    s.threshold = 120;
    s.layoutIndex = 2; // 2 columns
    s.sidePanelVisible = true;
    s.blinkIntervalMs = 350;
    s.blinkIndex = 1; // blinking on
    producer.applySession(s);

    // ---- 1) Engine-level ROI capture (the previously-broken path). ----
    const mviewer::domain::CompareSession captured = producer.compareSession();
    assert(captured.selection.w > 0 && captured.selection.h > 0 &&
           "CompareEngine::session() must capture the ROI/selection");
    assert(captured.selection.x == 1 && captured.selection.y == 2 && captured.selection.w == 4 &&
           captured.selection.h == 3 &&
           "captured ROI must match applied selection");
    std::cout << "[ok] CompareEngine::session() captures ROI (" << captured.selection.x << ","
              << captured.selection.y << "," << captured.selection.w << ","
              << captured.selection.h << ")\n";

    // ---- 2) Serialize -> deserialize round-trip preserves every field. ----
    const std::string json = mviewer::core::serializeCompareSession(captured);
    const auto r = mviewer::core::deserializeCompareSession(json);
    assert(r.has_value() && "deserializeCompareSession must succeed");
    assert(r->selection.w == 4 && r->selection.h == 3 && "ROI must survive round-trip");
    assert(r->threshold == 120 && "threshold must survive round-trip");
    assert(r->layoutIndex == 2 && "layoutIndex must survive round-trip");
    assert(r->sidePanelVisible == true && "sidePanelVisible must survive round-trip");
    assert(r->blinkIntervalMs == 350 && "blinkIntervalMs must survive round-trip");
    assert(r->blinkIndex == 1 && "blinkIndex must survive round-trip");
    assert(static_cast<int>(r->imageIds.size()) == 2 && "image list must survive round-trip");
    std::cout << "[ok] serialize -> deserialize preserves ROI/threshold/layout/side/blink\n";

    // ---- 3) applySession on a fresh workspace restores the state. ----
    CompareWorkspace consumer;
    consumer.setImages(paths); // engine must own frames before applySession
    consumer.applySession(*r);

    mviewer::domain::CompareSession after = consumer.compareSession();
    assert(after.selection.w == 4 && after.selection.h == 3 && "applied session must restore ROI");
    assert(after.threshold == 120 && "applied session must restore threshold");
    assert(after.layoutIndex == 2 && "applied session must restore layout");
    assert(after.sidePanelVisible == true && "applied session must restore side panel");
    assert(after.blinkIntervalMs == 350 && "applied session must restore blink interval");
    std::cout << "[ok] applySession fully restores Compare state on a fresh workspace\n";

    // ---- 4) Regression: destroying engines while their diffs are queued must
    //      not leave worker callbacks dereferencing the destroyed engine. ----
    TaskScheduler &scheduler = TaskScheduler::instance();
    assert(scheduler.drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5)) &&
           "earlier analysis jobs must drain before the lifetime regression");
    scheduler.setPoolMaxThreads(TaskScheduler::AnalysisPool, 1);

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};
    const auto blocker = scheduler.submit(
        TaskScheduler::Priority::Analysis,
        [&blockerStarted, &releaseBlocker](const TaskScheduler::TaskContext &)
        {
            blockerStarted.store(true, std::memory_order_release);
            while (!releaseBlocker.load(std::memory_order_acquire))
                std::this_thread::yield();
        });
    assert(blocker && "analysis queue blocker must be accepted");

    const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blockerStarted.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < waitDeadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        std::this_thread::yield();
    }
    assert(blockerStarted.load(std::memory_order_acquire) && "analysis queue blocker must start");

    std::atomic<int> diffEvents{0};
    const int diffSubId = EventBus::instance().subscribe(
        "CompareEngine.DiffResult",
        [&diffEvents](void *)
        {
            diffEvents.fetch_add(1, std::memory_order_relaxed);
        });

    constexpr int kQueuedEngines = 32;
    for (int i = 0; i < kQueuedEngines; ++i)
    {
        auto engine = std::make_unique<CompareEngine>();
        engine->setImages({p1.toStdString(), p2.toStdString()});
        assert(engine->requestDiff(1, 0) && "queued diff must be accepted");
    }

    releaseBlocker.store(true, std::memory_order_release);
    assert(scheduler.drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5)) &&
           "queued compare diffs must finish within the bounded wait");
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    EventBus::instance().unsubscribe(diffSubId);
    assert(diffEvents.load(std::memory_order_relaxed) == 0 &&
           "destroyed compare engines must not publish completion events");

    QFile::remove(p1);
    QFile::remove(p2);
    std::cout << "compare_session_tests: PASS\n";
    return 0;
}
