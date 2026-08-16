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

class QRunnable;

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
    // NOTE: values are used directly as array indices into priorityQueues[5]
    // and m_poolState[5] (via static_cast<int>). They MUST stay contiguous
    // 0..4. Ordering here intentionally mirrors priority (low -> high).
    enum class Priority : int
    {
        Background = 0,
        Analysis = 1,
        Thumbnail = 2,
        Decode = 3,
        UI = 4
    };

    struct TaskContext
    {
        TaskId id = 0;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> progress = std::make_shared<std::atomic<int>>(0);
        std::function<void(int)> onProgress;
        std::vector<TaskId> dependencies;

        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max();
        std::atomic<bool> deadline_exceeded{false};
        // M27: set by the worker when user work throws; surfaced through
        // PoolMetrics::execution_failures at the terminal transition.
        std::atomic<bool> execution_failed{false};

        Priority priority = Priority::Background;
        PoolType pool = MetadataPool;

        void requestCancel()
        {
            *cancel = true;
        }
        bool isCancelled() const
        {
            return cancel->load(std::memory_order_relaxed);
        }
        int currentProgress() const
        {
            return progress->load(std::memory_order_relaxed);
        }
        void reportProgress(int p) const
        {
            const int v = p < 0 ? 0 : (p > 100 ? 100 : p);
            progress->store(v, std::memory_order_relaxed);
            // M27: a throwing onProgress callback must never escape the worker
            // (the deadline path also reports progress before the terminal
            // transition). Progress callbacks are contained by contract; the
            // done-path counterpart is observable via
            // PoolMetrics::callback_failures.
            if (onProgress)
            {
                try
                {
                    onProgress(v);
                }
                catch (...)
                {
                }
            }
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
        // waiting = tasks submitted with dependencies that have not yet been
        // released to a pool (deferred). Tracked so cancelTree and the
        // lifecycle metrics match the real state without counting a waiting
        // task as either queued or active.
        size_t waiting{0};
        // pending = tasks submitted but not yet observed complete. Incremented in
        // submit() for EVERY accepted task (deferred or not); decremented exactly
        // once on the terminal transition (onTaskComplete or cancelTree). Lets
        // waitForPoolDrained() block until EVERY submitted task (including ones
        // submitted concurrently with the drain) has finished, not just until the
        // pool is momentarily idle (which QThreadPool::waitForDone can miss
        // under a submitting producer on another thread -> use-after-free).
        size_t pending{0};
        // M27: user work threw inside a worker (contained; the process must
        // survive). Observable so fault injection can be verified.
        uint64_t execution_failures{0};
        // M27: the user done callback threw (contained; the process must
        // survive).
        uint64_t callback_failures{0};
    };

    // M27: live dependency-graph working set. All counters must return to
    // zero when the scheduler is idle; a nonzero value means bookkeeping
    // leaked (handles/deferred/dependency edges outliving their tasks).
    struct GraphMetrics
    {
        size_t handles{0};
        size_t deferred{0};
        size_t dep_graph_entries{0};
        size_t dependents_entries{0};
    };

    static TaskScheduler &instance();

    void submit(PoolType pool, void *runnable);

    TaskHandle submit(Priority prio, std::function<void(const TaskContext &)> work,
                      std::vector<TaskId> deps = {},
                      std::chrono::steady_clock::time_point deadline =
                          std::chrono::steady_clock::time_point::max(),
                      std::function<void()> done = {}, std::function<void(int)> onProgress = {});

    TaskHandle submit(PoolType pool, std::function<void()> work, std::function<void()> done = {});

    static Priority toPriority(PoolType pool);

    void setQueueMaxThreads(Priority prio, int n);
    void setPoolMaxThreads(PoolType pool, int n);

    void setMaxQueueDepth(PoolType pool, size_t max);
    size_t maxQueueDepth(PoolType pool) const;

    static void cancel(TaskHandle &h)
    {
        if (h)
            h->requestCancel();
    }
    static void cancelTree(TaskId rootId);

    TaskHandle handle(TaskId id);

    PoolMetrics metrics(PoolType pool) const;

    // Live dependency-graph working set (see GraphMetrics).
    GraphMetrics graphMetrics() const;
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
    void setBackPressureHandler(BackPressureFn fn)
    {
        m_backpressure = std::move(fn);
    }

  protected:
    TaskScheduler();
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler &) = delete;
    TaskScheduler &operator=(const TaskScheduler &) = delete;

    struct Impl;
    Impl *m_impl = nullptr;

    static std::atomic<uint64_t> s_nextId;
    // Prerequisite map: taskId -> taskIds it waits for (releaseReadyTasks).
    std::unordered_map<TaskId, std::vector<TaskId>> m_depGraph;
    // Reverse-dependency map: taskId -> taskIds waiting on it. cancelTree BFS
    // walks THIS map so it cancels transitive DEPENDENTS and never the
    // prerequisites of the cancelled task.
    std::unordered_map<TaskId, std::vector<TaskId>> m_dependents;
    std::unordered_map<TaskId, TaskHandle> m_handles;
    std::unordered_map<TaskId, Priority> m_taskPriomap;
    mutable std::mutex m_graphMtx;

    struct DeferredEntry
    {
        Priority prio;
        void *runnable = nullptr;
        std::chrono::steady_clock::time_point deadline;
    };
    std::unordered_map<TaskId, DeferredEntry> m_deferred;

    struct PoolState
    {
        PoolMetrics metrics;
        size_t max_queue_depth = 1000;
        bool paused = false; /// refuses new tasks when true
    };
    PoolState m_poolState[5];

    /// Wait until all active tasks finish or timeout. Returns false on timeout.
    bool waitForPoolDrained(int idx, std::chrono::milliseconds timeout);

    BackPressureFn m_backpressure;

    static PoolType poolFromPriority(Priority p)
    {
        switch (p)
        {
        case Priority::UI:
            return IOPool;
        case Priority::Decode:
            return DecodePool;
        case Priority::Thumbnail:
            return ThumbnailPool;
        case Priority::Analysis:
            return AnalysisPool;
        case Priority::Background:
            return MetadataPool;
        }
        return MetadataPool;
    }

    void releaseReadyTasks(std::vector<std::pair<Priority, void *>> &out);
    bool rejectBackpressure(Priority prio);
    void enqueueTask(Priority prio, const std::shared_ptr<TaskContext> &ctx, QRunnable *runnable);
    // Returns false when the task was already finalized by cancelTree() (the
    // caller must then suppress the user done callback). executionFailed feeds
    // PoolMetrics::execution_failures.
    bool onTaskComplete(TaskId id, Priority prio, bool deadlineExceeded,
                        bool executionFailed = false);
};
