#pragma once

#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstdint>

// Unified priority task scheduler: all background work flows through here.
// Routes to 5 independent thread pools by Priority.
//
// Qt headers (QRunnable/QThreadPool) are kept in the .cpp to avoid leaking
// Qt into core-layer headers. This class owns the threading primitives
// via PIMPL (internals in TaskScheduler.cpp).
class TaskScheduler
{
public:
    using TaskId = uint64_t;

    enum PoolType { MetadataPool, DecodePool, ThumbnailPool, AnalysisPool, IOPool };
    enum class Priority : int { UI, Decode, Thumbnail, Analysis, Background };

    // Per-task runtime context: cancel token + progress + dependencies.
    // All fields are thread-safe.
    struct TaskContext {
        TaskId id = 0;
        std::shared_ptr<std::atomic<bool>> cancel =
            std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> progress =
            std::make_shared<std::atomic<int>>(0);
        std::function<void(int)> onProgress;

        // Tasks whose completion gates this one (RFC-004 dependency).
        std::vector<TaskId> dependencies;

        void requestCancel() { *cancel = true; }
        bool isCancelled() const { return cancel->load(std::memory_order_relaxed); }
        int currentProgress() const { return progress->load(std::memory_order_relaxed); }
        void reportProgress(int p) {
            const int v = p < 0 ? 0 : (p > 100 ? 100 : p);
            progress->store(v, std::memory_order_relaxed);
            if (onProgress) onProgress(v);
        }
    };
    using TaskHandle = std::shared_ptr<TaskContext>;

    static TaskScheduler& instance();

    // Legacy QRunnable submit.
    // NOTE: takes a raw QRunnable* — callers must include <QRunnable> from .cpp
    void submit(PoolType pool, void* runnable); // void* to avoid QRunnable in header

    // Priority submit with dependency list (RFC-004). Work starts only after
    // all dependencies finish. Work/done/onProgress: done is dispatched back
    // to the QCoreApplication event loop.
    TaskHandle submit(Priority prio,
                      std::function<void(const TaskContext&)> work,
                      std::vector<TaskId> deps = {},
                      std::function<void()> done = {},
                      std::function<void(int)> onProgress = {});

    // Compat submit for void() work (no priority context).
    TaskHandle submit(PoolType pool,
                      std::function<void()> work,
                      std::function<void()> done = {});

    // Map legacy PoolType to normalized Priority.
    static Priority toPriority(PoolType pool);

    // Per-queue thread cap.
    void setQueueMaxThreads(Priority prio, int n);
    void setPoolMaxThreads(PoolType pool, int n);

    // Cancel just a task's own token.
    static void cancel(TaskHandle& h) { if (h) h->requestCancel(); }

    // Cancel a task and recursively all tasks that depend on it (BFS over dep graph).
    static void cancelTree(TaskId rootId);

    // Look up a live task handle by id.
    TaskHandle handle(TaskId id);

private:
    TaskScheduler();
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    struct Impl; // hides QThreadPool from this header
    Impl* m_impl = nullptr;

    static std::atomic<uint64_t> s_nextId;
    std::unordered_map<TaskId, std::vector<TaskId>> m_depGraph;
    std::unordered_map<TaskId, TaskHandle> m_handles;
    mutable std::mutex m_graphMtx;
};
