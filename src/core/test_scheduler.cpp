#include <QCoreApplication>
// M6 unit tests: TaskScheduler dependency + priority mapping.
#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
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

static void testTaskSchedulerDependency()
{
    printf("\n[TaskScheduler Dependency]\n");
    auto &sched = TaskScheduler::instance();

    auto h1 = sched.submit(TaskScheduler::Priority::Background,
                           [](const TaskScheduler::TaskContext &) {});
    auto h2 = sched.submit(TaskScheduler::Priority::Background,
                           [](const TaskScheduler::TaskContext &) {});
    CHECK(h1->id != h2->id, "TaskId auto-increments");
    CHECK(h1->id > 0, "TaskId starts from 1");

    // h3 depends on a gate task that is still running, so it stays deferred
    // in the registry until the gate is released. Depending on an unknown id
    // (the old 999999) counts as already satisfied: h3 launched at once and
    // could finish and leave m_handles before handle() looked it up, which
    // made this check flaky under the slower ASan build.
    std::atomic<bool> releaseGate{false};
    auto gate = sched.submit(TaskScheduler::Priority::Background,
                             [&releaseGate](const TaskScheduler::TaskContext &)
                             {
                                 while (!releaseGate.load(std::memory_order_acquire))
                                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
                             });
    const TaskScheduler::TaskId depId = gate->id;
    std::atomic<bool> h3Ran{false};
    auto h3 = sched.submit(TaskScheduler::Priority::Background,
                           [&h3Ran](const TaskScheduler::TaskContext &)
                           { h3Ran.store(true, std::memory_order_release); }, {depId});
    CHECK(h3->id > 0, "Dep task accepted");
    CHECK(h3->dependencies.size() == 1, "Dep recorded");

    auto found = sched.handle(h3->id);
    CHECK(found != nullptr, "handle() finds task");

    releaseGate.store(true, std::memory_order_release);
    CHECK(sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(10)),
          "dependent drains after gate release");
    CHECK(h3Ran.load(std::memory_order_acquire), "dependent runs after its dependency");

    auto h4 = sched.submit(TaskScheduler::DecodePool, []() {}, []() {});
    CHECK(h4->id > 0, "Compat submit works");

    CHECK(TaskScheduler::toPriority(TaskScheduler::DecodePool) == TaskScheduler::Priority::Decode,
          "toPriority(DecodePool) = Decode");
    CHECK(TaskScheduler::toPriority(TaskScheduler::MetadataPool) ==
              TaskScheduler::Priority::Background,
          "toPriority(MetadataPool) = Background");
}

static void testQueueDepthIsNotYetStarted()
{
    printf("\n[TaskScheduler queue_depth vs active_tasks]\n");
    auto &sched = TaskScheduler::instance();
    CHECK(sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)),
          "MetadataPool idle before metrics check");
    sched.setPoolMaxThreads(TaskScheduler::MetadataPool, 1);
    sched.setMaxQueueDepth(TaskScheduler::MetadataPool, 1000);

    std::atomic<bool> release{false};
    std::atomic<bool> blockerEntered{false};
    auto blocker = sched.submit(TaskScheduler::Priority::Background,
                                [&](const TaskScheduler::TaskContext &)
                                {
                                    blockerEntered.store(true, std::memory_order_release);
                                    while (!release.load(std::memory_order_acquire))
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                });
    CHECK(blocker != nullptr, "blocker accepted");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blockerEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(blockerEntered.load(), "blocker is executing");

    std::atomic<bool> secondRan{false};
    auto queued =
        sched.submit(TaskScheduler::Priority::Background, [&](const TaskScheduler::TaskContext &)
                     { secondRan.store(true, std::memory_order_release); });
    CHECK(queued != nullptr, "second task accepted onto the single worker");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto mid = sched.metrics(TaskScheduler::MetadataPool);
    CHECK(mid.active_tasks == 1, "only the running task counts as active");
    CHECK(mid.queue_depth >= 1, "not-yet-started task counts as queue_depth");
    CHECK(!secondRan.load(), "queued task has not started");

    TaskScheduler::cancelTree(queued->id);
    const auto cancelled = sched.metrics(TaskScheduler::MetadataPool);
    CHECK(cancelled.queue_depth == 0, "cancel before start drops queue_depth");
    CHECK(cancelled.active_tasks == 1, "cancel of a queued task leaves the runner active");
    CHECK(!secondRan.load(), "cancelled queued task does not run");

    release.store(true, std::memory_order_release);
    CHECK(sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)), "MetadataPool drains");
    const auto done = sched.metrics(TaskScheduler::MetadataPool);
    CHECK(done.pending == 0 && done.active_tasks == 0 && done.queue_depth == 0,
          "metrics converge after the runner finishes");

    // One running + one queued fills a depth of 2; the next submit is rejected.
    release.store(false, std::memory_order_release);
    blockerEntered.store(false, std::memory_order_release);
    sched.setMaxQueueDepth(TaskScheduler::MetadataPool, 2);
    auto again = sched.submit(TaskScheduler::Priority::Background,
                              [&](const TaskScheduler::TaskContext &)
                              {
                                  blockerEntered.store(true, std::memory_order_release);
                                  while (!release.load(std::memory_order_acquire))
                                      std::this_thread::sleep_for(std::chrono::milliseconds(1));
                              });
    const auto occupyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!blockerEntered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < occupyDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(again != nullptr && blockerEntered.load(), "depth probe occupies the worker");
    auto held = sched.submit(TaskScheduler::Priority::Background,
                             [](const TaskScheduler::TaskContext &) {});
    CHECK(held != nullptr, "second in-flight task fits max depth 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto rejected = sched.submit(TaskScheduler::Priority::Background,
                                 [](const TaskScheduler::TaskContext &) {});
    CHECK(rejected == nullptr, "queue_depth + active_tasks saturates max depth");
    release.store(true, std::memory_order_release);
    CHECK(sched.drain(TaskScheduler::MetadataPool, std::chrono::seconds(5)),
          "MetadataPool drains after the depth probe");
    const auto after = sched.metrics(TaskScheduler::MetadataPool);
    CHECK(after.pending == 0 && after.active_tasks == 0 && after.queue_depth == 0,
          "metrics converge after the depth probe");
    // `held` sat in the queue for the 20 ms sleep above, so its queue wait is
    // well above a millisecond. total_latency_ns is start minus enqueue.
    CHECK(after.total_latency_ns > 1000000, "queue-wait latency is start time minus enqueue time");
    sched.setMaxQueueDepth(TaskScheduler::MetadataPool, 1000);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Scheduler Tests (M6) ===\n");
    fflush(stdout);

    testTaskSchedulerDependency();
    testQueueDepthIsNotYetStarted();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
