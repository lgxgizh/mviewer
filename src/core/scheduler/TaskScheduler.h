#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

class TaskScheduler
{
public:
    using TaskId = uint64_t;

    enum PoolType
    {
        MetadataPool,
        DecodePool,
        ThumbnailPool,
        AnalysisPool,
        IOPool
    };
    enum class Priority : int
    {
        Background = 0,
        Analysis = 25,
        Thumbnail = 50,
        Decode = 75,
        UI = 100
    };

    struct TaskContext
    {
        TaskId id = 0;
        std::shared_ptr<std::atomic<bool>> cancel =
            std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> progress =
            std::make_shared<std::atomic<int>>(0);
        std::function<void(int)> onProgress;
        std::vector<TaskId> dependencies;

        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max();
        std::atomic<bool> deadline_exceeded{false};

        Priority priority = Priority::Background;
        PoolType pool = MetadataPool;

        void requestCancel() { *cancel = true; }
        bool isCancelled() const { return cancel->load(std::memory_order_relaxed); }
        int currentProgress() const { return progress->load(std::memory_order_relaxed); }
        void reportProgress(int p)
        {
            const int v = p < 0 ? 0 : (p > 100 ? 100 : p);
            progress->store(v, std::memory_order_relaxed);
            if (onProgress)
                onProgress(v);
        }
        bool isExpired() const
        {
            return deadline != std::chrono::steady_clock::time_point::max() &&
                   std::chrono::steady_clock::now() > deadline;
        }
    };
    using TaskHandle = std::shared_ptr<TaskContext>;

    struct PoolMetrics
    {
        uint64_t submitted{0};
        uint64_t completed{0};
        uint64_t cancelled{0};
        uint64_t deadline_exceeded{0};
        uint64_t backpressure_rejected{0};
        uint64_t total_latency_ns{0};
        size_t active_tasks{0};
        size_t queue_depth{0};
    };

    static TaskScheduler& instance();

    void submit(PoolType pool, void* runnable);

    TaskHandle submit(
        Priority prio,
        std::function<void(const TaskContext&)> work,
        std::vector<TaskId> deps = {},
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max(),
        std::function<void()> done = {},
        std::function<void(int)> onProgress = {});

    TaskHandle submit(PoolType pool, std::function<void()> work,
                      std::function<void()> done = {});

    static Priority toPriority(PoolType pool);

    void setQueueMaxThreads(Priority prio, int n);
    void setPoolMaxThreads(PoolType pool, int n);

    void setMaxQueueDepth(PoolType pool, size_t max);
    size_t maxQueueDepth(PoolType pool) const;

    static void cancel(TaskHandle& h) { if (h) h->requestCancel(); }
    static void cancelTree(TaskId rootId);

    TaskHandle handle(TaskId id);

    PoolMetrics metrics(PoolType pool) const;
    bool isSaturated(PoolType pool) const;
    size_t queueDepth(PoolType pool) const;
    size_t activeTaskCount(PoolType pool) const;

    // Pause / resume a pool. Paused pools hold accepts no new tasks (submit
    // returns nullptr with back-pressure callback). Running tasks complete
    // normally.
    void pause(PoolType pool);
    void resume(PoolType pool);

    // Block until all active tasks in a pool finish, or timeout elapses.
    // Returns true if drained, false on timeout.
    bool drain(PoolType pool, std::chrono::milliseconds timeout);

    // Shutdown: pause all pools, drain each with timeout. For app exit.
    void shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5));

    using BackPressureFn = std::function<void(PoolType)>;
    void setBackPressureHandler(BackPressureFn fn) { m_backpressure = std::move(fn); }

protected:
    TaskScheduler();
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    struct Impl;
    Impl* m_impl = nullptr;

    static std::atomic<uint64_t> s_nextId;
    std::unordered_map<TaskId, std::vector<TaskId>> m_depGraph;
    std::unordered_map<TaskId, TaskHandle> m_handles;
    std::unordered_map<TaskId, Priority> m_taskPriomap;
    mutable std::mutex m_graphMtx;

    struct DeferredEntry
    {
        Priority prio;
        void* runnable = nullptr;
        std::chrono::steady_clock::time_point deadline;
    };
    std::unordered_map<TaskId, DeferredEntry> m_deferred;

    struct PoolState
    {
        PoolMetrics metrics;
        size_t max_queue_depth = 1000;
        bool paused = false;   /// refuses new tasks when true
    };
    PoolState m_poolState[5];

    /// Wait until all active tasks finish or timeout. Returns false on timeout.
    bool waitForPoolDrained(int idx, std::chrono::milliseconds timeout);

    BackPressureFn m_backpressure;

    static PoolType poolFromPriority(Priority p) {
        switch (p) {
        case Priority::UI:        return IOPool;
        case Priority::Decode:    return DecodePool;
        case Priority::Thumbnail: return ThumbnailPool;
        case Priority::Analysis:  return AnalysisPool;
        case Priority::Background: return MetadataPool;
        }
        return MetadataPool;
    }

    void releaseReadyTasks(std::vector<std::pair<Priority, void*>>& out);
    void onTaskComplete(TaskId id, Priority prio);
};
