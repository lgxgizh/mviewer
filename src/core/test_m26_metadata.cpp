// M26 Phase 0 — MetadataIndexer multi-consumer regression tests.
//
// Contract under test (Phase 2 hardening):
//   Two independent consumers (MainWindow search re-index, ThumbnailPanel
//   camera/lens filter) index the SAME directory concurrently. Neither may
//   silently cancel the other: both must receive their own completion and
//   entries. Superseding is only legal for a *new request from the same
//   consumer for the same directory generation*.
//
// Current (M25) behavior: index() bumps a single global generation and
// cancels the previous handle unconditionally — consumer B's request cancels
// consumer A's in-flight request, and A's onDone is silently dropped.

#include "core/metadata/MetadataIndexer.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
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

using Entry = mviewer::core::MetadataIndexer::Entry;

// Pump the event loop so queued (main-thread-marshaled) callbacks can land.
void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

// Wait until `flag` is true or `ms` elapses.
bool waitFor(std::atomic<bool> &flag, int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
    {
        pump(5);
        if (flag.load())
            return true;
    }
    return flag.load();
}

std::vector<std::string> makeDngs(QTemporaryDir &tmp, int count)
{
    std::vector<std::string> paths;
    paths.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const std::string p =
            tmp.path().toStdString() + "/img_" + std::to_string(i) + ".dng";
        QFile f(QString::fromStdString(p));
        f.open(QIODevice::WriteOnly);
        f.close();
        paths.push_back(p);
    }
    return paths;
}

// Consumer A: MainWindow search re-index style (no onEntry, only onDone).
// Consumer B: ThumbnailPanel filter style (onEntry per file + onDone).
// B starts while A is still in flight — the bug is A's completion being
// silently dropped.
void testDualConsumerNoSilentCancel()
{
    printf("\n[dual consumers: B must not silently cancel A]\n");
    fflush(stdout);
    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel();

    QTemporaryDir tmp;
    // Enough files that consumer A's single background pass is still running
    // when consumer B's request lands microseconds later.
    const std::vector<std::string> all = makeDngs(tmp, 1200);
    const std::vector<std::string> sub(all.begin(), all.begin() + 700);

    // Consumer A (search): index the full set.
    std::atomic<bool> aDone{false};
    indexer.index(all, {}, [&]() { aDone = true; });

    // Consumer B (filter): starts immediately, supersedes A's generation.
    std::atomic<int> bEntries{0};
    std::atomic<bool> bDone{false};
    indexer.index(
        sub,
        [&](const Entry &e)
        {
            if (e.path.empty())
                CHECK(false, "consumer B entry carries a path");
            bEntries.fetch_add(1);
        },
        [&]() { bDone = true; });

    const bool bFinished = waitFor(bDone, 15000);
    const bool aFinished = waitFor(aDone, 15000);
    CHECK(bFinished, "consumer B receives its own completion");
    CHECK(bEntries.load() == 700, "consumer B receives exactly its 700 entries");
    CHECK(aFinished, "consumer A completion NOT silently dropped by B's request");

    pump(500);
    // The shared cache must serve both consumers' paths (work is reused).
    int cachedA = 0;
    for (const auto &p : all)
        if (indexer.cached(p).has_value())
            ++cachedA;
    CHECK(cachedA == static_cast<int>(all.size()),
          "shared cache holds every indexed path (work reused, not discarded)");
}

// Reverse interleaving: consumer A starts after B and must not cancel B.
void testDualConsumerReverseOrder()
{
    printf("\n[dual consumers: A must not silently cancel B]\n");
    fflush(stdout);
    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel();

    QTemporaryDir tmp;
    const std::vector<std::string> all = makeDngs(tmp, 1200);

    // Consumer B (filter) first.
    std::atomic<int> bEntries{0};
    std::atomic<bool> bDone{false};
    indexer.index(
        all,
        [&](const Entry &) { bEntries.fetch_add(1); },
        [&]() { bDone = true; });

    // Consumer A (search) second, same directory.
    std::atomic<bool> aDone{false};
    indexer.index(all, {}, [&]() { aDone = true; });

    const bool aFinished = waitFor(aDone, 15000);
    CHECK(aFinished, "consumer A receives its own completion");
    CHECK(bDone.load(), "consumer B completion NOT silently dropped by A's request");
    CHECK(bEntries.load() == 1200, "consumer B receives exactly its 1200 entries");
}

// A request superseded by the SAME consumer's new directory request is
// allowed to be cancelled — but the second request must still complete.
void testSameConsumerSupersedeCompletes()
{
    printf("\n[same consumer: newer request wins, older one may drop]\n");
    fflush(stdout);
    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel();

    QTemporaryDir tmp;
    const std::vector<std::string> a = makeDngs(tmp, 800);

    std::atomic<bool> firstDone{false};
    const uint64_t firstReq = indexer.index(a, {}, [&]() { firstDone = true; });
    CHECK(firstReq != 0, "first request accepted");
    std::atomic<bool> secondDone{false};
    std::atomic<int> secondEntries{0};
    const uint64_t secondReq = indexer.index(
        a,
        [&](const Entry &) { secondEntries.fetch_add(1); },
        [&]() { secondDone = true; });
    CHECK(secondReq != 0, "second request accepted");

    CHECK(waitFor(secondDone, 15000), "the newer same-consumer request completes");
    CHECK(secondEntries.load() == 800, "newer request delivers all its entries");
}

// ─── cancelRequest: superseding ONE request drops ONLY that request ────────
void testCancelRequestIsolation()
{
    printf("\n[same consumer may supersede its own stale request only]\n");
    fflush(stdout);
    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel();

    // Make the supersede timing DETERMINISTIC (no wall-clock assumptions):
    // pin the Background pool to one worker and occupy it with a blocker that
    // only returns after this test releases it. Both index requests are then
    // guaranteed to be queued (never started) when cancelRequest(stale) runs,
    // so its token is observed before the worker can deliver a completion.
    // Under parallel CTest load the previous time-based blocker (300 ms)
    // could expire while the 1000-file fixture set was still being written,
    // letting the stale request finish first and fail spuriously.
    auto &sched = TaskScheduler::instance();
    // Background pool default is max(1, idealThreadCount / 2); pin to one
    // worker for this test and restore afterwards.
    const int restoreThreads = std::max(1, QThread::idealThreadCount() / 2);
    sched.setPoolMaxThreads(TaskScheduler::PoolType::MetadataPool, 1);

    // Fixtures FIRST — the slow part must not run while the worker is
    // supposed to be occupied by the blocker.
    QTemporaryDir tmp;
    const std::vector<std::string> a = makeDngs(tmp, 500);
    QTemporaryDir tmp2;
    const std::vector<std::string> b = makeDngs(tmp2, 500);

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};
    const auto blocker = sched.submit(
        TaskScheduler::Priority::Background,
        [&blockerStarted, &releaseBlocker](const TaskScheduler::TaskContext &)
        {
            blockerStarted.store(true, std::memory_order_release);
            while (!releaseBlocker.load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
    if (!blocker)
    {
        CHECK(false, "blocker accepted (Background pool not paused/saturated)");
        sched.setPoolMaxThreads(TaskScheduler::PoolType::MetadataPool, restoreThreads);
        return;
    }
    // Wait until the blocker genuinely owns the single worker.
    const auto startedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blockerStarted.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < startedDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(blockerStarted.load(std::memory_order_acquire), "blocker occupies the single worker");

    std::atomic<bool> staleDone{false};
    const uint64_t stale = indexer.index(a, {}, [&]() { staleDone = true; });
    CHECK(stale != 0, "stale request accepted");

    // A concurrent INDEPENDENT consumer (different directory) must complete.
    std::atomic<bool> otherDone{false};
    const uint64_t other = indexer.index(b, {}, [&]() { otherDone = true; });
    CHECK(other != 0, "independent request accepted");

    // The blocker still owns the only worker, so BOTH requests are queued.
    // Supersede ONLY the stale request; its token is observed before the
    // worker ever starts it, so its completion can never fire.
    indexer.cancelRequest(stale);
    releaseBlocker.store(true, std::memory_order_release);
    CHECK(waitFor(otherDone, 15000), "independent consumer completes despite the cancel");
    pump(1500);
    CHECK(!staleDone.load(), "superseded request never delivers its completion");

    sched.setPoolMaxThreads(TaskScheduler::PoolType::MetadataPool, restoreThreads);
}

// ─── Bounded cache + value-semantics reads ──────────────────────────────────
void testCacheBoundAndValueSemantics()
{
    printf("\n[bounded cache + stable value reads]\n");
    fflush(stdout);
    auto &indexer = mviewer::core::MetadataIndexer::instance();
    indexer.cancel();
    const size_t savedLimit = indexer.cacheLimit();
    indexer.setCacheLimit(100);

    QTemporaryDir tmp;
    const std::vector<std::string> a = makeDngs(tmp, 300);

    std::atomic<bool> done{false};
    const uint64_t req = indexer.index(a, {}, [&]() { done = true; });
    CHECK(req != 0, "index request accepted");
    CHECK(waitFor(done, 15000), "index completes");

    // Bound: after indexing 300 distinct paths with a 100-entry budget, only
    // the most recent 100 may remain.
    CHECK(indexer.size() <= 100, "cache stays within its configured bound");

    // Value semantics: a copy obtained before another index pass remains valid
    // and unchanged even while the cache grows/churns underneath.
    const std::optional<Entry> snapshot = indexer.cached(a.front());
    std::atomic<bool> done2{false};
    QTemporaryDir tmp2;
    const std::vector<std::string> b = makeDngs(tmp2, 200);
    const uint64_t req2 = indexer.index(b, {}, [&]() { done2 = true; });
    CHECK(req2 != 0, "second index accepted");
    CHECK(waitFor(done2, 15000), "second index completes");
    CHECK(indexer.size() <= 100, "cache stays bounded across index passes");
    if (snapshot)
        CHECK(!snapshot->path.empty(), "snapshot copy remains readable after cache churn");

    indexer.setCacheLimit(savedLimit);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M26 MetadataIndexer dual-consumer tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(TaskScheduler::Priority::Background, 2);

    testDualConsumerNoSilentCancel();
    testDualConsumerReverseOrder();
    testSameConsumerSupersedeCompletes();
    testCancelRequestIsolation();
    testCacheBoundAndValueSemantics();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
