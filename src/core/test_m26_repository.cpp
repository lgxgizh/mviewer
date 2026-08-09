// M26 Phase 0 — ImageRepository async completion contract tests.
//
// Contracts under test (Phase 4 hardening):
//   1. loadDirectoryAsync() must invoke its aggregate callback EXACTLY ONCE
//      even when the DecodePool is saturated and most submissions are
//      rejected — rejected items must become explicit failure Results, not
//      silently dropped.
//   2. Synchronous loadDirectory() must never busy-wait forever when the
//      scheduler is paused/saturated — every submission is accounted for, and
//      the call is bounded.
//   3. loadDirectory() must not permanently clobber a caller-configured queue
//      depth, and must not rely on flipping global scheduler configuration.
//
// Current (M25) behavior: rejected submissions are dropped silently so the
// aggregate callback can never fire; loadDirectory() temporarily sets the
// global queue depth to 0 (unlimited) and restores a hard-coded 1000,
// clobbering any configured value.

#include "core/cache/CacheManager.h"
#include "core/image/Encoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QThread>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
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
using Priority = TaskScheduler::Priority;

std::string makeDir(int count)
{
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    for (int i = 0; i < count; ++i)
    {
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path =
            tmp.path().toStdString() + "/img_" + std::to_string(i) + ".png";
        Encoder::encode(mvcore::fromQImage(img), path, Encoder::Params{});
    }
    return tmp.path().toStdString();
}

void cleanupDir(const std::string &dir)
{
    if (dir.empty())
        return;
    QDir qdir(QString::fromStdString(dir));
    qdir.removeRecursively();
}

struct Guard
{
    ~Guard()
    {
        auto &sched = TaskScheduler::instance();
        sched.setMaxQueueDepth(PoolType::DecodePool, 1000);
        sched.resume(PoolType::DecodePool);
        sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    }
};

struct SyncLoadState
{
    std::atomic<bool> returned{false};
    std::vector<ImageRepository::Result> results;
};

// ─── 1. Saturated pool: aggregate callback still fires exactly once ─────────
void testSaturatedAsyncCompletes()
{
    printf("\n[1. saturated DecodePool: async callback fires exactly once]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    Guard guard;

    sched.setPoolMaxThreads(PoolType::DecodePool, 1);
    sched.setMaxQueueDepth(PoolType::DecodePool, 2); // 1 active + 1 queued max

    // Occupy the single DecodePool thread so any directory task can only queue
    // (and beyond the depth limit, must be rejected).
    std::atomic<bool> unblock{false};
    sched.submit(Priority::Decode,
                 [&](const TaskScheduler::TaskContext &)
                 {
                     while (!unblock.load())
                         std::this_thread::sleep_for(std::chrono::milliseconds(2));
                 });

    const std::string dir = makeDir(8);
    ImageRepository &repo = ImageRepository::instance();
    std::atomic<bool> called{false};
    std::atomic<int> callCount{0};
    std::vector<ImageRepository::Result> results;
    std::atomic<bool> resultsReady{false};

    repo.loadDirectoryAsync(
        dir,
        [&](std::vector<ImageRepository::Result> r)
        {
            callCount.fetch_add(1);
            results = std::move(r);
            resultsReady = true;
            called = true;
        });

    // The aggregate callback must fire (exactly once) even though the pool is
    // saturated and 7 of 8 submissions are rejected. The one accepted task is
    // queued behind the blocker; once it runs, the count reaches n and the
    // callback must fire — rejected items carried as explicit failure Results,
    // never silently dropped.
    unblock = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!called.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(called.load(), "aggregate callback fires under pool saturation");
    CHECK(callCount.load() == 1, "aggregate callback fires EXACTLY once under saturation");
    CHECK(resultsReady.load() && results.size() == 8,
          "callback carries one Result per file (rejected items included)");
    int explicitFailures = 0;
    for (const auto &r : results)
        if (!r.success())
            ++explicitFailures;
    CHECK(explicitFailures >= 7, "rejected submissions become explicit failure Results");

    // Drain the blocker; the one accepted task now runs and completes.
    sched.drain(PoolType::DecodePool, std::chrono::milliseconds(8000));
    cleanupDir(dir);
}

// ─── 2. Sync loadDirectory is bounded when the pool is paused ───────────────
void testSyncLoadBoundedUnderPause()
{
    printf("\n[2. paused pool: sync loadDirectory is bounded, no busy-wait]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    Guard guard;

    sched.setPoolMaxThreads(PoolType::DecodePool, 1);
    const std::string dir = makeDir(6);
    ImageRepository &repo = ImageRepository::instance();

    sched.pause(PoolType::DecodePool);
    // Heap-owned state so a pre-fix zombie worker (infinite busy-wait) never
    // touches freed stack.
    auto state = std::make_shared<SyncLoadState>();
    std::thread worker(
        [state, &repo, dir]
        {
            state->results = repo.loadDirectory(dir, 6);
            state->returned = true;
        });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state->returned.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    CHECK(state->returned.load(), "sync loadDirectory returns while the pool is paused (bounded)");

    // Cleanup: post-fix the worker returned long ago (rejected -> error
    // results). Pre-fix it busy-waits forever — resume, give a bounded grace,
    // then detach so the test binary can finish and report.
    sched.resume(PoolType::DecodePool);
    const auto grace = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!state->returned.load() && std::chrono::steady_clock::now() < grace)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (worker.joinable())
    {
        if (state->returned.load())
            worker.join();
        else
            worker.detach();
    }
    CHECK(!state->results.empty(), "bounded sync load still produces a Result per file");
    cleanupDir(dir);
}

// ─── 3. Queue-depth configuration is preserved ──────────────────────────────
void testQueueDepthConfigPreserved()
{
    printf("\n[3. loadDirectory does not clobber configured queue depth]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    Guard guard;

    sched.setMaxQueueDepth(PoolType::DecodePool, 3);
    const std::string dir = makeDir(4);
    ImageRepository &repo = ImageRepository::instance();
    const std::vector<ImageRepository::Result> results = repo.loadDirectory(dir, 4);

    CHECK(results.size() == 4, "sync loadDirectory completes normally");
    CHECK(sched.maxQueueDepth(PoolType::DecodePool) == 3,
          "caller-configured queue depth preserved after loadDirectory");
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0, "DecodePool metrics converge after sync load");
    cleanupDir(dir);
}

// ─── 4. Empty directory: callback fires exactly once (guard) ────────────────
void testEmptyDirCallbackOnce()
{
    printf("\n[4. empty directory: aggregate callback exactly once]\n");
    fflush(stdout);
    Guard guard;
    const std::string dir = makeDir(0);
    ImageRepository &repo = ImageRepository::instance();

    std::atomic<int> calls{0};
    repo.loadDirectoryAsync(dir, [&](std::vector<ImageRepository::Result> r)
                            {
                                calls.fetch_add(1);
                                CHECK(r.empty(), "empty directory yields empty results");
                            });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(calls.load() == 1, "empty-directory callback fires exactly once");
    cleanupDir(dir);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M26 ImageRepository async completion tests ===\n");
    fflush(stdout);

    testSaturatedAsyncCompletes();
    testSyncLoadBoundedUnderPause();
    testQueueDepthConfigPreserved();
    testEmptyDirCallbackOnce();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
