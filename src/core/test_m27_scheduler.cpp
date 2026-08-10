// M27 Phase 0 — TaskScheduler fault-injection regression tests.
//
// Contracts under test (must hold after Phase 1/2 hardening):
//   1. work throwing std::exception must NOT escape the worker (process survives).
//   2. work throwing a non-std::exception type must NOT escape the worker.
//   3. onProgress throwing must NOT escape the worker (normal + deadline paths).
//   4. user done throwing is contained (already true since M26).
//   5. empty work is rejected at submit (nullptr), never a bad_function_call
//      on a Release worker.
//   6. cancelTree victims (waiting / pool-queued / running) NEVER get their
//      user done callback.
//   7. cancel(handle) soft cancel: work skipped, done STILL fires.
//   8. after exception/cancel storms every pool converges: pending == 0,
//      waiting == 0, active_tasks == 0, queue_depth == 0, no handle residue.
//   9. dependency / reverse-dependency graph working-set returns to zero after
//      heavy task churn (no growth with historical task count).
//  10. drain(timeout) wall clock <= ~1.5x timeout (no accidental 2x wait).
//  11. throwing backpressure handler cannot escape submit (returns nullptr).
//  12. execution failures observable via metrics (execution_failures).
//
// Pre-fix (M26) reproduced failures: 1, 2, 3, 5, 6 (queued+running), 8
// (dependents entries), 9 (drain wall clock), 11.
//
// Throwing-work scenarios run in a child process (pre-fix the child aborts,
// so the parent observes the crash and reports FAIL); in-process they would
// kill the whole suite.

#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QProcess>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

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

const auto kDrain = std::chrono::milliseconds(5000);

bool allPoolsConverged(TaskScheduler &sched)
{
    for (int p = 0; p < 5; ++p)
    {
        const auto m = sched.metrics(static_cast<PoolType>(p));
        if (m.pending != 0 || m.waiting != 0 || m.active_tasks != 0 || m.queue_depth != 0)
            return false;
    }
    const auto g = sched.graphMetrics();
    return g.handles == 0 && g.deferred == 0;
}

// ─── child scenarios (run in a fresh process so a pre-fix abort is visible) ─

// work throws std::exception.
int childWorkThrowStd()
{
    auto &sched = TaskScheduler::instance();
    sched.submit(Priority::Background,
                 [](const TaskContext &) { throw std::runtime_error("work boom"); });
    if (!sched.drain(PoolType::MetadataPool, std::chrono::seconds(10)))
        return 2; // never finalized
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 3; // bookkeeping did not converge
    if (m.execution_failures < 1)
        return 4; // failure not observable
    printf("  child: work-throw-std survived and converged\n");
    return 0;
}

// work throws a non-std::exception type (int).
int childWorkThrowInt()
{
    auto &sched = TaskScheduler::instance();
    sched.submit(Priority::Background, [](const TaskContext &) { throw 42; });
    if (!sched.drain(PoolType::MetadataPool, std::chrono::seconds(10)))
        return 2;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 3;
    if (m.execution_failures < 1)
        return 4;
    printf("  child: work-throw-int survived and converged\n");
    return 0;
}

// onProgress throws inside the work callback (normal path).
int childProgressThrow()
{
    auto &sched = TaskScheduler::instance();
    std::atomic<bool> done{false};
    sched.submit(
        Priority::Background,
        [](const TaskContext &ctx) { const_cast<TaskContext &>(ctx).reportProgress(50); }, {},
        std::chrono::steady_clock::time_point::max(), [&]() { done = true; },
        [](int) { throw std::runtime_error("progress boom"); });
    if (!sched.drain(PoolType::MetadataPool, std::chrono::seconds(10)))
        return 2;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 3;
    if (!done.load())
        return 4; // task must still complete
    printf("  child: progress-throw survived and task completed\n");
    return 0;
}

// onProgress throws on the deadline-exceeded path.
int childDeadlineProgressThrow()
{
    auto &sched = TaskScheduler::instance();
    sched.submit(
        Priority::Background,
        [](const TaskContext &) { throw std::runtime_error("should never run"); }, {},
        std::chrono::steady_clock::now() - std::chrono::seconds(1), []() {},
        [](int) { throw std::runtime_error("progress boom"); });
    if (!sched.drain(PoolType::MetadataPool, std::chrono::seconds(10)))
        return 2;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 3;
    if (m.deadline_exceeded < 1)
        return 4;
    printf("  child: deadline-progress-throw survived and finalized\n");
    return 0;
}

// user done throws — must be contained (baseline, already safe since M26).
int childDoneThrow()
{
    auto &sched = TaskScheduler::instance();
    sched.submit(
        Priority::Background, [](const TaskContext &) {}, {},
        std::chrono::steady_clock::time_point::max(),
        []() { throw std::runtime_error("done boom"); });
    if (!sched.drain(PoolType::MetadataPool, std::chrono::seconds(10)))
        return 2;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 3;
    if (m.callback_failures < 1)
        return 4;
    printf("  child: done-throw contained\n");
    return 0;
}

// empty work must be rejected at submit (nullptr), never reach a worker.
int childEmptyWork()
{
    auto &sched = TaskScheduler::instance();
    std::function<void(const TaskContext &)> empty;
    auto h = sched.submit(Priority::Background, empty);
    if (h != nullptr)
    {
        printf("  child: empty work was ACCEPTED (violates contract)\n");
        // still drain: pre-fix the worker would die from bad_function_call.
        sched.drain(PoolType::MetadataPool, std::chrono::seconds(10));
        return 2;
    }
    if (!allPoolsConverged(sched))
        return 3;
    printf("  child: empty work rejected with nullptr\n");
    return 0;
}

// ─── parent: subprocess checks ─────────────────────────────────────────────

int runChild(QCoreApplication &app, const char *arg)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(app.applicationFilePath(), QStringList{QString::fromLatin1(arg)});
    if (!proc.waitForStarted(5000))
        return -1;
    if (!proc.waitForFinished(30000))
    {
        proc.kill();
        proc.waitForFinished();
        return -2; // hung
    }
    if (proc.exitStatus() != QProcess::NormalExit)
        return -3; // crashed / terminated
    return proc.exitCode();
}

void testThrowingWorkSubprocesses(QCoreApplication &app)
{
    printf("\n[1. throwing work / callbacks must not kill the process]\n");
    fflush(stdout);
    CHECK(runChild(app, "--child-work-throw-std") == 0, "work throwing std::exception survives");
    CHECK(runChild(app, "--child-work-throw-int") == 0, "work throwing int survives");
    CHECK(runChild(app, "--child-progress-throw") == 0,
          "onProgress throwing survives (normal path)");
    CHECK(runChild(app, "--child-deadline-progress-throw") == 0,
          "onProgress throwing survives (deadline path)");
    CHECK(runChild(app, "--child-done-throw") == 0, "done throwing is contained");
    CHECK(runChild(app, "--child-empty-work") == 0, "empty work rejected at submit");
}

// ─── 2. cancelTree done semantics ──────────────────────────────────────────

// Pool-queued victim: cancelTree must suppress the user done callback.
void testCancelTreeQueuedVictim()
{
    printf("\n[2. cancelTree: queued victim done suppressed]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> blockerDone{false};
    std::atomic<bool> victimDone{false};
    auto blocker = sched.submit(Priority::Background,
                                [&](const TaskContext &)
                                {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                                    blockerDone = true;
                                });
    auto victim = sched.submit(
        Priority::Background, [&](const TaskContext &) {}, {},
        std::chrono::steady_clock::time_point::max(), [&]() { victimDone = true; });
    TaskScheduler::cancelTree(victim->id);
    CHECK(victim->isCancelled(), "victim cancel token set");
    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(blockerDone.load(), "blocker ran to completion");
    CHECK(!victimDone.load(), "cancelTree'd queued victim: user done NEVER fires");
    CHECK(allPoolsConverged(sched), "pools converge after queued cancelTree");
}

// Running cooperative victim: cancelTree must suppress the user done callback.
void testCancelTreeRunningVictim()
{
    printf("\n[3. cancelTree: running cooperative victim done suppressed]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> victimDone{false};
    auto victim = sched.submit(
        Priority::Background,
        [](const TaskContext &ctx)
        {
            for (int i = 0; i < 40; ++i)
            {
                if (ctx.isCancelled())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        },
        {}, std::chrono::steady_clock::time_point::max(), [&]() { victimDone = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    TaskScheduler::cancelTree(victim->id);
    CHECK(victim->isCancelled(), "running victim cancel token set");
    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(!victimDone.load(), "cancelTree'd running victim: user done NEVER fires");
    CHECK(allPoolsConverged(sched), "pools converge after running cancelTree");
}

// Waiting (deferred) victim: done must not fire (baseline).
void testCancelTreeWaitingVictim()
{
    printf("\n[4. cancelTree: waiting victim done suppressed]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> victimDone{false};
    auto a = sched.submit(Priority::Background, [](const TaskContext &)
                          { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
    auto b = sched.submit(
        Priority::Background, [](const TaskContext &) {}, {a->id},
        std::chrono::steady_clock::time_point::max(), [&]() { victimDone = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    TaskScheduler::cancelTree(b->id);
    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(!victimDone.load(), "cancelTree'd waiting victim: user done NEVER fires");
    CHECK(allPoolsConverged(sched), "pools converge after waiting cancelTree");
}

// Soft cancel(handle): work skipped but done STILL fires.
void testSoftCancelKeepsDone()
{
    printf("\n[5. cancel(handle): done still fires]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    std::atomic<bool> victimDone{false};
    std::atomic<bool> victimRan{false};
    auto blocker = sched.submit(Priority::Background, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
    auto victim = sched.submit(
        Priority::Background, [&](const TaskContext &) { victimRan = true; }, {},
        std::chrono::steady_clock::time_point::max(), [&]() { victimDone = true; });
    TaskScheduler::cancel(victim);
    sched.drain(PoolType::MetadataPool, kDrain);
    CHECK(!victimRan.load(), "soft-cancelled task skips its work");
    CHECK(victimDone.load(), "soft-cancelled task: user done STILL fires");
    CHECK(allPoolsConverged(sched), "pools converge after soft cancel");
}

// ─── 6. exception/cancel storm: counters converge, no handle residue ───────
void testStormConvergence()
{
    printf("\n[6. storm: exceptions + cancels leave zero bookkeeping]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    // T11 repro: rapid-fire throwing tasks with the full soak case mix.
    // M27_STORMCASE=<n> isolates a single case for bisection.
    {
        std::function<void(const TaskContext &)> e;
        std::function<void(const TaskContext &)> f = [](const TaskContext &) {};
        printf("  storm: empty bool=%d nonempty bool=%d\n", static_cast<int>(bool(e)),
               static_cast<int>(bool(f)));
        fflush(stdout);
    }
    int onlyCase = -1;
    if (const char *e = std::getenv("M27_STORMCASE"))
        onlyCase = std::atoi(e);
    for (int i = 0; i < 800; ++i)
    {
        const int c = i % 5;
        if (onlyCase >= 0 && c != onlyCase)
            continue;
        switch (c)
        {
        case 0:
            sched.submit(Priority::Analysis,
                         [](const TaskContext &) { throw std::runtime_error("storm"); });
            break;
        case 1:
            sched.submit(Priority::Analysis, [](const TaskContext &) { throw 42; });
            break;
        case 2:
            sched.submit(
                Priority::Analysis, [](const TaskContext &) {}, {},
                std::chrono::steady_clock::time_point::max(),
                []() { throw std::runtime_error("done storm"); });
            break;
        case 3:
            sched.submit(
                Priority::Analysis,
                [](const TaskContext &ctx) { const_cast<TaskContext &>(ctx).reportProgress(50); },
                {}, std::chrono::steady_clock::time_point::max(), {},
                [](int) { throw std::runtime_error("progress storm"); });
            break;
        case 4:
        {
            std::function<void(const TaskContext &)> empty;
            if (sched.submit(Priority::Analysis, empty) != nullptr)
                CHECK(false, "empty work rejected in storm");
            break;
        }
        }
    }
    sched.drain(PoolType::AnalysisPool, kDrain);
    CHECK(allPoolsConverged(sched), "all pools converge after exception/cancel storm");
    const auto m = sched.metrics(PoolType::AnalysisPool);
    CHECK(m.execution_failures >= 100, "execution_failures observable in metrics");
}

// ─── 7. dependency churn: graph working-set must return to zero ────────────
void testDependencyGraphChurn()
{
    printf("\n[7. dependency churn: graph working-set returns to zero]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    int chains = 1500;
    if (const char *e = std::getenv("M27_CHURN"))
        chains = std::atoi(e);
    printf("  churn: %d chains\n", chains);
    fflush(stdout);
    // The default 1000-task pool cap would reject submits while the single
    // worker drains — that rejection is a separate contract, not what this
    // test measures. Raise the cap so the churn is pure dependency-graph work.
    sched.setMaxQueueDepth(PoolType::MetadataPool, 100000);
    for (int i = 0; i < chains; ++i)
    {
        auto a = sched.submit(Priority::Background, [](const TaskContext &) {});
        if (!a)
        {
            CHECK(false, "churn submission accepted (pool cap too low for the test)");
            break;
        }
        auto b = sched.submit(Priority::Background, [](const TaskContext &) {}, {a->id});
        sched.submit(Priority::Background, [](const TaskContext &) {}, {b->id});
        if (i % 500 == 0)
        {
            printf("  churn: submitted %d\n", i);
            fflush(stdout);
        }
    }
    sched.setMaxQueueDepth(PoolType::MetadataPool, 1000);
    printf("  churn: submitted, draining\n");
    fflush(stdout);
    sched.drain(PoolType::MetadataPool, kDrain);
    const auto g = sched.graphMetrics();
    CHECK(g.handles == 0, "no live handles after churn");
    CHECK(g.deferred == 0, "no deferred entries after churn");
    CHECK(g.dep_graph_entries == 0, "dep-graph entries return to zero after churn");
    CHECK(g.dependents_entries == 0, "reverse-dependency entries return to zero after churn");
    CHECK(allPoolsConverged(sched), "pools converge after churn");
}

// ─── 8. drain wall clock must not be ~2x the timeout ───────────────────────
void testDrainWallClock()
{
    printf("\n[8. drain(timeout) wall clock respects the timeout]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    auto longTask = sched.submit(Priority::Analysis, [](const TaskContext &)
                                 { std::this_thread::sleep_for(std::chrono::milliseconds(2500)); });
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = sched.drain(PoolType::AnalysisPool, std::chrono::milliseconds(300));
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    printf("  drain(300ms) with 2.5s task took %lld ms\n", static_cast<long long>(ms));
    CHECK(!ok, "drain reports timeout while work still active");
    CHECK(ms < 450, "drain wall clock <= ~1.5x timeout (buggy build waits ~2x)");
    sched.drain(PoolType::AnalysisPool, kDrain);
    CHECK(allPoolsConverged(sched), "pools converge after timeout drain");
}

// ─── 9. throwing backpressure handler cannot escape submit ─────────────────
void testBackpressureHandlerThrows()
{
    printf("\n[9. throwing backpressure handler contained]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    sched.setBackPressureHandler([](PoolType) { throw std::runtime_error("bp boom"); });
    sched.pause(PoolType::MetadataPool);
    bool threw = false;
    TaskScheduler::TaskHandle h = nullptr;
    try
    {
        h = sched.submit(Priority::Background, [](const TaskContext &) {});
    }
    catch (...)
    {
        threw = true;
    }
    sched.resume(PoolType::MetadataPool);
    sched.setBackPressureHandler({});
    CHECK(!threw, "throwing backpressure handler does not escape submit");
    CHECK(h == nullptr, "paused pool rejects with nullptr");
    CHECK(allPoolsConverged(sched), "pools converge after rejected submit");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (argc > 1)
    {
        const std::string mode = argv[1];
        if (mode == "--child-work-throw-std")
            return childWorkThrowStd();
        if (mode == "--child-work-throw-int")
            return childWorkThrowInt();
        if (mode == "--child-progress-throw")
            return childProgressThrow();
        if (mode == "--child-deadline-progress-throw")
            return childDeadlineProgressThrow();
        if (mode == "--child-done-throw")
            return childDoneThrow();
        if (mode == "--child-empty-work")
            return childEmptyWork();
        fprintf(stderr, "unknown child mode: %s\n", mode.c_str());
        return 127;
    }

    printf("=== M27 Scheduler fault-injection tests ===\n");
    fflush(stdout);

    auto &sched = TaskScheduler::instance();
    // Deterministic ordering: one worker per pool.
    for (int p = 0; p < 5; ++p)
        sched.setQueueMaxThreads(static_cast<Priority>(p), 1);

    testThrowingWorkSubprocesses(app);
    testCancelTreeQueuedVictim();
    testCancelTreeRunningVictim();
    testCancelTreeWaitingVictim();
    testSoftCancelKeepsDone();
    testDependencyGraphChurn();
    testDrainWallClock();
    testBackpressureHandlerThrows();
    // Last: pre-fix the in-process throwing tasks abort the suite — that abort
    // IS the reproduction of "work throwing kills the process".
    testStormConvergence();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
