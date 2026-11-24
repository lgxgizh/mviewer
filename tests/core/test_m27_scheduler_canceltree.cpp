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

namespace
{
struct GateRelease
{
    std::shared_ptr<CancelGate> gate;
    ~GateRelease()
    {
        if (!gate)
            return;
        std::lock_guard<std::mutex> lock(gate->mu);
        gate->release = true;
        gate->cv.notify_all();
    }
};

bool blockedGauges(TaskScheduler &sched, size_t active, size_t queued, size_t pending,
                   uint64_t cancelled, size_t handles)
{
    const auto m = sched.metrics(PoolType::MetadataPool);
    const auto g = sched.graphMetrics();
    return m.active_tasks == active && m.queue_depth == queued && m.pending == pending &&
           m.waiting == 0 && m.cancelled == cancelled && g.deferred == 0 && g.handles == handles;
}

bool terminalSettled(TaskScheduler &sched)
{
    for (int p = 0; p < 5; ++p)
    {
        const auto m = sched.metrics(static_cast<PoolType>(p));
        if (m.pending != 0 || m.waiting != 0 || m.active_tasks != 0 || m.queue_depth != 0)
            return false;
    }
    const auto g = sched.graphMetrics();
    return g.handles == 0 && g.deferred == 0 && g.dep_graph_entries == 0 &&
           g.dependents_entries == 0;
}
} // namespace

int childCancelTreeRunningAccounting()
{
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Background, 1);
    auto gate = std::make_shared<CancelGate>();
    GateRelease release{gate};
    auto doneCount = std::make_shared<std::atomic<int>>(0);
    auto parent = sched.submit(
        Priority::Background,
        [gate](const TaskContext &)
        {
            std::unique_lock<std::mutex> lock(gate->mu);
            gate->entered = true;
            gate->cv.notify_all();
            gate->cv.wait_for(lock, std::chrono::seconds(10), [&] { return gate->release; });
        },
        {}, std::chrono::steady_clock::time_point::max(),
        [doneCount]() { doneCount->fetch_add(1, std::memory_order_relaxed); });
    if (!parent)
        return 2;
    {
        std::unique_lock<std::mutex> lock(gate->mu);
        if (!gate->cv.wait_for(lock, std::chrono::seconds(10), [&] { return gate->entered; }))
            return 3;
    }
    const auto root = parent->id;
    parent.reset();
    sched.cancelTree(root);
    if (!blockedGauges(sched, 1, 0, 1, 0, 1))
        return 4;
    sched.cancelTree(root);
    if (!blockedGauges(sched, 1, 0, 1, 0, 1))
        return 5;
    if (sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(200)))
        return 6;
    {
        std::lock_guard<std::mutex> lock(gate->mu);
        gate->release = true;
        gate->cv.notify_all();
    }
    if (!sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(5000)))
        return 7;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (doneCount->load(std::memory_order_relaxed) != 0 || m.cancelled != 1 || m.completed != 0)
        return 8;
    if (!terminalSettled(sched))
        return 9;
    printf("  child: running tree-cancel retired after user work returned\n");
    return 0;
}

int childCancelTreeQueuedAccounting()
{
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Background, 1);
    auto gate = std::make_shared<CancelGate>();
    GateRelease release{gate};
    auto treeDone = std::make_shared<std::atomic<int>>(0);
    auto treeWork = std::make_shared<std::atomic<int>>(0);
    auto softDone = std::make_shared<std::atomic<int>>(0);
    auto softWork = std::make_shared<std::atomic<int>>(0);
    auto blocker = sched.submit(Priority::Background,
                                [gate](const TaskContext &)
                                {
                                    std::unique_lock<std::mutex> lock(gate->mu);
                                    gate->entered = true;
                                    gate->cv.notify_all();
                                    gate->cv.wait_for(lock, std::chrono::seconds(10),
                                                      [&] { return gate->release; });
                                });
    if (!blocker)
        return 2;
    {
        std::unique_lock<std::mutex> lock(gate->mu);
        if (!gate->cv.wait_for(lock, std::chrono::seconds(10), [&] { return gate->entered; }))
            return 3;
    }
    auto tree = sched.submit(
        Priority::Background,
        [treeWork](const TaskContext &) { treeWork->fetch_add(1, std::memory_order_relaxed); }, {},
        std::chrono::steady_clock::time_point::max(),
        [treeDone]() { treeDone->fetch_add(1, std::memory_order_relaxed); });
    auto soft = sched.submit(
        Priority::Background,
        [softWork](const TaskContext &) { softWork->fetch_add(1, std::memory_order_relaxed); }, {},
        std::chrono::steady_clock::time_point::max(),
        [softDone]() { softDone->fetch_add(1, std::memory_order_relaxed); });
    if (!tree || !soft || sched.graphMetrics().deferred != 0)
        return 4;
    if (!blockedGauges(sched, 1, 2, 3, 0, 3))
        return 5;
    const auto treeId = tree->id;
    sched.cancelTree(treeId);
    if (!blockedGauges(sched, 1, 2, 3, 0, 3))
        return 6;
    sched.cancelTree(treeId);
    if (!blockedGauges(sched, 1, 2, 3, 0, 3))
        return 7;
    TaskScheduler::cancel(soft);
    blocker.reset();
    tree.reset();
    soft.reset();
    if (!blockedGauges(sched, 1, 2, 3, 0, 3))
        return 8;
    if (sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(200)))
        return 9;
    {
        std::lock_guard<std::mutex> lock(gate->mu);
        gate->release = true;
        gate->cv.notify_all();
    }
    if (!sched.drain(PoolType::MetadataPool, std::chrono::milliseconds(5000)))
        return 10;
    const auto m = sched.metrics(PoolType::MetadataPool);
    if (treeDone->load(std::memory_order_relaxed) != 0 ||
        treeWork->load(std::memory_order_relaxed) != 0 ||
        softDone->load(std::memory_order_relaxed) != 1 ||
        softWork->load(std::memory_order_relaxed) != 0 || m.cancelled != 1 || m.completed != 2)
        return 11;
    if (!terminalSettled(sched))
        return 12;
    printf("  child: queued tree-cancel retired when the pool took it\n");
    return 0;
}
