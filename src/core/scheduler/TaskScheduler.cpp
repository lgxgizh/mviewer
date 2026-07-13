#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <memory>
#include <algorithm>

namespace
{
// Lambda wrapper: polls deps in waitForDeps(), then runs work.
// After work, reports progress=100, then dispatches done via QueuedConnection.
class LambdaTask : public QRunnable
{
public:
    LambdaTask(TaskScheduler::TaskHandle ctx,
               std::function<void(const TaskScheduler::TaskContext&)> work,
               std::function<void()> done,
               TaskScheduler* sched,
               std::vector<TaskScheduler::TaskId> deps)
        : m_ctx(std::move(ctx)), m_work(std::move(work)), m_done(std::move(done)),
          m_sched(sched), m_deps(std::move(deps))
    {
        setAutoDelete(true);
    }

    // Called by QThreadPool BEFORE run() for dependency tasks.
    // We override start() by hooking run() itself — QThreadPool calls run().
    // So this is called from inside run() before work executes.
    void waitForDeps()
    {
        if (m_deps.empty()) return;
        // Poll: wait until every dependency handle is gone (finished/cancelled).
        size_t resolved = 0;
        while (resolved < m_deps.size()) {
            resolved = 0;
            for (auto id : m_deps) {
                auto h = m_sched->handle(id);
                if (!h) { resolved++; continue; }
                if (h->isCancelled()) { resolved++; continue; }
                if (h->currentProgress() >= 100) { resolved++; continue; }
            }
            if (resolved < m_deps.size())
                QThread::msleep(1);
        }
    }

    void run() override
    {
        waitForDeps();
        if (m_work && !m_ctx->isCancelled())
            m_work(*m_ctx);
        m_ctx->reportProgress(100);
        if (m_done) {
            QObject* tgt = QCoreApplication::instance();
            if (tgt) {
                QMetaObject::invokeMethod(
                    tgt, [d = std::move(m_done)]() { d(); },
                    Qt::QueuedConnection);
            } else {
                m_done();
            }
        }
    }

private:
    TaskScheduler::TaskHandle m_ctx;
    std::function<void(const TaskScheduler::TaskContext&)> m_work;
    std::function<void()> m_done;
    TaskScheduler* m_sched;
    std::vector<TaskScheduler::TaskId> m_deps;
};
} // namespace

std::atomic<uint64_t> TaskScheduler::s_nextId{1};

TaskScheduler& TaskScheduler::instance()
{
    static TaskScheduler inst;
    return inst;
}

TaskScheduler::TaskScheduler()
{
    const int n = QThread::idealThreadCount();
    setQueueMaxThreads(Priority::UI, std::max(1, n));
    setQueueMaxThreads(Priority::Decode, std::max(1, n));
    setQueueMaxThreads(Priority::Thumbnail, std::max(2, n));
    setQueueMaxThreads(Priority::Analysis, std::max(1, n / 2));
    setQueueMaxThreads(Priority::Background, std::max(1, n / 2));
}

TaskScheduler::Priority TaskScheduler::toPriority(PoolType pool)
{
    switch (pool) {
    case IOPool:        return Priority::UI;
    case DecodePool:    return Priority::Decode;
    case ThumbnailPool: return Priority::Thumbnail;
    case AnalysisPool:  return Priority::Analysis;
    case MetadataPool:  return Priority::Background;
    }
    return Priority::Background;
}

void TaskScheduler::submit(PoolType p, QRunnable* task)
{
    pool(p)->start(task);
}

TaskScheduler::TaskHandle TaskScheduler::submit(
    Priority prio,
    std::function<void(const TaskContext&)> work,
    std::vector<TaskId> deps,
    std::function<void()> done,
    std::function<void(int)> onProgress)
{
    auto ctx = std::make_shared<TaskContext>();
    ctx->id = s_nextId.fetch_add(1);
    ctx->onProgress = std::move(onProgress);
    ctx->dependencies = std::move(deps);

    {
        std::lock_guard<std::mutex> lock(m_graphMtx);
        if (!ctx->dependencies.empty())
            m_depGraph[ctx->id] = ctx->dependencies;
        m_handles[ctx->id] = ctx;
    }

    // For tasks with deps, use the LambdaTask that polls before starting work.
    // For tasks with no deps, still use LambdaTask but with empty deps (waitForDeps returns immediately).
    pool(prio)->start(new LambdaTask(ctx, std::move(work), std::move(done), this, ctx->dependencies));
    return ctx;
}

TaskScheduler::TaskHandle TaskScheduler::submit(PoolType pool,
                                                std::function<void()> work,
                                                std::function<void()> done)
{
    return submit(toPriority(pool),
                  [w = std::move(work)](const TaskContext&) { if (w) w(); },
                  {}, std::move(done), {});
}

void TaskScheduler::setQueueMaxThreads(Priority prio, int n)
{
    pool(prio)->setMaxThreadCount(std::max(1, n));
}

void TaskScheduler::setPoolMaxThreads(PoolType pool, int n)
{
    setQueueMaxThreads(toPriority(pool), n);
}

void TaskScheduler::cancelTree(TaskId rootId)
{
    auto& sched = instance();
    std::lock_guard<std::mutex> lock(sched.m_graphMtx);

    // BFS to find all descendants (tasks depending on rootId, recursively).
    std::vector<TaskId> stack;
    std::vector<TaskId> victims;
    stack.push_back(rootId);
    while (!stack.empty()) {
        TaskId cur = stack.back();
        stack.pop_back();
        // Avoid cycles
        if (std::find(victims.begin(), victims.end(), cur) != victims.end())
            continue;
        victims.push_back(cur);
        auto it = sched.m_depGraph.find(cur);
        if (it != sched.m_depGraph.end()) {
            for (auto child : it->second)
                stack.push_back(child);
        }
    }
    // Cancel children first (reverse order), then remove from handle map.
    for (auto it = victims.rbegin(); it != victims.rend(); ++it) {
        auto hit = sched.m_handles.find(*it);
        if (hit != sched.m_handles.end()) {
            hit->second->requestCancel();
            sched.m_handles.erase(hit);
        }
    }
    sched.m_depGraph.erase(rootId);
}

TaskScheduler::TaskHandle TaskScheduler::handle(TaskId id)
{
    std::lock_guard<std::mutex> lock(m_graphMtx);
    auto it = m_handles.find(id);
    if (it == m_handles.end()) return nullptr;
    return it->second;
}
