# TaskScheduler Specification

## Module

TaskScheduler + TaskContext + TaskHandle (Priority, PoolType, TaskId)

## Purpose

TaskScheduler is the unified priority task scheduler: all background work flows through here. It routes tasks to 5 independent thread pools by Priority (UI, Decode, Thumbnail, Analysis, Background). Each task owns a TaskId, cancel token, progress counter, and dependency list. `cancelTree()` cancels a task plus all transitive dependents (BFS over the reverse-dependency map).

## Lifecycle (M26)

Every accepted task transitions through exactly one terminal path:

```
submitted ──(no deps)──> queued ──> active(running) ──> terminal
   │                                                          │
   └────────(deps)──> waiting ──> queued ──> active(running) ─┘
```

- `pending` = submitted but not terminal (incremented for EVERY accepted task at
  submit; decremented exactly once at the terminal transition — completion,
  cancellation, or deadline expiry).
- `waiting` = deferred (dependency-satisfaction) count.
- `active_tasks` = tasks handed to a pool (running or pool-queued).
- `queue_depth` = transient pool-queue counter.
- A deadline that has already expired when the task reaches a worker skips the
  work but STILL finalizes: `deadline_exceeded` metric increments, the handle is
  removed, `pending`/`active_tasks` return to zero and the `done` callback runs.
- No counter can underflow: each is only modified at its owning transition.

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
        std::chrono::steady_clock::time_point deadline;
        std::atomic<bool> deadline_exceeded{false};

        void requestCancel();
        bool isCancelled() const;
        int currentProgress() const;
        void reportProgress(int p);
    };
    using TaskHandle = std::shared_ptr<TaskContext>;

    static TaskScheduler& instance();

    // Legacy QRunnable submit (no priority/deps) — still fully lifecycle-tracked.
    void submit(PoolType pool, void* runnable);

    // Priority submit with dependency list (RFC-004)
    TaskHandle submit(Priority prio,
                      std::function<void(const TaskContext&)> work,
                      std::vector<TaskId> deps = {},
                      std::chrono::steady_clock::time_point deadline = time_point::max(),
                      std::function<void()> done = {},
                      std::function<void(int)> onProgress = {});

    // Compat void() submit
    TaskHandle submit(PoolType pool,
                      std::function<void()> work,
                      std::function<void()> done = {});

    static Priority toPriority(PoolType pool);
    void setQueueMaxThreads(Priority prio, int n);
    void setPoolMaxThreads(PoolType pool, int n);
    void setMaxQueueDepth(PoolType pool, size_t max);
    size_t maxQueueDepth(PoolType pool) const;

    // Cancel a single task's token
    static void cancel(TaskHandle& h);

    // Cancel a task + all transitive dependents (BFS over the reverse map).
    // Cancelled tasks never run and their done callback never fires.
    static void cancelTree(TaskId rootId);

    // Look up a live task handle by id
    TaskHandle handle(TaskId id);

    PoolMetrics metrics(PoolType pool) const;
    bool isSaturated(PoolType pool) const;
    size_t queueDepth(PoolType pool) const;
    size_t activeTaskCount(PoolType pool) const;
    void pause(PoolType pool);
    void resume(PoolType pool);
    bool drain(PoolType pool, std::chrono::milliseconds timeout);
    void shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5));
    void setBackPressureHandler(BackPressureFn fn);
};
```

## Input

| Parameter | Type | Constraints | Default |
| ----------- | ------ | ------------- | --------- |
| `prio` | `Priority` | — | — |
| `pool` | `PoolType` | — | — |
| `work` | `function<void(TaskContext&)>` | Non-null | — |
| `deps` | `vector<TaskId>` | Existing task IDs; cycle not checked | `{}` |
| `deadline` | `steady_clock::time_point` | Expired => skip work, still finalize | `max()` |
| `done` | `function<void()>` | Invoked on the WORKER thread | `{}` |
| `onProgress` | `function<void(int)>` | 0-100; invoked on the worker thread | `{}` |
| `n` | `int` | >0 | — |

## Output

| Method | Return | Semantics |
| -------- | -------- | ----------- |
| `submit` | `TaskHandle` | Handle for cancel/progress; null on rejection (paused/saturated) |
| `cancel(TaskHandle&)` | `void` | Sets cancel token; the task still finalizes and `done` still fires |
| `cancelTree(TaskId)` | `void` | BFS over dependents; done never fires for cancelled tasks |
| `handle(TaskId)` | `TaskHandle` | Null if not found or expired |
| `toPriority(PoolType)` | `Priority` | Static mapping |

## Ownership

- TaskScheduler **owns** the 5 QThreadPool instances, the prerequisite graph and
  the reverse-dependency graph.
- Caller receives `shared_ptr<TaskContext>` (shared ownership of context).
- TaskContext owns cancel token, progress counter (shared_ptr so they outlive task).
- Dependency graph stores weak refs via TaskHandle (no cycles at type level).

## Thread Safety

| Method | Thread | Mechanism |
| -------- | -------- | ----------- |
| `submit` | Any thread | Mutex-protected dep graph + pool queues; back-pressure handler invoked OUTSIDE the lock |
| `cancel/cancelTree` | Any thread | Atomic cancel + graph BFS under mutex |
| `handle` | Any thread | Graph lookup under mutex |
| `reportProgress` | Worker thread | Atomic store; `onProgress` runs on the worker |

## Callback thread contract (M26)

`done` and `onProgress` callbacks run on the **scheduler worker thread** that
executed the task — never on the submitting (UI) thread. UI consumers marshal
to the UI thread themselves (QMetaObject::invokeMethod). The internal
bookkeeping completion (metrics, handle removal, dependency release) happens on
the worker before the user `done` fires, so `drain()` never deadlocks with the
main event loop.

## Memory

| Operation | Dominant Allocation |
| ----------- | --------------------- |
| `submit` | 1 × TaskContext (~200 bytes + function captures) |
| `cancelTree` | O(k) BFS where k = dependents |
| `handle` | shared_ptr copy |

## Performance

| Scenario | Budget | Baseline |
| ---------- | -------- | ---------- |
| `submit` | <0.1 ms | queue only |
| `cancelTree(k deps)` | <0.05 ms | atomic ops only |
| Latency to start (no deps) | <5 ms | QThreadPool dispatch |

## Errors

| Error | Cause | Recovery |
| ------- | ------- | ---------- |
| null work | Invalid input | Return null handle |
| dependency not found | Unknown TaskId | Log warning; treat as no-dep (run immediately) |
| deadlock via circular deps | Bad usage | Not detected; use `cancelTree` to break |
| pool full / paused | Exceeds maxThreads / paused | Return nullptr; back-pressure handler notified; no state leaked |
| deadline expired pre-start | Deadline passed | Skip work, finalize, increment `deadline_exceeded` |

## Examples

```cpp
// Simple background task (signature: work, deps, deadline, done, onProgress)
auto h = TaskScheduler::instance().submit(
    TaskScheduler::Priority::Analysis,
    [](const TaskContext& ctx) {
        for (int i = 0; i <= 100; i += 10) {
            ctx.reportProgress(i);
            if (ctx.isCancelled()) return;
            std::this_thread::sleep_for(50ms);
        }
    },
    {},                                   // no deps
    std::chrono::steady_clock::time_point::max(), // no deadline
    []() { std::cout << "done\n"; }       // worker thread
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

// Deadline: expired tasks skip their work but still finalize.
auto bounded = TaskScheduler::instance().submit(
    TaskScheduler::Priority::Background,
    [](const TaskContext&) { /* work */ },
    {},
    std::chrono::steady_clock::now() + std::chrono::seconds(2));

// Cancel an entire subtree (root + transitive dependents)
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
