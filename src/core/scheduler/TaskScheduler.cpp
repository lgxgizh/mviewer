#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <memory>

namespace
{

class LambdaTask : public QRunnable
{
  public:
    LambdaTask(std::shared_ptr<TaskScheduler::TaskContext> ctx,
               std::function<void(const TaskScheduler::TaskContext &)> work,
               std::function<void()> done)
        : m_ctx(std::move(ctx)), m_work(std::move(work)), m_done(std::move(done))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // M26: every terminal path must finalize exactly once. A task whose
        // deadline already expired when it reaches a worker skips its work but
        // still reports the terminal transition (m_done -> onTaskComplete ->
        // metrics + user callback), so the pool can never be left with a stuck
        // pending/active counter or a leaked handle.
        if (m_ctx->deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() > m_ctx->deadline)
        {
            m_ctx->deadline_exceeded.store(true, std::memory_order_relaxed);
            m_ctx->reportProgress(0);
            if (m_done)
                m_done();
            return;
        }

        // Guard against cancellation only; invoke m_work unconditionally.
        // std::function::operator bool is standard-guaranteed reliable, so an
        // empty task here is programmer error, not a runtime condition to
        // branch on. The assert below catches that error in debug builds;
        // assert() compiles out under NDEBUG, so it is a dev-time guard only,
        // not a runtime safety mechanism.
        // Empty task is programmer error. Runtime path assumes valid callable.
        assert(m_work.target_type() != typeid(void));
        if (!m_ctx->isCancelled())
            m_work(*m_ctx);
        m_ctx->reportProgress(100);

        // M26: the done callback runs on this worker thread by contract (see
        // docs/spec/TaskScheduler.spec.md); UI callers marshal to the UI
        // thread themselves. Calling it here (instead of queueing a main-thread
        // metainvoke) keeps scheduler bookkeeping timely so drain() can observe
        // active_tasks hitting 0 without depending on the main event loop.
        auto done = std::move(m_done);
        try
        {
            done();
        }
        catch (...)
        {
        }
    }

  private:
    std::shared_ptr<TaskScheduler::TaskContext> m_ctx;
    std::function<void(const TaskScheduler::TaskContext &)> m_work;
    std::function<void()> m_done;
};

} // namespace

struct TaskScheduler::Impl
{
    static constexpr int kNumQueues = 5;
    QThreadPool priorityQueues[kNumQueues];
};

std::atomic<uint64_t> TaskScheduler::s_nextId{1};

TaskScheduler &TaskScheduler::instance()
{
    // Intentionally leaked (never destroyed via a function-local static).
    // TaskScheduler owns QThreadPool worker threads. If it were a
    // function-local static, its destructor would run at program exit —
    // AFTER QCoreApplication has already been torn down. QThreadPool's
    // destructor joins its worker threads, which can deadlock when those
    // threads still reference the (dying) QCoreApplication, so the process
    // never exits. Leaking the singleton lets the OS reclaim the threads on
    // process exit, which is the correct lifetime for a process-global
    // scheduler. The destructor below remains a safe explicit-shutdown path.
    static TaskScheduler *inst = new TaskScheduler();
    return *inst;
}

TaskScheduler::TaskScheduler() : m_impl(new Impl)
{
    const int n = QThread::idealThreadCount();
    setQueueMaxThreads(Priority::UI, std::max(1, n));
    setQueueMaxThreads(Priority::Decode, std::max(1, n));
    setQueueMaxThreads(Priority::Thumbnail, std::max(2, n));
    setQueueMaxThreads(Priority::Analysis, std::max(1, n / 2));
    setQueueMaxThreads(Priority::Background, std::max(1, n / 2));
}

TaskScheduler::~TaskScheduler()
{
    // Explicitly drain every pool before destroying the QThreadPool objects.
    // TaskScheduler is a function-local static, so its destructor runs at
    // program exit — AFTER QCoreApplication has already been torn down. Letting
    // QThreadPool's own destructor block in waitForDone() while its worker
    // threads still reference the (dying) QCoreApplication causes a teardown
    // deadlock (the process never exits). Draining here forces each pool's
    // threads to finish and exit cleanly while we still own them.
    if (m_impl)
    {
        for (int i = 0; i < Impl::kNumQueues; ++i)
        {
            QThreadPool &q = m_impl->priorityQueues[i];
            q.setExpiryTimeout(0);
            q.clear();
            q.waitForDone();
        }
    }
    delete m_impl;
}

TaskScheduler::Priority TaskScheduler::toPriority(PoolType pool)
{
    switch (pool)
    {
    case IOPool:
        return Priority::UI;
    case DecodePool:
        return Priority::Decode;
    case ThumbnailPool:
        return Priority::Thumbnail;
    case AnalysisPool:
        return Priority::Analysis;
    case MetadataPool:
        return Priority::Background;
    }
    return Priority::Background;
}

void TaskScheduler::setMaxQueueDepth(PoolType pool, size_t max)
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    m_poolState[idx].max_queue_depth = max;
}

size_t TaskScheduler::maxQueueDepth(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    return m_poolState[idx].max_queue_depth;
}

void TaskScheduler::submit(PoolType pool, void *runnable)
{
    // M26: the legacy QRunnable path gets the same lifecycle as every other
    // task so its metrics converge (pending/active return to zero, drain is
    // bounded). The handle is registered but never returned to the caller.
    auto prio = toPriority(pool);
    auto idx = static_cast<int>(prio);
    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->priority = prio;
    ctx->pool = pool;
    const TaskId ctx_id = ctx->id;
    auto *wrapped = new LambdaTask(
        ctx, [runnable](const TaskContext &)
        {
            auto *r = static_cast<QRunnable *>(runnable);
            r->run();
            // Preserve the QThreadPool::start ownership contract: auto-delete
            // runnables are freed by the executor after run().
            if (r->autoDelete())
                delete r;
        },
        [this, ctx_id, prio]()
        {
            onTaskComplete(ctx_id, prio, false);
            // No user done callback exists for this legacy path.
        });
    std::lock_guard<std::mutex> lock(m_graphMtx);
    m_poolState[idx].metrics.submitted++;
    m_poolState[idx].metrics.pending++;
    m_handles[ctx_id] = ctx;
    m_taskPriomap[ctx_id] = prio;
    m_poolState[idx].metrics.queue_depth++;
    m_impl->priorityQueues[idx].start(wrapped);
    m_poolState[idx].metrics.queue_depth--;
    m_poolState[idx].metrics.active_tasks++;
}

TaskScheduler::TaskHandle
TaskScheduler::submit(Priority prio, std::function<void(const TaskContext &)> work,
                      std::vector<TaskId> deps, std::chrono::steady_clock::time_point deadline,
                      std::function<void()> done, std::function<void(int)> onProgress)
{
    const int pIdx = static_cast<int>(prio);
    assert(pIdx >= 0 && pIdx < 5);

    // Back-pressure check. The user handler must run OUTSIDE the lock (it may
    // call back into the scheduler).
    {
        PoolType backpressurePool = MetadataPool;
        bool rejected = false;
        {
            std::lock_guard<std::mutex> lock(m_graphMtx);
            if (m_poolState[pIdx].paused)
            {
                m_poolState[pIdx].metrics.backpressure_rejected++;
                rejected = true;
                backpressurePool = poolFromPriority(prio);
            }
            else
            {
                const size_t md = m_poolState[pIdx].metrics.queue_depth +
                                  m_poolState[pIdx].metrics.active_tasks;
                if (m_poolState[pIdx].max_queue_depth > 0 &&
                    md >= m_poolState[pIdx].max_queue_depth)
                {
                    m_poolState[pIdx].metrics.backpressure_rejected++;
                    rejected = true;
                    backpressurePool = poolFromPriority(prio);
                }
            }
        }
        if (rejected)
        {
            if (m_backpressure)
                m_backpressure(backpressurePool);
            return nullptr;
        }
    }

    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->onProgress = std::move(onProgress);
    ctx->dependencies = std::move(deps);
    ctx->deadline = deadline;
    ctx->priority = prio;

    const TaskId ctx_id = ctx->id;
    auto *runnable = new LambdaTask(ctx, std::move(work),
                                    [this, done = std::move(done), ctx, prio]()
                                    {
                                        onTaskComplete(ctx->id, prio,
                                                       ctx->deadline_exceeded.load(
                                                           std::memory_order_relaxed));
                                        if (done)
                                            done();
                                    });

    {
        // P0-1 fix: submitted++ moved inside the lock to avoid data race
        // with submit(PoolType, void*) and onTaskComplete().
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_poolState[pIdx].metrics.submitted++;
        m_handles[ctx_id] = ctx;
        m_taskPriomap[ctx_id] = prio;
        if (!ctx->dependencies.empty())
        {
            // Waiting state: counted in pending + waiting, not active/queued.
            m_poolState[pIdx].metrics.pending++;
            m_poolState[pIdx].metrics.waiting++;
            m_depGraph[ctx_id] = ctx->dependencies;
            for (TaskId dep : ctx->dependencies)
                m_dependents[dep].push_back(ctx_id);
            m_deferred[ctx_id] = DeferredEntry{prio, runnable, deadline};
            // If any deps already finished, launch ready deferred tasks.
            std::vector<std::pair<Priority, void *>> ready;
            releaseReadyTasks(ready);
            for (auto &[p, r] : ready)
            {
                const int rpIdx = static_cast<int>(p);
                m_poolState[rpIdx].metrics.queue_depth++;
                m_impl->priorityQueues[rpIdx].start(static_cast<QRunnable *>(r));
                m_poolState[rpIdx].metrics.queue_depth--;
                m_poolState[rpIdx].metrics.active_tasks++;
            }
        }
        else
        {
            // Queued -> active transition.
            m_poolState[pIdx].metrics.pending++;
            m_poolState[pIdx].metrics.queue_depth++;
            m_impl->priorityQueues[pIdx].start(runnable);
            m_poolState[pIdx].metrics.queue_depth--;
            m_poolState[pIdx].metrics.active_tasks++;
        }
    }
    return ctx;
}

TaskScheduler::TaskHandle TaskScheduler::submit(PoolType pool, std::function<void()> work,
                                                std::function<void()> done)
{
    return submit(toPriority(pool),
                  [w = std::move(work)](const TaskContext &)
                  {
                      if (w)
                          w();
                  },
                  {}, std::chrono::steady_clock::time_point::max(), std::move(done), {});
}

void TaskScheduler::setQueueMaxThreads(Priority prio, int n)
{
    m_impl->priorityQueues[static_cast<int>(prio)].setMaxThreadCount(std::max(1, n));
}

void TaskScheduler::setPoolMaxThreads(PoolType pool, int n)
{
    setQueueMaxThreads(toPriority(pool), n);
}

bool depDone(const std::unordered_map<uint64_t, TaskScheduler::TaskHandle> &handles, uint64_t id)
{
    auto it = handles.find(id);
    if (it == handles.end())
        return true;
    if (it->second->isCancelled())
        return true;
    return it->second->currentProgress() >= 100;
}

void TaskScheduler::releaseReadyTasks(std::vector<std::pair<Priority, void *>> &out)
{
    if (m_deferred.empty())
        return;

    for (auto it = m_deferred.begin(); it != m_deferred.end();)
    {
        const auto &deps = m_depGraph[it->first];
        bool all_done = true;
        for (auto d : deps)
        {
            if (!depDone(m_handles, d))
            {
                all_done = false;
                break;
            }
        }
        if (all_done)
        {
            const TaskId id = it->first;
            const int pIdx = static_cast<int>(it->second.prio);
            // Waiting -> running transition (pending unchanged until terminal).
            if (m_poolState[pIdx].metrics.waiting > 0)
                m_poolState[pIdx].metrics.waiting--;
            // The released task no longer waits on its prerequisites; drop the
            // reverse edges so a later cancelTree of a prerequisite cannot
            // touch a task that has already moved on.
            auto depsIt = m_depGraph.find(id);
            if (depsIt != m_depGraph.end())
            {
                for (TaskId dep : depsIt->second)
                {
                    auto &followers = m_dependents[dep];
                    followers.erase(std::remove(followers.begin(), followers.end(), id),
                                    followers.end());
                }
                m_depGraph.erase(depsIt);
            }
            out.push_back({it->second.prio, it->second.runnable});
            it = m_deferred.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TaskScheduler::onTaskComplete(TaskId id, Priority prio, bool deadlineExceeded)
{
    std::vector<std::pair<Priority, void *>> ready;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        // P2-4 fix: if cancelTree() already erased this handle and decremented
        // active_tasks/pending, skip the metrics update to avoid double-decrement.
        auto it = m_handles.find(id);
        if (it == m_handles.end())
            return; // already cancelled/removed — metrics already adjusted
        m_handles.erase(it);
        m_taskPriomap.erase(id);
        m_depGraph.erase(id);
        const int pIdx = static_cast<int>(prio);
        m_poolState[pIdx].metrics.completed++;
        if (deadlineExceeded)
            m_poolState[pIdx].metrics.deadline_exceeded++;
        // Terminal transition: exactly one decrement of each counter.
        if (m_poolState[pIdx].metrics.pending > 0)
            m_poolState[pIdx].metrics.pending--;
        if (m_poolState[pIdx].metrics.active_tasks > 0)
            m_poolState[pIdx].metrics.active_tasks--;
        // Drop this task from the prerequisite edges of its dependents is not
        // needed: dependents keep their m_depGraph entries and depDone() treats
        // a missing handle as done, which is exactly the release condition.
        releaseReadyTasks(ready);
        for (auto &[p, r] : ready)
        {
            const int rpIdx = static_cast<int>(p);
            m_poolState[rpIdx].metrics.queue_depth++;
            m_impl->priorityQueues[rpIdx].start(static_cast<QRunnable *>(r));
            m_poolState[rpIdx].metrics.queue_depth--;
            m_poolState[rpIdx].metrics.active_tasks++;
        }
    }
}

void TaskScheduler::cancelTree(TaskId rootId)
{
    auto &sched = instance();
    std::lock_guard<std::mutex> lock(sched.m_graphMtx);

    // M26: walk the REVERSE map — m_dependents[id] lists the tasks waiting on
    // id — so cancelTree(root) cancels root plus all transitive DEPENDENTS
    // and never touches root's own prerequisites.
    std::vector<TaskId> stack;
    std::vector<TaskId> victims;
    stack.push_back(rootId);
    while (!stack.empty())
    {
        TaskId cur = stack.back();
        stack.pop_back();
        if (std::find(victims.begin(), victims.end(), cur) != victims.end())
            continue;
        victims.push_back(cur);
        auto it = sched.m_dependents.find(cur);
        if (it != sched.m_dependents.end())
        {
            for (TaskId dependent : it->second)
                stack.push_back(dependent);
        }
    }
    for (TaskId vic : victims)
    {
        auto pit = sched.m_taskPriomap.find(vic);
        Priority prio = (pit != sched.m_taskPriomap.end()) ? pit->second : Priority::Background;
        const int pIdx = static_cast<int>(prio);
        auto dit = sched.m_deferred.find(vic);
        if (dit != sched.m_deferred.end())
        {
            // Waiting (deferred) task: never started, owned by m_deferred.
            // It holds pending + waiting, not active.
            auto hit = sched.m_handles.find(vic);
            if (hit != sched.m_handles.end())
                hit->second->requestCancel(); // observers see isCancelled()
            delete static_cast<QRunnable *>(dit->second.runnable);
            sched.m_deferred.erase(dit);
            sched.m_poolState[pIdx].metrics.cancelled++;
            if (sched.m_poolState[pIdx].metrics.waiting > 0)
                sched.m_poolState[pIdx].metrics.waiting--;
            if (sched.m_poolState[pIdx].metrics.pending > 0)
                sched.m_poolState[pIdx].metrics.pending--;
            sched.m_handles.erase(vic);
            sched.m_taskPriomap.erase(vic);
        }
        else
        {
            // Live task (running or queued): it still holds active + pending.
            auto hit = sched.m_handles.find(vic);
            if (hit != sched.m_handles.end())
            {
                hit->second->requestCancel();
                sched.m_handles.erase(hit);
                sched.m_taskPriomap.erase(vic);
                sched.m_poolState[pIdx].metrics.cancelled++;
                if (sched.m_poolState[pIdx].metrics.active_tasks > 0)
                    sched.m_poolState[pIdx].metrics.active_tasks--;
                if (sched.m_poolState[pIdx].metrics.pending > 0)
                    sched.m_poolState[pIdx].metrics.pending--;
            }
        }
        sched.m_depGraph.erase(vic);
        sched.m_dependents.erase(vic);
        // Remove this victim from every other task's dependents list so no
        // edge to it survives (a cancelled dep must never gate a release).
        for (auto &[follower, deps] : sched.m_depGraph)
        {
            deps.erase(std::remove(deps.begin(), deps.end(), vic), deps.end());
        }
    }
}

TaskScheduler::TaskHandle TaskScheduler::handle(TaskId id)
{
    std::lock_guard<std::mutex> lock(m_graphMtx);
    auto it = m_handles.find(id);
    if (it == m_handles.end())
        return nullptr;
    return it->second;
}

TaskScheduler::PoolMetrics TaskScheduler::metrics(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    return m_poolState[idx].metrics;
}

bool TaskScheduler::isSaturated(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    const auto &m = m_poolState[idx].metrics;
    if (m_poolState[idx].max_queue_depth == 0)
        return false;
    return (m.queue_depth + m.active_tasks) >= m_poolState[idx].max_queue_depth;
}

size_t TaskScheduler::queueDepth(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    return m_poolState[idx].metrics.queue_depth;
}

void TaskScheduler::pause(PoolType pool)
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    m_poolState[idx].paused = true;
}

void TaskScheduler::resume(PoolType pool)
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    m_poolState[idx].paused = false;
}

bool TaskScheduler::waitForPoolDrained(int idx, std::chrono::milliseconds timeout)
{
    // P0-2 fix: never call QThreadPool::waitForDone() while holding m_graphMtx.
    // Worker threads call onTaskComplete() -> lock(m_graphMtx) when they finish;
    // if we hold the lock during waitForDone(), the workers can never complete
    // -> deadlock. Instead, read the pending/active snapshot under the lock,
    // release it, then call waitForDone() unlocked.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        {
            std::lock_guard<std::mutex> lock(m_graphMtx);
            if (m_poolState[idx].metrics.pending == 0 && m_poolState[idx].metrics.active_tasks == 0)
                break; // pool is idle — exit loop to do a final waitForDone unlocked
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Call waitForDone() WITHOUT holding the lock so worker threads can acquire
    // m_graphMtx in onTaskComplete() and decrement pending/active_tasks.
    return m_impl->priorityQueues[idx].waitForDone(static_cast<int>(timeout.count()));
}

bool TaskScheduler::drain(PoolType pool, std::chrono::milliseconds timeout)
{
    auto idx = static_cast<int>(toPriority(pool));
    return waitForPoolDrained(idx, timeout);
}

void TaskScheduler::shutdown(std::chrono::milliseconds timeout)
{
    // Pause all pools
    for (int i = 0; i < 5; ++i)
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_poolState[i].paused = true;
    }
    // Drain each pool sequentially
    for (int i = 0; i < 5; ++i)
    {
        waitForPoolDrained(i, timeout / 5);
    }
}

size_t TaskScheduler::activeTaskCount(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    return m_poolState[idx].metrics.active_tasks;
}
