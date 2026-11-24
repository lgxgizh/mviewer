#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>

using Priority = TaskScheduler::Priority;
using PoolType = TaskScheduler::PoolType;
using TaskContext = TaskScheduler::TaskContext;

namespace
{
struct CancelGate
{
    std::mutex mu;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
};

struct CancelProbe
{
    std::shared_ptr<int> sentinel;
    std::shared_ptr<std::atomic<int>> hits;
    std::shared_ptr<CancelGate> gate;
    ~CancelProbe()
    {
        if (hits)
            hits->fetch_add(1, std::memory_order_relaxed);
        if (gate)
        {
            std::lock_guard<std::mutex> lock(gate->mu);
            gate->release = true;
            gate->cv.notify_all();
        }
        (void)TaskScheduler::instance().graphMetrics();
    }
};
} // namespace

int childCancelTreeDeferredLockedDtor()
{
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Background, 1);
    auto gate = std::make_shared<CancelGate>();
    auto doneCount = std::make_shared<std::atomic<int>>(0);
    auto countDone = [doneCount]() { doneCount->fetch_add(1, std::memory_order_relaxed); };
    auto parent = sched.submit(
        Priority::Background,
        [gate](const TaskContext &ctx)
        {
            {
                std::lock_guard<std::mutex> lock(gate->mu);
                gate->entered = true;
                gate->cv.notify_all();
            }
            std::unique_lock<std::mutex> lock(gate->mu);
            gate->cv.wait_for(lock, std::chrono::seconds(10),
                              [&] { return ctx.isCancelled() || gate->release; });
        },
        {}, std::chrono::steady_clock::time_point::max(), countDone);
    if (!parent)
        return 2;
    {
        std::unique_lock<std::mutex> lock(gate->mu);
        if (!gate->cv.wait_for(lock, std::chrono::seconds(10), [&] { return gate->entered; }))
            return 3;
    }

    auto hits = std::make_shared<std::atomic<int>>(0);
    auto work = std::make_shared<CancelProbe>();
    auto progress = std::make_shared<CancelProbe>();
    work->sentinel = std::make_shared<int>(1);
    progress->sentinel = std::make_shared<int>(1);
    work->hits = progress->hits = hits;
    work->gate = progress->gate = gate;
    std::weak_ptr<int> workWeak = work->sentinel;
    std::weak_ptr<int> progressWeak = progress->sentinel;
    const auto root = parent->id;
    auto victim = sched.submit(
        Priority::Background, [work](const TaskContext &) { (void)work; }, {root},
        std::chrono::steady_clock::time_point::max(), countDone,
        [progress](int) { (void)progress; });
    if (!victim)
        return 4;
    parent.reset();
    victim.reset();
    work.reset();
    progress.reset();
    if (sched.graphMetrics().deferred != 1)
        return 4;

    sched.cancelTree(root);
    if (hits->load(std::memory_order_relaxed) != 2)
        return 6;
    if (!workWeak.expired() || !progressWeak.expired())
        return 7;
    if (!sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(5000)))
        return 5;
    if (doneCount->load(std::memory_order_relaxed) != 0)
        return 8;
    if (sched.metrics(PoolType::MetadataPool).cancelled != 2)
        return 9;
    for (int p = 0; p < 5; ++p)
    {
        const auto m = sched.metrics(static_cast<PoolType>(p));
        if (m.pending != 0 || m.waiting != 0 || m.active_tasks != 0 || m.queue_depth != 0)
            return 10;
    }
    const auto g = sched.graphMetrics();
    if (g.handles != 0 || g.deferred != 0 || g.dep_graph_entries != 0 || g.dependents_entries != 0)
        return 11;
    printf("  child: cancelTree dropped deferred work and progress captures\n");
    return 0;
}
