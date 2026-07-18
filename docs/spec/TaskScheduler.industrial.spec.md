# TaskScheduler Industrialization Specification

## Module

TaskScheduler Industrialization

## Purpose

Upgrade the existing `TaskScheduler` from a basic priority-queue scheduler into
an industrial-grade multi-executor engine suitable for:

- Priority-based preemptive scheduling
- Task dependency graphs (DAG)
- Cancel-token-based cooperative cancellation
- Per-executor thread affinity and deadlines
- Performance monitoring and back-pressure

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  TaskScheduler (Singleton)           │
│                                                      │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐      │
│  │ IO        │  │ Decode    │  │ Thumbnail │      │
│  │ Executor  │  │ Executor  │  │ Executor  │      │
│  │ (2 cores) │  │ (N/2)     │  │ (N/2)     │      │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘      │
│        │              │              │             │
│  ┌─────┴─────┐  ┌─────┴─────┐  ┌─────┴─────┐      │
│  │ Analysis  │  │ Render    │  │ Background│      │
│  │ Executor  │  │ Executor  │  │ Executor  │      │
│  │ (N)       │  │ (1)       │  │ (1)       │      │
│  └───────────┘  └───────────┘  └───────────┘      │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │           DependencyTracker                   │   │
│  │  (DAG resolution, cycle detection)           │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │           DeadlineScheduler                   │   │
│  │  (per-task deadline, timeout, SLA)            │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │           MetricsCollector                    │   │
│  │  (p50/p95/p99, queue depth, throughput)      │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

## Executor Registry

| Executor | Queue | Max Threads | Affinity | Purpose |
| ---------- | ------- | ------------- | ---------- | --------- |
| IO | IOPool | 2 | Low-latency | File reads, disk cache writes |
| Decode | DecodePool | N/2 | CPU-bound | JPEG/PNG/BMP decode |
| Thumbnail | ThumbnailPool | N/2 | CPU-bound | Thumbnail scaling |
| Analysis | AnalysisPool | N | CPU-bound | PSNR/SSIM/histogram/diff |
| Render | RenderPool | 1 | UI-adjacent | Pre-render QImage scalings |
| Background | BackgroundPool | 1 | Best-effort | Prefetch, cache eviction |

## Task Lifecycle

```
Created → Scheduled → WaitingDeps → Queued → Running → Completed
                                       → Cancelled
                                       → DeadlineExceeded
                                       → Failed
```

### Dependency DAG

- Directed Acyclic Graph (DAG) — cycle detection at submission time
- A task enters `WaitingDeps` state until all deps transition to `Terminal`
- Mixed deps: `AnyOf` (default), `AllOf` (barrier), `None`
- Cycle detection: topological-sort check on submission; if cycle, reject

### Deadline Semantics

- Per-task deadline (optional): if work doesn't start before `now + deadline_ms`,
  task is marked `DeadlineExceeded` and callback with error is invoked.
- If work exceeds `max_runtime_ms`, task is cancelled with cancel token.
- Deadline != runtime timeout: deadline is "start before", timeout is "run no longer than"

### CancelToken

- `shared_ptr<atomic<bool>>` per task
- Extended tokens can be chained: parent cancel → child cancel cascade
- `CancellationToken scope_guard` for RAII patterns

## Priority Model

```
Highest = 100 = User-visible image decode
         75 = Active viewport decode
         50 = Thumbnail generation (visible items)
         25 = Analysis computation
         10 = Prefetch / cache warming
Lowest  = 0  = Background housekeeping
```

Priority classes:

- **Realtime**: must preempt everything else (UI deadline tasks)
- **High**: user-visible, blocking viewport
- **Medium**: quality-of-service, partial previews
- **Low**: best-effort, deferrable under pressure
- **Idle**: only run when all other queues are empty

## Back-Pressure

When the queue depth exceeds `max_queue_depth` (default 1000 per executor):

- New tasks are rejected with `SchedulerError::BackPressure`
- Caller should retry with exponential backoff or fall back to sync
- Critical (deadline) tasks bypass back-pressure

## Metrics (per executor)

- queue_depth
- active_tasks
- completed_per_second
- p50 / p95 / p99 latency (ms)
- cancelled_count
- backpressure_rejection_count

## API (extends current TaskScheduler)

```cpp
class TaskScheduler {
public:
    // ... existing API ...

    // Deadline-aware submission
    TaskHandle submit(Priority prio,
                      std::function<void(const TaskContext&)> work,
                      std::vector<TaskId> deps = {},
                      std::chrono::steady_clock::time_point deadline = {},
                      std::function<void()> done = {},
                      std::function<void(int)> onProgress = {});

    // Per-executor metrics
    struct ExecutorMetrics {
        size_t queue_depth;
        size_t active;
        double p50_ms, p95_ms, p99_ms;
        uint64_t completed, cancelled, backpressure_rejected;
    };
    ExecutorMetrics metrics(PoolType pool) const;
    std::vector<std::pair<PoolType, ExecutorMetrics>> allMetrics() const;

    // Queue depth
    size_t queueDepth(PoolType pool) const;

    // Saturated?
    bool isSaturated(PoolType pool) const;

    // Global pause/resume (draining mode for shutdown)
    void pause(PoolType pool);
    void resume(PoolType pool);

    // Wait for all pending tasks in a pool to complete
    void drain(PoolType pool, std::chrono::milliseconds timeout);
};
```

## Phase 2 Implementation Order

1. **Phase 2a**: Deadline tracking + runtime timeout → minimal change
2. **Phase 2b**: Dependency DAG with waiting-queue separation → patch m_deferred
3. **Phase 2c**: Executor affinity + metrics → expose queues individually
4. **Phase 2d**: Back-pressure + saturation detection → add depth tracking
5. **Phase 2e**: Drain + pause/resume → for shutdown correctness

## Verification Criteria

- All existing tests pass unchanged (backward compatible)
- `DependencyGatesStart` test validates DAG semantics
- Back-pressure counter increments when queue_depth > max
- Deadline-exceeded tasks receive error callback

## Future Extension

- Work stealing (idle pool picks from saturated pool)
- Task serialization (save/submit across process restart)
- Dynamic thread-pool resize based on metrics
- Machine-learning-based priority prediction
