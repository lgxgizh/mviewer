// M27 Phase 3 — ImageRepository timeout / rejection safety regression tests.
//
// Contracts under test (must hold after Phase 3 hardening):
//   1. loadAsync() must NEVER silently lose a request: on scheduler rejection
//      the callback fires EXACTLY ONCE, on the calling thread, with an
//      explicit rejection error.
//   2. loadDirectory() must honor its defensive budget (wall clock): it must
//      return within the budget even when accepted Decode tasks are stuck
//      behind a blocked pool.
//   3. loadDirectory() timeout must not leave late workers writing freed
//      stack state — worker state must use safe shared ownership and
//      outstanding work must be cancelled on timeout.
//   4. Late completions (workers resuming after the caller returned) must be
//      safe: no crash, no UAF, scheduler converges, results are explicit
//      failures.
//   5. Normal (no-timeout) directory load still succeeds.
//
// Pre-fix (M26) failures reproduced here: #1 (callback never fires on
// rejection), #2/#3/#4 (stack-reference capture; budget is a hard-coded
// 5-minute constant so the hazard cannot even be exercised).

#include "core/image/Encoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
using TaskContext = TaskScheduler::TaskContext;

std::string makeDir(int count)
{
    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    for (int i = 0; i < count; ++i)
    {
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(QColor((i * 7) % 256, (i * 13) % 256, (i * 29) % 256));
        const std::string path = tmp.path().toStdString() + "/img_" + std::to_string(i) + ".png";
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

// Reuses the (potentially freed) caller stack region while late workers fire.
volatile std::uint64_t g_churnSink = 0;
void churnStack()
{
    volatile char buf[65536];
    std::memset(const_cast<char *>(buf), 0xAA, sizeof(buf));
    g_churnSink += static_cast<std::uint64_t>(buf[4096]);
}

// ─── 1. loadAsync rejection: exactly-once callback with explicit error ─────
void testLoadAsyncRejection()
{
    printf("\n[1. loadAsync: rejection fires the callback exactly once]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.pause(PoolType::DecodePool);
    std::atomic<int> calls{0};
    std::string lastError;
    repo.loadAsync("C:/definitely/missing/file.png",
                   [&](const ImageRepository::Result &r)
                   {
                       calls.fetch_add(1);
                       lastError = r.error;
                   });
    repo.loadAsync("C:/definitely/missing/file2.png",
                   [&](const ImageRepository::Result &r)
                   {
                       calls.fetch_add(1);
                       lastError = r.error;
                   });
    // Callback must have fired synchronously on this (calling) thread.
    CHECK(calls.load() == 2, "each rejected request fires its callback exactly once");
    CHECK(lastError.find("rejected") != std::string::npos,
          "rejection error is explicit about the scheduler rejection");
    sched.resume(PoolType::DecodePool);
}

// ─── 2. loadAsync normal success path ──────────────────────────────────────
void testLoadAsyncSuccess()
{
    printf("\n[2. loadAsync: success path still delivers]\n");
    fflush(stdout);
    auto &repo = ImageRepository::instance();

    const std::string dir = makeDir(1);
    const std::string path = dir + "/img_0.png";
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    repo.loadAsync(path,
                   [&](const ImageRepository::Result &r)
                   {
                       ok.store(r.success());
                       done.store(true);
                   });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(done.load(), "async success callback fires");
    CHECK(ok.load(), "async load succeeds on a real file");
    cleanupDir(dir);
}

// ─── 3. loadDirectory timeout: budget honored, late completion safe ────────
void testLoadDirectoryTimeoutLateCompletion()
{
    printf("\n[3. loadDirectory: budget honored + late completion is safe]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    auto &repo = ImageRepository::instance();

    sched.setPoolMaxThreads(PoolType::DecodePool, 1);
    repo.setSyncLoadBudget(std::chrono::milliseconds(400));
    const std::string dir = makeDir(8);

    // Saturate the single Decode worker: loadDirectory's submissions are
    // ACCEPTED (queued) but cannot start for 3 s — longer than the budget.
    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::seconds(3)); });

    const auto t0 = std::chrono::steady_clock::now();
    auto results = repo.loadDirectory(dir, 1000);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    printf("  loadDirectory returned in %lld ms\n",
           static_cast<long long>(
               std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
    CHECK(elapsed < std::chrono::seconds(2),
          "loadDirectory returns within the budget (does not wait for blocked tasks)");

    int timeoutCount = 0;
    for (const auto &r : results)
        if (!r.success() && r.error.find("timed out") != std::string::npos)
            ++timeoutCount;
    CHECK(timeoutCount >= 1, "timed-out items carry explicit failure results");

    // Late-completion window: churn the stack (reusing the region that used to
    // hold loadDirectory's stack locals) while the queued tasks eventually run
    // after the blocker finishes. Pre-fix this is a UAF write; post-fix the
    // worker state lives on the heap and the tasks exit via cancellation.
    const auto churnUntil = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < churnUntil)
    {
        for (int i = 0; i < 64; ++i)
            churnStack();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "scheduler converges after late completion");
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live handles after late completion");

    // Recover global state for subsequent tests.
    repo.setSyncLoadBudget(std::chrono::milliseconds(5 * 60 * 1000));
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    cleanupDir(dir);
}

// ─── 4. loadDirectory normal completion sanity ─────────────────────────────
void testLoadDirectoryNormal()
{
    printf("\n[4. loadDirectory: normal completion sanity]\n");
    fflush(stdout);
    auto &repo = ImageRepository::instance();

    const std::string dir = makeDir(4);
    auto results = repo.loadDirectory(dir, 1000);
    int ok = 0;
    for (const auto &r : results)
        ok += r.success() ? 1 : 0;
    CHECK(ok == 4, "all 4 images load successfully without a timeout");
    cleanupDir(dir);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M27 ImageRepository timeout/rejection tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    testLoadAsyncRejection();
    testLoadAsyncSuccess();
    testLoadDirectoryTimeoutLateCompletion();
    testLoadDirectoryNormal();

    // Leave the scheduler clean.
    sched.resume(PoolType::DecodePool);
    sched.setMaxQueueDepth(PoolType::DecodePool, 1000);
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
