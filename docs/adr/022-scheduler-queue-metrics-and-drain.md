# ADR 022 — Scheduler queue metrics and drain wakeup

## Status

Accepted

## Context

`TaskScheduler` treated every task handed to a `QThreadPool` as already
running: `queue_depth` was incremented and decremented around `start()`, and
`active_tasks` counted tasks that were still sitting in the pool queue.
Backpressure (`queue_depth + active_tasks`) happened to see the right sum, but
each counter was wrong, and `cancelTree` could not tell a queued task from a
running one. `cancelTree`'s DFS also scanned the victim list with `std::find`
(O(V²)). `waitForPoolDrained` polled every 5 ms.

## Decision

- `queue_depth` is tasks handed to a pool that have not started. It drops when
  a worker begins the runnable, or when `cancelTree` cancels the task before
  start.
- `active_tasks` is tasks currently executing. It rises at worker start and
  drops on the terminal transition (`onTaskComplete` or `cancelTree` of a
  running task).
- Backpressure stays `queue_depth + active_tasks` against `max_queue_depth`.
  Dependency-waiting tasks stay in `waiting`, not in either counter.
- `PoolMetrics::total_latency_ns` sums queue wait (worker start minus enqueue),
  not execution time.
- `cancelTree` records visited ids in `std::unordered_set<TaskId>`. The victim
  order and which tasks are cancelled are unchanged.
- `waitForPoolDrained` waits on `m_drainCv` with `m_graphMtx` until
  `pending == 0` and `active_tasks == 0`, or the deadline. Completion and
  `cancelTree` notify when a pool reaches that state. The final
  `QThreadPool::waitForDone` still runs unlocked, with only the remaining
  timeout.

## Consequences

- Queue and active gauges match the pool. A full queue now shows up as
  `queue_depth` instead of a pile of fake active tasks.
- Drain wakes as soon as the pool is idle instead of on the next 5 ms tick.
  The timeout budget is still not spent twice.
- `total_latency_ns` is queue wait. It is not an execution-time average.
