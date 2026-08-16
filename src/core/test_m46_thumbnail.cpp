// M46 — ThumbnailPipeline mid-flight supersession tests.
//
// Contract under test (see ThumbnailPipeline::setThumbSize + the generation
// guards in the pipeline):
//
//   T1  A thumbnail-size change while decodes at the old size are in flight
//       CANCELLS the old-size workload: superseded decodes are neither cached
//       nor delivered, and no FURTHER old-size decode is scheduled after the
//       change (stale workload bounded to the in-flight batch).
//   T2  Rapid size churn (A→B→C→D) with in-flight decodes converges: only the
//       final size is cached/delivered and the pipeline returns to
//       handles==0 / pending==0 when idle.
//   T3  setSources() (directory switch) mid-decode drops every stale result:
//       no old-directory delivery, no cache pollution.
//   T4  Idle convergence: the ThumbnailPool and the pipeline bookkeeping
//       return to zero after every interleaving.
//
// All decodes are injected fake functions gated by latches — the interleavings
// are constructed deterministically, never by timing.

#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QCoreApplication>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

namespace
{
using PoolType = TaskScheduler::PoolType;

std::vector<std::string> makeSources(const std::string &prefix, int n)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        out.push_back(prefix + "/img_" + std::to_string(i) + ".png");
    return out;
}

// A decode gate that blocks every call until released. Counts per-size and
// per-directory calls.
struct DecodeGate
{
    std::atomic<int> calls{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<int> callsAtSize64{0};
    std::atomic<int> callsAtSize128{0};
    std::atomic<int> callsAtSize256{0};
    std::atomic<int> callsDirA{0};
    std::atomic<int> callsDirB{0};
};

ImageData gatedDecode(const std::shared_ptr<DecodeGate> &gate, const std::string &path, int size)
{
    gate->calls.fetch_add(1, std::memory_order_relaxed);
    if (size == 64)
        gate->callsAtSize64.fetch_add(1, std::memory_order_relaxed);
    else if (size == 128)
        gate->callsAtSize128.fetch_add(1, std::memory_order_relaxed);
    else
        gate->callsAtSize256.fetch_add(1, std::memory_order_relaxed);
    if (path.rfind("dirA", 0) == 0)
        gate->callsDirA.fetch_add(1, std::memory_order_relaxed);
    else if (path.rfind("dirB", 0) == 0)
        gate->callsDirB.fetch_add(1, std::memory_order_relaxed);
    gate->entered.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return makeImageData(size, size, PixelFormat::RGB24);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

bool poolIdle()
{
    const auto m = TaskScheduler::instance().metrics(PoolType::ThumbnailPool);
    return m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0;
}

void testMidFlightResize()
{
    printf("\n[T1. mid-flight thumbnail resize supersedes old-size work]\n");
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "ThumbnailPool drained before T1");

    ThumbnailPipeline pipe;
    pipe.setPredictiveCount(0); // deterministic: only the visible window is scheduled
    auto gate = std::make_shared<DecodeGate>();
    pipe.setDecodeFn([gate](const std::string &p, int size) { return gatedDecode(gate, p, size); });
    std::vector<std::pair<int, std::string>> delivered; // (size, path)
    pipe.setResultFn([&](const std::string &p, int size, const ImageData &img)
                     {
                         if (!img.isNull())
                             delivered.push_back({size, p});
                     });

    pipe.setSources(makeSources("dirA", 20));
    pipe.setVisibleRange(0, 10); // schedules 10 decodes at thumbSize=256
    CHECK(waitTrue([&] { return gate->calls.load() >= 1; }, 5000),
          "old-size decode started");

    // Resize while decodes are in flight.
    pipe.setThumbSize(128);
    CHECK(pipe.generation() > 0, "resize bumped the generation");
    pipe.setVisibleRange(0, 10); // re-schedule at 128

    // Release the gate: old (256) and new (128) decodes all finish.
    gate->release.store(true, std::memory_order_release);
    CHECK(waitTrue([&] { return delivered.size() >= 10; }, 10000),
          "new-size results delivered");
    CHECK(waitTrue([&] { return pipe.handlesCount() == 0 && pipe.pendingCount() == 0; }, 10000),
          "pipeline bookkeeping converges to zero");

    bool onlyNewSize = true;
    for (const auto &d : delivered)
        if (d.first != 128)
            onlyNewSize = false;
    CHECK(onlyNewSize, "every delivered thumbnail is at the NEW size (no stale 256 delivery)");
    // The old-size decodes ran (they were already in flight) but their results
    // must not be cached: the memory cache holds exactly the 10 new-size keys.
    // (Pre-fix: the old-size results were cached AND delivered — 20 cached keys.)
    CHECK(pipe.memCacheSize() == 10, "memory cache holds only the new-size results");
    CHECK(waitTrue(poolIdle, 5000), "ThumbnailPool idle after the resize interleaving");
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(5)),
          "ThumbnailPool drains after T1");
}

void testRapidSizeChurn()
{
    printf("\n[T2. rapid size churn A->B->C->D with in-flight decodes]\n");
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "ThumbnailPool drained before T2");

    ThumbnailPipeline pipe;
    pipe.setPredictiveCount(0); // deterministic: only the visible window is scheduled
    auto gate = std::make_shared<DecodeGate>();
    pipe.setDecodeFn([gate](const std::string &p, int size) { return gatedDecode(gate, p, size); });
    std::atomic<int> size256Deliveries{0};
    std::atomic<int> size64Deliveries{0};
    std::atomic<int> finalDeliveries{0};
    pipe.setResultFn([&](const std::string &, int size, const ImageData &img)
                     {
                         if (img.isNull())
                             return;
                         if (size == 256)
                             size256Deliveries.fetch_add(1);
                         else if (size == 64)
                             size64Deliveries.fetch_add(1);
                         else
                             finalDeliveries.fetch_add(1);
                     });

    pipe.setSources(makeSources("dirB", 30));
    pipe.setVisibleRange(0, 10); // 256
    CHECK(waitTrue([&] { return gate->calls.load() >= 1; }, 5000),
          "churn: first decode started");
    pipe.setThumbSize(128); // supersede 256
    pipe.setThumbSize(64);  // supersede 128
    pipe.setThumbSize(128); // final size
    pipe.setVisibleRange(0, 10);

    gate->release.store(true, std::memory_order_release);
    CHECK(waitTrue([&] { return pipe.handlesCount() == 0 && pipe.pendingCount() == 0; }, 15000),
          "churn: pipeline converges");
    CHECK(size256Deliveries.load() == 0,
          "churn: no delivery from the superseded 256 size");
    CHECK(size64Deliveries.load() == 0,
          "churn: no delivery from the superseded 64 size");
    CHECK(finalDeliveries.load() == 10, "churn: exactly the final-size (128) batch delivers");
    CHECK(pipe.memCacheSize() == 10,
          "churn: memory cache holds only the final-size results");
    CHECK(waitTrue(poolIdle, 5000), "churn: ThumbnailPool idle");
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(5)),
          "ThumbnailPool drains after T2");
}

void testDirectorySwitchMidDecode()
{
    printf("\n[T3. setSources mid-decode drops every stale result]\n");
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "ThumbnailPool drained before T3");

    ThumbnailPipeline pipe;
    pipe.setPredictiveCount(0); // deterministic: only the visible window is scheduled
    auto gate = std::make_shared<DecodeGate>();
    pipe.setDecodeFn([gate](const std::string &p, int size) { return gatedDecode(gate, p, size); });
    std::atomic<int> dirADeliveries{0};
    std::atomic<int> dirBDeliveries{0};
    pipe.setResultFn([&](const std::string &p, int, const ImageData &img)
                     {
                         if (img.isNull())
                             return;
                         if (p.rfind("dirA", 0) == 0)
                             dirADeliveries.fetch_add(1);
                         else
                             dirBDeliveries.fetch_add(1);
                     });

    pipe.setSources(makeSources("dirA", 20));
    pipe.setVisibleRange(0, 5);
    CHECK(waitTrue([&] { return gate->calls.load() >= 1; }, 5000),
          "dirA decode started");
    // Switch directory mid-decode.
    pipe.setSources(makeSources("dirB", 20));
    pipe.setVisibleRange(0, 5);
    const int callsDirAAfterSwitch = gate->callsDirA.load();

    gate->release.store(true, std::memory_order_release);
    CHECK(waitTrue([&] { return pipe.handlesCount() == 0 && pipe.pendingCount() == 0; }, 10000),
          "switch: pipeline converges");
    CHECK(dirADeliveries.load() == 0, "switch: no stale dirA delivery");
    CHECK(dirBDeliveries.load() >= 5, "switch: current dirB deliveries present");
    CHECK(gate->callsDirA.load() == callsDirAAfterSwitch,
          "switch: no further stale dirA decode scheduled after the switch");
    CHECK(waitTrue(poolIdle, 5000), "switch: ThumbnailPool idle");
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(5)),
          "ThumbnailPool drains after T3");
}

void testIdleConvergenceAfterChurn()
{
    printf("\n[T4. idle convergence after churn]\n");
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(PoolType::ThumbnailPool, std::chrono::seconds(15)),
          "ThumbnailPool drained before T4");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0 && g.deferred == 0 && g.dep_graph_entries == 0 &&
              g.dependents_entries == 0,
          "scheduler dependency graph empty after churn");
    const auto m = sched.metrics(PoolType::ThumbnailPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "ThumbnailPool metrics at zero");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M46 ThumbnailPipeline supersession tests ===\n");
    fflush(stdout);

    testMidFlightResize();
    testRapidSizeChurn();
    testDirectorySwitchMidDecode();
    testIdleConvergenceAfterChurn();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
