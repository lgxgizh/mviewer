#include "TaskScheduler.h"

// Pool-state policy and diagnostic snapshots stay independent of QThreadPool
// implementation details. Definitions preserve their existing locking contract.

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

size_t TaskScheduler::activeTaskCount(PoolType pool) const
{
    auto idx = static_cast<int>(toPriority(pool));
    std::lock_guard<std::mutex> lock(m_graphMtx);
    return m_poolState[idx].metrics.active_tasks;
}
