// M26 Phase 0 — TaskScheduler RC reliability regression tests.
//
// Contracts under test (must hold after Phase 1 hardening):
//   1. A task whose deadline has already expired when it starts must NOT run
//      its work, MUST finalize its lifecycle (handle removed, metrics back to
//      zero), and MUST report deadline_exceeded.
//   2. Dependency chains run strictly in order; deferred (waiting) tasks must
//      move through waiting -> running -> terminal with correct metrics at
//      every state; no size_t underflow on pending/active/queue_depth.
//   3. cancelTree(root) cancels root + all TRANSITIVE DEPENDENTS (tasks that
//      depend on root), and must never cancel a task's prerequisites.
//   4. Rejected submissions (paused / saturated) leave no pending state and
//      the caller sees failure (null handle).
//   5. The done/progress callbacks run on the scheduler worker thread — never
//      on the submitting (main) thread; scheduler bookkeeping completes
//      exactly once per task.
//
// Current (M25) behavior: several of these FAIL — deferred tasks never
// increment `pending` so completing them underflows the counter; cancelTree
// walks the prerequisite map (cancelling parents instead of children);
// deadline-expired tasks never finalize; the deadline_exceeded metric is
// never incremented.

#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
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
using Priority = TaskScheduler::Priority;
using PoolType = TaskScheduler::PoolType;
using TaskContext = TaskScheduler::TaskContext;

const auto kDrain = std::chrono::milliseconds(3000);

// ─── 1. Deadline expired before the task starts ─────────────────────────────
void testDeadlineExpiredBeforeStart()
{
    printf("\n[1. deadline expired before start]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> ran{false};
    std::atomic<bool> done{false};
    auto h = sched.submit(
        Priority::Background,
        [&](const TaskContext &) { ran = true; },
        {},
        std::chrono::steady_clock::now() - std::chrono::seconds(1), // already expired
        [&]() { done = true; });
    CHECK(h != nullptr, "submission accepted");

    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(!ran.load(), "expired task never runs its work");
    CHECK(done.load(), "expired task still finalizes (done fires exactly once)");

    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending returns to 0 after deadline task");
    CHECK(m.active_tasks == 0, "active_tasks returns to 0 after deadline task");
    CHECK(m.queue_depth == 0, "queue_depth returns to 0 after deadline task");
    CHECK(m.deadline_exceeded >= 1, "deadline_exceeded metric incremented");
    CHECK(sched.handle(h->id) == nullptr, "expired task handle is removed (no residue)");
}

// ─── 2. Dependency chain: order + metrics at every state ────────────────────
void testDependencyChainMetrics()
{
    printf("\n[2. dependency chain A->B->C]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::vector<std::string> order;
    std::mutex mtx;
    auto a = sched.submit(
        Priority::Background,
        [&](const TaskContext &)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            std::lock_guard<std::mutex> lk(mtx);
            order.push_back("A");
        });
    auto b = sched.submit(
        Priority::Background,
        [&](const TaskContext &)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> lk(mtx);
            order.push_back("B");
        },
        {a->id});
    auto c = sched.submit(
        Priority::Background,
        [&](const TaskContext &)
        {
            std::lock_guard<std::mutex> lk(mtx);
            order.push_back("C");
        },
        {b->id});

    CHECK(sched.handle(b->id) != nullptr, "deferred task has a live handle while waiting");
    sched.drain(PoolType::MetadataPool, kDrain);
    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK(order.size() == 3, "all three tasks executed");
        CHECK(order.size() == 3 && order[0] == "A" && order[1] == "B" && order[2] == "C",
              "A -> B -> C strictly ordered");
    }
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending == 0 after chain (no deferred underflow)");
    CHECK(m.active_tasks == 0, "active_tasks == 0 after chain");
    CHECK(m.queue_depth == 0, "queue_depth == 0 after chain");
    CHECK(m.completed >= 3, "completed counts the three tasks");
}

// ─── 2b. Deferred task released AT SUBMIT time (all deps already done) ──────
void testDeferredReleasedAtSubmit()
{
    printf("\n[2b. deferred task whose deps are already complete]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    // A finishes before B is submitted; B's release happens inside submit().
    auto a = sched.submit(Priority::Background, [](const TaskContext &) {});
    sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(500));

    std::atomic<bool> bRan{false};
    auto b = sched.submit(Priority::Background, [&](const TaskContext &) { bRan = true; },
                          {a->id});
    CHECK(b != nullptr, "dependent submitted");
    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(bRan.load(), "dependent with satisfied deps still runs");
    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending == 0 (no underflow from submit-time release)");
    CHECK(m.active_tasks == 0, "active_tasks == 0 (no underflow from submit-time release)");
    CHECK(m.queue_depth == 0, "queue_depth == 0");
}

// ─── 3. cancelTree cancels transitive dependents, never prerequisites ───────
void testCancelTreeCancelsDependents()
{
    printf("\n[3. cancelTree(A) cancels transitive dependents B and C]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> aRan{false};
    std::atomic<bool> bRan{false};
    std::atomic<bool> cRan{false};
    auto a = sched.submit(
        Priority::Background,
        [&](const TaskContext &ctx)
        {
            for (int i = 0; i < 100 && !ctx.isCancelled(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            aRan = true;
        });
    auto b = sched.submit(Priority::Background, [&](const TaskContext &) { bRan = true; },
                          {a->id});
    auto c = sched.submit(Priority::Background, [&](const TaskContext &) { cRan = true; },
                          {b->id});

    // B and C must be deferred (waiting) here — submit was microseconds ago.
    CHECK(sched.handle(b->id) != nullptr && sched.handle(c->id) != nullptr,
          "B and C are live deferred tasks");
    TaskScheduler::cancelTree(a->id);

    CHECK(a->isCancelled(), "root A cancelled");
    CHECK(b->isCancelled(), "dependent B cancelled by cancelTree(A)");
    CHECK(c->isCancelled(), "transitive dependent C cancelled by cancelTree(A)");

    sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(8000));
    CHECK(!bRan.load(), "dependent B work never runs");
    CHECK(!cRan.load(), "transitive dependent C work never runs");

    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending == 0 after cancelTree subtree");
    CHECK(m.active_tasks == 0, "active_tasks == 0 after cancelTree subtree");
    CHECK(m.queue_depth == 0, "queue_depth == 0 after cancelTree subtree");
    CHECK(m.cancelled >= 3, "cancelled metric counts the whole subtree");
    CHECK(sched.handle(b->id) == nullptr && sched.handle(c->id) == nullptr,
          "no residual handles in the cancelled subtree");

    // Cleanup for the CURRENT buggy implementation: cancelTree(A) leaves B and
    // C as stale deferred entries; a later submit() releases them and runs
    // lambdas whose stack captures are long gone (use-after-free — the crash
    // this suite reproduces when the next test submits). Delete them here so
    // the test binary can finish; the FIXED scheduler deletes them inside
    // cancelTree and this cleanup becomes a no-op.
    TaskScheduler::cancelTree(b->id);
    TaskScheduler::cancelTree(c->id);
}

// ─── 3b. cancelTree(B) must NOT cancel B's prerequisite A ───────────────────
void testCancelTreeDoesNotCancelPrerequisites()
{
    printf("\n[3b. cancelTree(B) leaves prerequisite A alone]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> aRan{false};
    auto a = sched.submit(
        Priority::Background,
        [&](const TaskContext &ctx)
        {
            for (int i = 0; i < 100 && !ctx.isCancelled(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            aRan = true;
        });
    // Regression: a previous cancelTree (3) must not poison Background-pool
    // metrics so badly that this submit gets silently rejected. Pre-fix,
    // cancelTree on deferred tasks underflowed active_tasks/queue_depth to
    // SIZE_MAX -> every later submit was REJECTED -> callers saw nullptr.
    CHECK(a != nullptr, "submit accepted after previous cancelTree (metrics not poisoned)");
    if (!a)
        return;
    printf("  3b: submitting B\n"); fflush(stdout);
    std::atomic<bool> bRan{false};
    auto b = sched.submit(Priority::Background, [&](const TaskContext &) { bRan = true; },
                          {a->id});

    TaskScheduler::cancelTree(b->id); // must cancel ONLY B
    CHECK(!a->isCancelled(), "prerequisite A is NOT cancelled by cancelTree(B)");
    CHECK(b->isCancelled(), "B itself is cancelled");

    sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(8000));
    CHECK(aRan.load(), "prerequisite A still completes normally");
    CHECK(!bRan.load(), "B work never runs");

    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending == 0 after prerequisite-safe cancelTree");
    CHECK(m.active_tasks == 0, "active_tasks == 0 after prerequisite-safe cancelTree");
    CHECK(m.queue_depth == 0, "queue_depth == 0 after prerequisite-safe cancelTree");
    CHECK(m.cancelled >= 1, "cancelled metric counts B");
    CHECK(sched.handle(b->id) == nullptr, "B handle removed");
}

// ─── 3c. cancelTree on a deferred task (still waiting) ──────────────────────
void testCancelTreeDeferredTask()
{
    printf("\n[3c. cancelTree on a waiting (deferred) task]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> xRan{false};
    auto x = sched.submit(
        Priority::Background,
        [&](const TaskContext &ctx)
        {
            for (int i = 0; i < 100 && !ctx.isCancelled(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            xRan = true;
        });
    CHECK(x != nullptr, "submit(X) accepted despite earlier cancelTree residue (metrics not poisoned)");
    if (!x)
        return;
    std::atomic<bool> dRan{false};
    auto d = sched.submit(Priority::Background, [&](const TaskContext &) { dRan = true; },
                          {x->id});

    CHECK(sched.handle(d->id) != nullptr, "D is live and waiting on X");
    TaskScheduler::cancelTree(d->id);
    CHECK(d->isCancelled(), "waiting task D cancelled");
    CHECK(!x->isCancelled(), "X (prerequisite) untouched");

    sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(8000));
    CHECK(!dRan.load(), "cancelled waiting task never runs");
    CHECK(xRan.load(), "prerequisite X completes normally");

    const auto m = sched.metrics(PoolType::MetadataPool);
    CHECK(m.pending == 0, "pending == 0 (waiting-task cancel decrements exactly once)");
    CHECK(m.active_tasks == 0, "active_tasks == 0 (no underflow from waiting-task cancel)");
    CHECK(m.queue_depth == 0, "queue_depth == 0 (no underflow from waiting-task cancel)");
    CHECK(m.cancelled >= 1, "cancelled metric counts D");
    CHECK(sched.handle(d->id) == nullptr, "D handle removed");
}

// ─── 4. Rejection leaves no pending state ───────────────────────────────────
void testRejectedSubmitLeavesNoPending()
{
    printf("\n[4. paused pool rejects submission without residue]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<int> backpressureCalls{0};
    sched.setBackPressureHandler([&](PoolType) { backpressureCalls.fetch_add(1); });

    sched.pause(PoolType::DecodePool);
    std::atomic<bool> ran{false};
    auto h = sched.submit(Priority::Decode, [&](const TaskContext &) { ran = true; }, {},
                          std::chrono::steady_clock::time_point::max(), [&]() {});
    CHECK(h == nullptr, "submit returns nullptr while paused");
    CHECK(backpressureCalls.load() >= 1, "back-pressure handler notified");

    const auto mWhilePaused = sched.metrics(PoolType::DecodePool);
    CHECK(mWhilePaused.pending == 0, "rejected task leaves pending == 0");
    CHECK(mWhilePaused.active_tasks == 0, "rejected task leaves active_tasks == 0");

    sched.resume(PoolType::DecodePool);
    CHECK(sched.drain(PoolType::DecodePool, kDrain), "drain is bounded and succeeds");
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "DecodePool metrics converge to zero after resume + drain");
    CHECK(m.backpressure_rejected >= 1, "backpressure_rejected metric recorded");
    sched.setBackPressureHandler(nullptr);
}

// ─── 5. Callback thread contract ────────────────────────────────────────────
void testCallbackThreadContract()
{
    printf("\n[5. done/progress callbacks run on the worker, never the submitter]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    const auto mainTid = std::this_thread::get_id();
    std::atomic<std::thread::id> workTid{};
    std::atomic<std::thread::id> doneTid{};
    std::atomic<std::thread::id> progressTid{};
    std::atomic<bool> done{false};

    auto h = sched.submit(
        Priority::Analysis,
        [&](const TaskContext &ctx)
        {
            workTid = std::this_thread::get_id();
            const_cast<TaskContext &>(ctx).reportProgress(50);
            const_cast<TaskContext &>(ctx).reportProgress(100);
        },
        {},
        std::chrono::steady_clock::time_point::max(),
        [&]()
        {
            doneTid = std::this_thread::get_id();
            done = true;
        },
        [&](int) { progressTid = std::this_thread::get_id(); });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    CHECK(done.load(), "done callback fired");
    CHECK(workTid.load() != mainTid, "work runs off the submitting thread");
    CHECK(doneTid.load() == workTid.load(), "done callback runs on the worker thread");
    CHECK(progressTid.load() == workTid.load(), "progress callback runs on the worker thread");

    sched.drain(PoolType::AnalysisPool, kDrain);
    const auto m = sched.metrics(PoolType::AnalysisPool);
    CHECK(m.pending == 0 && m.active_tasks == 0 && m.queue_depth == 0,
          "AnalysisPool metrics converge to zero");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== M26 Scheduler reliability tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    // Deterministic ordering: one Background worker.
    sched.setQueueMaxThreads(Priority::Background, 1);
    sched.setQueueMaxThreads(Priority::Analysis, 1);

    testDeadlineExpiredBeforeStart();
    testDependencyChainMetrics();
    testDeferredReleasedAtSubmit();
    testCancelTreeCancelsDependents();
    testCancelTreeDoesNotCancelPrerequisites();
    testCancelTreeDeferredTask();
    testRejectedSubmitLeavesNoPending();
    testCallbackThreadContract();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
