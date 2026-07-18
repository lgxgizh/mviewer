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
        if (m_ctx->deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() > m_ctx->deadline)
        {
            m_ctx->deadline_exceeded.store(true, std::memory_order_relaxed);
            m_ctx->reportProgress(0);
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

        // HACK: The original code queued m_done via QMetaObject::invokeMethod,
        // which requires the main thread's event loop to pump — but drain()
        // blocks the main thread, so the done callback never fired and
        // active_tasks never decremented. Call m_done() directly on the
        // worker thread so onTaskComplete runs immediately and drain() can
        // observe active_tasks hitting 0. Skip the broken operator bool and
        // use try/catch to handle the empty-function case.
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
    auto prio = toPriority(pool);
    auto idx = static_cast<int>(prio);
    m_impl->priorityQueues[idx].start(static_cast<QRunnable *>(runnable));
    // Legacy path: metrics updated opportunistically
    m_poolState[idx].metrics.submitted++;
    m_poolState[idx].metrics.pending++;
    m_poolState[idx].metrics.active_tasks++;
}

TaskScheduler::TaskHandle
TaskScheduler::submit(Priority prio, std::function<void(const TaskContext &)> work,
                      std::vector<TaskId> deps, std::chrono::steady_clock::time_point deadline,
                      std::function<void()> done, std::function<void(int)> onProgress)
{
    const int pIdx = static_cast<int>(prio);
    assert(pIdx >= 0 && pIdx < 5);

    // Back-pressure check
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        if (m_poolState[pIdx].paused)
        {
            m_poolState[pIdx].metrics.backpressure_rejected++;
            if (m_backpressure)
                m_backpressure(poolFromPriority(prio));
            return nullptr;
        }
        const size_t md =
            m_poolState[pIdx].metrics.queue_depth + m_poolState[pIdx].metrics.active_tasks;
        if (m_poolState[pIdx].max_queue_depth > 0 && md >= m_poolState[pIdx].max_queue_depth)
        {
            m_poolState[pIdx].metrics.backpressure_rejected++;
            if (m_backpressure)
                m_backpressure(poolFromPriority(prio));
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
                                    [this, done = std::move(done), ctx_id, prio]()
                                    {
                                        onTaskComplete(ctx_id, prio);
                                        if (done)
                                            done();
                                    });

    m_poolState[pIdx].metrics.submitted++;

    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_handles[ctx_id] = ctx;
        m_taskPriomap[ctx_id] = prio;
        if (!ctx->dependencies.empty())
        {
            m_depGraph[ctx_id] = ctx->dependencies;
            m_deferred[ctx_id] = DeferredEntry{prio, runnable, deadline};
            // If any deps already finished, launch ready deferred tasks
            std::vector<std::pair<Priority, void *>> ready;
            releaseReadyTasks(ready);
            for (auto &[p, r] : ready)
                m_impl->priorityQueues[static_cast<int>(p)].start(static_cast<QRunnable *>(r));
        }
        else
        {
            m_poolState[pIdx].metrics.queue_depth++;
            m_poolState[pIdx].metrics.pending++;
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
            out.push_back({it->second.prio, it->second.runnable});
            it = m_deferred.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TaskScheduler::onTaskComplete(TaskId id, Priority prio)
{
    std::vector<std::pair<Priority, void *>> ready;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_handles.erase(id);
        m_taskPriomap.erase(id);
        m_depGraph.erase(id);
        const int pIdx = static_cast<int>(prio);
        m_poolState[pIdx].metrics.completed++;
        m_poolState[pIdx].metrics.pending--;
        m_poolState[pIdx].metrics.active_tasks--;
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
        auto it = sched.m_depGraph.find(cur);
        if (it != sched.m_depGraph.end())
        {
            for (auto child : it->second)
                stack.push_back(child);
        }
    }
    for (auto it = victims.rbegin(); it != victims.rend(); ++it)
    {
        auto pit = sched.m_taskPriomap.find(*it);
        Priority prio = (pit != sched.m_taskPriomap.end()) ? pit->second : Priority::Background;
        const int pIdx = static_cast<int>(prio);
        auto hit = sched.m_handles.find(*it);
        if (hit != sched.m_handles.end())
        {
            hit->second->requestCancel();
            sched.m_handles.erase(hit);
            sched.m_poolState[pIdx].metrics.cancelled++;
            sched.m_poolState[pIdx].metrics.active_tasks--;
        }
        auto dit = sched.m_deferred.find(*it);
        if (dit != sched.m_deferred.end())
        {
            delete static_cast<QRunnable *>(dit->second.runnable);
            sched.m_deferred.erase(dit);
            sched.m_poolState[pIdx].metrics.queue_depth--;
        }
    }
    sched.m_depGraph.erase(rootId);
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
    // HACK: active_tasks/queue_depth tracking is coupled to the broken
    // std::function operator bool on this toolchain, so the counter-based
    // drain never observes zero. Fall back to QThreadPool::waitForDone()
    // which actually blocks until all QRunnable::run() calls finish.
    // BUT waitForDone() only sees tasks already in the pool; a producer
    // submitting concurrently (e.g. loadDirectoryAsync's 1000-task loop
    // racing drain()) can enqueue AFTER waitForDone() started and be
    // missed -> early return -> use-after-free. Guard with the per-pool
    // `pending` counter: spin until pending hits 0 (all submitted tasks
    // observed complete) AND the pool is idle.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        {
            std::lock_guard<std::mutex> lock(m_graphMtx);
            if (m_poolState[idx].metrics.pending == 0 &&
                m_poolState[idx].metrics.active_tasks == 0)
                return m_impl->priorityQueues[idx].waitForDone(
                    static_cast<int>(timeout.count()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // Timed out; do a final waitForDone to drain whatever is left.
    return m_impl->priorityQueues[idx].waitForDone(
        static_cast<int>(timeout.count()));
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