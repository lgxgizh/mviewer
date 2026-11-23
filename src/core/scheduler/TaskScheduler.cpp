#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace
{

std::atomic<int> g_foregroundDepth{0};
std::mutex g_foregroundMtx;
std::condition_variable g_foregroundCv;

// Thumbnail work yields to foreground work for a bounded time (never forever,
void yieldToForeground(const TaskScheduler::TaskContext &ctx)
{
    if (g_foregroundDepth.load(std::memory_order_relaxed) == 0 || ctx.isCancelled())
        return;
    std::unique_lock<std::mutex> lock(g_foregroundMtx);
    g_foregroundCv.wait_for(
        lock, std::chrono::milliseconds(200), [&]()
        { return g_foregroundDepth.load(std::memory_order_relaxed) == 0 || ctx.isCancelled(); });
}

// The task's state (context + callables) is kept in a shared object that both
// the runnable and the completion callback hold. That makes the state's
// destruction strictly happen-after run() has finished reading it, instead of
// the runnable's own members being read while a recycled allocation is being
// constructed at the same address (the churn stress test reuses them fast).
struct LambdaState
{
    std::shared_ptr<TaskScheduler::TaskContext> ctx;
    std::function<void(const TaskScheduler::TaskContext &)> work;
    std::function<void()> done;
    std::function<void()> onStart;
};

class LambdaTask : public QRunnable
{
  public:
    explicit LambdaTask(std::shared_ptr<LambdaState> state) : m_state(std::move(state))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // Thin override on purpose: the body lives in runTask() so a race report
        // names a symbolicateable frame instead of the bare `run` that every
        // QRunnable in the process shares.
        runTask();
    }

  private:
    void runTask()
    {
        LambdaState &state = *m_state;
        // The runnable has left the pool queue. Move queue_depth -> active_tasks
        if (state.onStart)
            state.onStart();

        auto &ctx = *state.ctx;
        // M26: every terminal path must finalize exactly once. A task whose
        if (ctx.deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() > ctx.deadline)
        {
            ctx.deadline_exceeded.store(true, std::memory_order_relaxed);
            ctx.reportProgress(0);
            if (state.done)
                state.done();
            return;
        }

        // Guard against cancellation only; invoke work unconditionally.
        assert(state.work.target_type() != typeid(void));
        // M27: user work is contained. A throwing task must never escape the
        try
        {
            if (ctx.priority == TaskScheduler::Priority::Thumbnail)
            {
                const auto beforeYield = std::chrono::steady_clock::now();
                yieldToForeground(ctx);
                ctx.foregroundYieldNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - beforeYield)
                                            .count();
            }
            if (ctx.priority == TaskScheduler::Priority::Decode)
            {
                const TaskScheduler::ForegroundWork foreground;
                if (!ctx.isCancelled())
                    state.work(ctx);
            }
            else if (!ctx.isCancelled())
            {
                state.work(ctx);
            }
        }
        catch (...)
        {
            ctx.execution_failed.store(true, std::memory_order_relaxed);
        }
        ctx.reportProgress(100);

        // M26: the done callback runs on this worker thread by contract (see
        auto done = std::move(state.done);
        try
        {
            done();
        }
        catch (...)
        {
        }
    }

    std::shared_ptr<LambdaState> m_state;
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
    static TaskScheduler *inst = new TaskScheduler();
    return *inst;
}

TaskScheduler::TaskScheduler() : m_impl(new Impl)
{
    const int n = QThread::idealThreadCount();
    // UI-queue work is short file I/O (file ops, visible prefetch): it never
    setQueueMaxThreads(Priority::UI, std::max(2, n / 2));
    setQueueMaxThreads(Priority::Decode, std::max(1, n));
    // Thumbnails used to get n threads on top of n decode + n resample threads
    setQueueMaxThreads(Priority::Thumbnail, std::max(2, n / 2));
    setQueueMaxThreads(Priority::Analysis, std::max(1, n / 2));
    // Background = metadata indexing / adjacent prefetch: lowest priority and
    setQueueMaxThreads(Priority::Background, std::max(1, n / 4));
    for (int i = 0; i < Impl::kNumQueues; ++i)
        m_impl->priorityQueues[i].setExpiryTimeout(5000);
    for (const Priority low : {Priority::Thumbnail, Priority::Analysis, Priority::Background})
        m_impl->priorityQueues[static_cast<int>(low)].setThreadPriority(QThread::LowPriority);
}

TaskScheduler::ForegroundWork::ForegroundWork()
{
    g_foregroundDepth.fetch_add(1, std::memory_order_relaxed);
}

TaskScheduler::ForegroundWork::~ForegroundWork()
{
    if (g_foregroundDepth.fetch_sub(1, std::memory_order_release) == 1)
    {
        std::lock_guard<std::mutex> lock(g_foregroundMtx);
        g_foregroundCv.notify_all();
    }
}

bool TaskScheduler::foregroundBusy()
{
    return g_foregroundDepth.load(std::memory_order_relaxed) > 0;
}

TaskScheduler::~TaskScheduler()
{
    // Explicitly drain every pool before destroying the QThreadPool objects.
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
    auto prio = toPriority(pool);
    auto idx = static_cast<int>(prio);
    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->priority = prio;
    ctx->pool = pool;
    const TaskId ctx_id = ctx->id;
    auto state = std::make_shared<LambdaState>();
    state->ctx = ctx;
    state->work = [runnable](const TaskContext &)
    {
        auto *r = static_cast<QRunnable *>(runnable);
        r->run();
        // Preserve the QThreadPool::start ownership contract: auto-delete
        if (r->autoDelete())
            delete r;
    };
    auto *wrapped = new LambdaTask(state);
    state->done = [this, ctx, ctx_id, prio]()
    {
        onTaskComplete(ctx_id, prio, false,
                       ctx->execution_failed.load(std::memory_order_relaxed));
        // No user done callback exists for this legacy path.
    };
    state->onStart = [this, ctx_id, prio]() { noteTaskStarted(ctx_id, prio); };
    std::lock_guard<std::mutex> lock(m_graphMtx);
    m_poolState[idx].metrics.submitted++;
    m_poolState[idx].metrics.pending++;
    m_handles[ctx_id] = ctx;
    m_taskPriomap[ctx_id] = prio;
    launchQueued(ReadyLaunch{prio, ctx_id, wrapped});
}

TaskScheduler::TaskHandle
TaskScheduler::submit(Priority prio, std::function<void(const TaskContext &)> work,
                      std::vector<TaskId> deps, std::chrono::steady_clock::time_point deadline,
                      std::function<void()> done, std::function<void(int)> onProgress)
{
    const int pIdx = static_cast<int>(prio);
    assert(pIdx >= 0 && pIdx < 5);

    // M27: an empty work callable is a contract violation, not a runtime
    if (!work)
        return nullptr;

    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->onProgress = std::move(onProgress);
    ctx->dependencies = std::move(deps);
    ctx->deadline = deadline;
    ctx->priority = prio;

    const TaskId ctx_id = ctx->id;
    auto state = std::make_shared<LambdaState>();
    state->ctx = ctx;
    state->work = std::move(work);
    auto *runnable = new LambdaTask(state);
    state->done = [this, done = std::move(done), ctx, prio]()
    {
            // M27: cancelTree() erases the handle and finalizes the metrics
            const bool finalized = onTaskComplete(
                ctx->id, prio, ctx->deadline_exceeded.load(std::memory_order_relaxed),
                ctx->execution_failed.load(std::memory_order_relaxed));
            if (!finalized)
                return;
            try
            {
                if (done)
                    done();
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(m_graphMtx);
                m_poolState[static_cast<int>(prio)].metrics.callback_failures++;
            }
    };
    state->onStart = [this, ctx_id, prio]() { noteTaskStarted(ctx_id, prio); };

    if (!enqueueTask(prio, ctx, runnable))
        return nullptr;
    return ctx;
}

bool TaskScheduler::rejectBackpressure(Priority prio)
{
    const int pIdx = static_cast<int>(prio);
    PoolType pool = MetadataPool;
    bool rejected = false;
    BackPressureFn handler;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        if (wouldRejectBackpressureLocked(pIdx))
        {
            pool = poolFromPriority(prio);
            rejected = true;
        }
        handler = m_backpressure; // copy under the lock, invoke outside it
    }
    if (!rejected || !handler)
        return rejected;
    try
    {
        handler(pool);
    }
    catch (...)
    {
    }
    return true;
}

bool TaskScheduler::wouldRejectBackpressureLocked(int pIdx)
{
    const size_t depth =
        m_poolState[pIdx].metrics.queue_depth + m_poolState[pIdx].metrics.active_tasks;
    if (m_poolState[pIdx].paused ||
        (m_poolState[pIdx].max_queue_depth > 0 && depth >= m_poolState[pIdx].max_queue_depth))
    {
        m_poolState[pIdx].metrics.backpressure_rejected++;
        return true;
    }
    return false;
}

bool TaskScheduler::enqueueTask(Priority prio, const std::shared_ptr<TaskContext> &ctx,
                                QRunnable *runnable)
{
    const int pIdx = static_cast<int>(prio);
    BackPressureFn handler;
    bool rejected = false;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        // Backpressure is decided HERE, under the same lock that publishes the
        if (wouldRejectBackpressureLocked(pIdx))
        {
            rejected = true;
            handler = m_backpressure; // copied under the lock, invoked outside it
        }
        else
        {
            m_poolState[pIdx].metrics.submitted++;
            m_handles[ctx->id] = ctx;
            m_taskPriomap[ctx->id] = prio;
            if (!ctx->dependencies.empty())
            {
                m_poolState[pIdx].metrics.pending++;
                m_poolState[pIdx].metrics.waiting++;
                m_depGraph[ctx->id] = ctx->dependencies;
                for (TaskId dep : ctx->dependencies)
                    m_dependents[dep].push_back(ctx->id);
                m_deferred[ctx->id] = DeferredEntry{prio, runnable, ctx->deadline};
                std::vector<ReadyLaunch> ready;
                releaseReadyTasks(ready);
                for (const auto &item : ready)
                    launchQueued(item);
            }
            else
            {
                m_poolState[pIdx].metrics.pending++;
                launchQueued(ReadyLaunch{prio, ctx->id, runnable});
            }
        }
    }
    if (rejected)
    {
        if (handler)
        {
            try
            {
                handler(poolFromPriority(prio));
            }
            catch (...)
            {
            }
        }
        return false;
    }
    return true;
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

void TaskScheduler::launchQueued(const ReadyLaunch &item)
{
    // Caller holds m_graphMtx. QThreadPool::start does not run the runnable
    const int idx = static_cast<int>(item.prio);
    if (auto it = m_handles.find(item.id); it != m_handles.end() && it->second)
    {
        it->second->enqueuedAt = std::chrono::steady_clock::now();
        it->second->started = false;
    }
    m_poolState[idx].metrics.queue_depth++;
    m_impl->priorityQueues[idx].start(static_cast<QRunnable *>(item.runnable));
}

void TaskScheduler::noteTaskStarted(TaskId id, Priority prio)
{
    std::lock_guard<std::mutex> lock(m_graphMtx);
    auto it = m_handles.find(id);
    if (it == m_handles.end() || !it->second)
        return; // cancelTree already finalized a task that had not started
    if (it->second->started)
        return;
    it->second->started = true;
    const int pIdx = static_cast<int>(prio);
    if (m_poolState[pIdx].metrics.queue_depth > 0)
        m_poolState[pIdx].metrics.queue_depth--;
    m_poolState[pIdx].metrics.active_tasks++;
    const auto wait = std::chrono::steady_clock::now() - it->second->enqueuedAt;
    if (wait.count() > 0)
    {
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(wait).count();
        if (ns > 0)
            m_poolState[pIdx].metrics.total_latency_ns += static_cast<uint64_t>(ns);
    }
}

void TaskScheduler::notifyDrainIfIdle(int idx)
{
    // Caller holds m_graphMtx.
    const auto &metrics = m_poolState[idx].metrics;
    if (metrics.pending == 0 && metrics.active_tasks == 0)
        m_drainCv.notify_all();
}

void TaskScheduler::releaseReadyTasks(std::vector<ReadyLaunch> &out)
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
            auto depsIt = m_depGraph.find(id);
            if (depsIt != m_depGraph.end())
            {
                for (TaskId dep : depsIt->second)
                {
                    auto &followers = m_dependents[dep];
                    followers.erase(std::remove(followers.begin(), followers.end(), id),
                                    followers.end());
                    // M27: drop the now-empty reverse edge so the dependency
                    if (followers.empty())
                        m_dependents.erase(dep);
                }
                m_depGraph.erase(depsIt);
            }
            out.push_back(ReadyLaunch{it->second.prio, id, it->second.runnable});
            it = m_deferred.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool TaskScheduler::onTaskComplete(TaskId id, Priority prio, bool deadlineExceeded,
                                   bool executionFailed)
{
    std::vector<ReadyLaunch> ready;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        // P2-4 fix: if cancelTree() already erased this handle and decremented
        auto it = m_handles.find(id);
        if (it == m_handles.end())
            return false; // cancelTree() already finalized —caller suppresses done
        m_handles.erase(it);
        m_taskPriomap.erase(id);
        m_depGraph.erase(id);
        const int pIdx = static_cast<int>(prio);
        m_poolState[pIdx].metrics.completed++;
        if (deadlineExceeded)
            m_poolState[pIdx].metrics.deadline_exceeded++;
        if (executionFailed)
            m_poolState[pIdx].metrics.execution_failures++;
        // Terminal transition: exactly one decrement of each counter.
        if (m_poolState[pIdx].metrics.pending > 0)
            m_poolState[pIdx].metrics.pending--;
        if (m_poolState[pIdx].metrics.active_tasks > 0)
            m_poolState[pIdx].metrics.active_tasks--;
        // Drop this task from the prerequisite edges of its dependents is not
        releaseReadyTasks(ready);
        for (const auto &item : ready)
            launchQueued(item);
        notifyDrainIfIdle(pIdx);
    }
    return true;
}

void TaskScheduler::cancelTree(TaskId rootId)
{
    auto &sched = instance();
    std::lock_guard<std::mutex> lock(sched.m_graphMtx);

    // M26: walk the REVERSE map —m_dependents[id] lists the tasks waiting on
    std::vector<TaskId> stack;
    std::vector<TaskId> victims;
    std::unordered_set<TaskId> seen;
    stack.push_back(rootId);
    while (!stack.empty())
    {
        TaskId cur = stack.back();
        stack.pop_back();
        if (!seen.insert(cur).second)
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
            sched.notifyDrainIfIdle(pIdx);
        }
        else
        {
            // Live task. Queued (not started) holds queue_depth + pending.
            auto hit = sched.m_handles.find(vic);
            if (hit != sched.m_handles.end())
            {
                const bool started = hit->second && hit->second->started;
                hit->second->requestCancel();
                sched.m_handles.erase(hit);
                sched.m_taskPriomap.erase(vic);
                sched.m_poolState[pIdx].metrics.cancelled++;
                if (started)
                {
                    if (sched.m_poolState[pIdx].metrics.active_tasks > 0)
                        sched.m_poolState[pIdx].metrics.active_tasks--;
                }
                else if (sched.m_poolState[pIdx].metrics.queue_depth > 0)
                {
                    sched.m_poolState[pIdx].metrics.queue_depth--;
                }
                if (sched.m_poolState[pIdx].metrics.pending > 0)
                    sched.m_poolState[pIdx].metrics.pending--;
                sched.notifyDrainIfIdle(pIdx);
            }
        }
        // If the victim itself was waiting on prerequisites, drop it from
        if (auto depIt = sched.m_depGraph.find(vic); depIt != sched.m_depGraph.end())
        {
            for (TaskId dep : depIt->second)
            {
                auto &followers = sched.m_dependents[dep];
                followers.erase(std::remove(followers.begin(), followers.end(), vic),
                                followers.end());
                if (followers.empty())
                    sched.m_dependents.erase(dep);
            }
        }
        sched.m_depGraph.erase(vic);
        sched.m_dependents.erase(vic);
        // Remove this victim from every other task's dependents list so no
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

TaskScheduler::GraphMetrics TaskScheduler::graphMetrics() const
{
    std::lock_guard<std::mutex> lock(m_graphMtx);
    GraphMetrics g;
    g.handles = m_handles.size();
    g.deferred = m_deferred.size();
    g.dep_graph_entries = m_depGraph.size();
    g.dependents_entries = m_dependents.size();
    return g;
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
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    {
        std::unique_lock<std::mutex> lock(m_graphMtx);
        m_drainCv.wait_until(lock, deadline,
                             [&]
                             {
                                 return m_poolState[idx].metrics.pending == 0 &&
                                        m_poolState[idx].metrics.active_tasks == 0;
                             });
    }
    // M27: pass only the REMAINING budget to waitForDone(). Previously the
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    const int remainingMs = std::max(0, static_cast<int>(remaining.count()));
    // Call waitForDone() WITHOUT holding the lock so worker threads can acquire
    return m_impl->priorityQueues[idx].waitForDone(remainingMs);
}

bool TaskScheduler::drain(PoolType pool, std::chrono::milliseconds timeout)
{
    auto idx = static_cast<int>(toPriority(pool));
    return waitForPoolDrained(idx, timeout);
}

void TaskScheduler::shutdown(std::chrono::milliseconds timeout)
{
    // Pause all pools and request cancel on in-flight tasks so workers exit promptly
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        for (int i = 0; i < 5; ++i)
        {
            m_poolState[i].paused = true;
        }
        for (auto &pair : m_handles)
        {
            if (pair.second)
                pair.second->requestCancel();
        }
        // Deferred (never-started) tasks own a QRunnable that would otherwise
        for (auto &entry : m_deferred)
        {
            delete entry.second.runnable;
        }
        m_deferred.clear();
        m_depGraph.clear();
        m_dependents.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_foregroundMtx);
        g_foregroundCv.notify_all();
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
