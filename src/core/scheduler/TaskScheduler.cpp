#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <algorithm>
#include <memory>

namespace
{
// Owns the QRunnable plus the priority / done callback the scheduler
// uses to know when it's time to launch this task. Tasks with deps are
// enqueued in m_deferredTasks instead of the thread pool, so no thread
// blocks just spinning on dependencies.
struct DeferredEntry
{
    TaskScheduler::Priority prio;
    QRunnable* runnable = nullptr;
};

class LambdaTask : public QRunnable
{
public:
    LambdaTask(TaskScheduler::TaskHandle ctx,
        std::function<void(const TaskScheduler::TaskContext&)> work,
        std::function<void()> done)
        : m_ctx(std::move(ctx))
        , m_work(std::move(work))
        , m_done(std::move(done))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        if (m_work && !m_ctx->isCancelled())
            m_work(*m_ctx);
        m_ctx->reportProgress(100);
        if (m_done)
            m_done();
    }

private:
    TaskScheduler::TaskHandle m_ctx;
    std::function<void(const TaskScheduler::TaskContext&)> m_work;
    std::function<void()> m_done;
};
} // namespace

struct TaskScheduler::Impl
{
    static constexpr int kNumQueues = 5;
    QThreadPool priorityQueues[kNumQueues];
};

std::atomic<uint64_t> TaskScheduler::s_nextId{1};

TaskScheduler& TaskScheduler::instance()
{
    static TaskScheduler inst;
    return inst;
}

TaskScheduler::TaskScheduler()
    : m_impl(new Impl)
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

void TaskScheduler::submit(PoolType pool, void* runnable)
{
    m_impl->priorityQueues[static_cast<int>(toPriority(pool))].start(
        static_cast<QRunnable*>(runnable));
}

// A dep is satisfied once its handle is gone (cancelled / completed) or
// its progress reached 100.
static bool depDone(const std::unordered_map<uint64_t, TaskScheduler::TaskHandle>& handles,
    uint64_t id)
{
    auto it = handles.find(id);
    if (it == handles.end())
        return true;
    if (it->second->isCancelled())
        return true;
    return it->second->currentProgress() >= 100;
}

// Scan deferred tasks — launch any whose deps are now all satisfied.
// Caller must hold m_graphMtx.
void TaskScheduler::releaseReadyTasks(std::vector<std::pair<Priority, void*>>& out)
{
    if (m_deferred.empty())
        return;

    for (auto it = m_deferred.begin(); it != m_deferred.end();)
    {
        const auto& deps = m_depGraph[it->first];
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

void TaskScheduler::onTaskComplete(TaskId id)
{
    std::vector<std::pair<Priority, void*>> ready;
    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_handles.erase(id);
        m_depGraph.erase(id);
        releaseReadyTasks(ready);
    }
    // Launch outside the lock to avoid re-entering the scheduler.
    for (auto& [prio, r] : ready)
        m_impl->priorityQueues[static_cast<int>(prio)].start(static_cast<QRunnable*>(r));
}

TaskScheduler::TaskHandle TaskScheduler::submit(Priority prio,
    std::function<void(const TaskContext&)> work,
    std::vector<TaskId> deps,
    std::function<void()> done,
    std::function<void(int)> onProgress)
{
    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->onProgress = std::move(onProgress);
    ctx->dependencies = std::move(deps);

    const TaskId ctx_id = ctx->id;
    auto* runnable = new LambdaTask(ctx, std::move(work), [this, done = std::move(done), ctx_id]() {
        if (done)
        {
            QObject* tgt = QCoreApplication::instance();
            if (tgt)
            {
                QMetaObject::invokeMethod(
                    tgt, [d = std::move(done)]() { d(); }, Qt::QueuedConnection);
            }
            else
            {
                done();
            }
        }
        onTaskComplete(ctx_id);
    });

    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        m_handles[ctx_id] = ctx;
        if (!ctx->dependencies.empty())
        {
            m_depGraph[ctx_id] = ctx->dependencies;
            m_deferred[ctx_id] = DeferredEntry{prio, runnable};
            // If any deps already finished between submission and here,
            // launch all now-ready deferred tasks with their own priority.
            std::vector<std::pair<Priority, void*>> ready;
            releaseReadyTasks(ready);
            for (auto& [p, r] : ready)
                m_impl->priorityQueues[static_cast<int>(p)].start(static_cast<QRunnable*>(r));
        }
        else
        {
            m_impl->priorityQueues[static_cast<int>(prio)].start(runnable);
        }
    }
    return ctx;
}

TaskScheduler::TaskHandle
TaskScheduler::submit(PoolType pool, std::function<void()> work, std::function<void()> done)
{
    return submit(toPriority(pool),
        [w = std::move(work)](const TaskContext&) {
            if (w)
                w();
        },
        {},
        std::move(done),
        {});
}

void TaskScheduler::setQueueMaxThreads(Priority prio, int n)
{
    m_impl->priorityQueues[static_cast<int>(prio)].setMaxThreadCount(std::max(1, n));
}

void TaskScheduler::setPoolMaxThreads(PoolType pool, int n)
{
    setQueueMaxThreads(toPriority(pool), n);
}

void TaskScheduler::cancelTree(TaskId rootId)
{
    auto& sched = instance();
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
        auto hit = sched.m_handles.find(*it);
        if (hit != sched.m_handles.end())
        {
            hit->second->requestCancel();
            sched.m_handles.erase(hit);
        }
        // Any deferred task matching this id is dropped; its QRunnable
        // was never started, so we delete it here. Stored as void*, so
        // cast back to QRunnable* for the destructor to run correctly.
        auto dit = sched.m_deferred.find(*it);
        if (dit != sched.m_deferred.end())
        {
            delete static_cast<QRunnable*>(dit->second.runnable);
            sched.m_deferred.erase(dit);
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
