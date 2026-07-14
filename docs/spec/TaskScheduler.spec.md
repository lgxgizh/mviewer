# TaskScheduler Specification

## Module
TaskScheduler + TaskContext + TaskHandle (Priority, PoolType, TaskId)

## Purpose
TaskScheduler is the unified priority task scheduler: all background work flows through here. It routes tasks to 5 independent thread pools by Priority (UI, Decode, Thumbnail, Analysis, Background). Each task owns a TaskId, cancel token, progress counter, and dependency list. `cancelTree()` cancels a task plus all transitive dependents (BFS over the dependency graph).

## API

```cpp
class TaskScheduler {
public:
    using TaskId = uint64_t;

    enum PoolType { MetadataPool, DecodePool, ThumbnailPool, AnalysisPool, IOPool };
    enum class Priority : int { UI, Decode, Thumbnail, Analysis, Background };

    // Per-task runtime context (thread-safe).
    struct TaskContext {
        TaskId id = 0;
        std::shared_ptr<std::atomic<bool>> cancel = std::make_shared<std::atomic<bool>>(false);
        std::shared_ptr<std::atomic<int>> progress = std::make_shared<std::atomic<int>>(0);
        std::function<void(int)> onProgress;
        std::vector<TaskId> dependencies;

        void requestCancel();
        bool isCancelled() const;
        int currentProgress() const;
        void reportProgress(int p);
    };
    using TaskHandle = std::shared_ptr<TaskContext>;

    static TaskScheduler& instance();

    // Legacy QRunnable submit (no priority/deps)
    void submit(PoolType pool, QRunnable* task);

    // Priority submit with dependency list (RFC-004)
    TaskHandle submit(Priority prio,
                      std::function<void(const TaskContext&)> work,
                      std::vector<TaskId> deps = {},
                      std::function<void()> done = {},
                      std::function<void(int)> onProgress = {});

    // Compat void() submit
    TaskHandle submit(PoolType pool,
                      std::function<void()> work,
                      std::function<void()> done = {});

    static Priority toPriority(PoolType pool);
    void setQueueMaxThreads(Priority prio, int n);
    void setPoolMaxThreads(PoolType pool, int n);

    // Cancel a single task's token
    static void cancel(TaskHandle& h);

    // Cancel a task + all transitive dependents (BFS over dep graph)
    static void cancelTree(TaskId rootId);

    // Look up a live task handle by id
    TaskHandle handle(TaskId id);
};
```

## Input

| Parameter | Type | Constraints | Default |
|-----------|------|-------------|---------|
| `prio` | `Priority` | — | — |
| `pool` | `PoolType` | — | — |
| `work` | `function<void(TaskContext&)>` | Non-null | — |
| `deps` | `vector<TaskId>` | Existing task IDs; cycle not checked | `{}` |
| `done` | `function<void()>` | Invoked on main thread | `{}` |
| `onProgress` | `function<void(int)>` | 0-100 range | `{}` |
| `n` | `int` | >0 | — |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `submit` | `TaskHandle` | Handle for cancel/progress; null on failure |
| `cancel(TaskHandle&)` | `void` | Sets cancel token |
| `cancelTree(TaskId)` | `void` | BFS cancel |
| `handle(TaskId)` | `TaskHandle` | Null if not found or expired |
| `toPriority(PoolType)` | `Priority` | Static mapping |

## Ownership

- TaskScheduler **owns** the 5 QThreadPool instances and the dependency graph.
- Caller receives `shared_ptr<TaskContext>` (shared ownership of context).
- TaskContext owns cancel token, progress counter (shared_ptr so they outlive task).
- Dependency graph stores weak refs via TaskHandle (no cycles at type level).

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `submit` | Any thread | Mutex-protected dep graph + pool queues |
| `cancel/cancelTree` | Any thread | Atomic cancel + graph BFS under mutex |
| `handle` | Any thread | Graph lookup under mutex |
| `reportProgress` | Worker thread | Atomic store |

## Memory

| Operation | Dominant Allocation |
|-----------|---------------------|
| `submit` | 1 × TaskContext (~200 bytes + function captures) |
| `cancelTree` | O(k) BFS where k = dependents |
| `handle` | shared_ptr copy |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| `submit` | <0.1 ms | queue only |
| `cancelTree(k deps)` | <0.05 ms | atomic ops only |
| Latency to start (no deps) | <5 ms | QThreadPool dispatch |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| null work | Invalid input | Return null handle |
| dependency not found | Unknown TaskId | Log warning; treat as no-dep (run immediately) |
| deadlock via circular deps | Bad usage | Not detected; use `cancelTree` to break |
| pool full | Exceeds maxThreads | Task queued when thread available |

## Examples

```cpp
// Simple background task
auto h = TaskScheduler::instance().submit(
    TaskScheduler::Priority::Analysis,
    [](const TaskContext& ctx) {
        for (int i = 0; i <= 100; i += 10) {
            ctx.reportProgress(i);
            if (ctx.isCancelled()) return;
            std::this_thread::sleep_for(50ms);
        }
    },
    {}, // no deps
    []() { std::cout << "done\n"; }
);

// Chain: thumbnails after decode
auto decode = TaskScheduler::instance().submit(
    TaskScheduler::Priority::Decode,
    [](const TaskContext&) { /* decode pixels */ });
auto thumb = TaskScheduler::instance().submit(
    TaskScheduler::Priority::Thumbnail,
    [](const TaskContext&) { /* make thumbnail */ },
    {decode->id} // waits for decode
);

// Cancel an entire subtree
TaskScheduler::cancelTree(decode->id);
```

## Unit Tests

```cpp
TEST(Scheduler, SubmitStartsAndFinishes) {
    std::atomic<bool> ran{false};
    auto h = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [&](const TaskContext&) { ran = true; });
    std::this_thread::sleep_for(200ms);
    EXPECT_TRUE(ran);
}

TEST(Scheduler, DependencyGatesStart) {
    std::atomic<bool> first{false};
    auto a = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [&](const TaskContext&) { first = true; std::this_thread::sleep_for(100ms); });
    auto b = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [&](const TaskContext&) { EXPECT_TRUE(first); },
        {a->id});
    std::this_thread::sleep_for(500ms);
}

TEST(Scheduler, CancelStopsWork) {
    auto h = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [](const TaskContext& ctx) {
            std::this_thread::sleep_for(1000ms);
            EXPECT_TRUE(ctx.isCancelled());
        });
    TaskScheduler::cancel(h);
    std::this_thread::sleep_for(200ms);
}

TEST(Scheduler, CancelTreeCancelsDependents) {
    auto a = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [](const TaskContext&) { std::this_thread::sleep_for(1000ms); });
    auto b = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [](const TaskContext&) {},
        {a->id});
    TaskScheduler::cancelTree(a->id);
    EXPECT_TRUE(a->isCancelled());
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Scheduler::submit(Background)`.

## Future Extension

- Visual task monitor (GUI showing per-queue depth, progress bar)
- Work stealing across pools (idle threads help busy pools)
- Task priority inheritance (dependent promotes parent)
- Network task pool with rate limiting (GitHub, cloud storage)
- Historical task analytics (avg duration, bottlenecks)
