# Threading Model

## Principles

1. **UI runs on main thread only** — never block
2. **Background work uses thread pools** — TaskScheduler
3. **Lock-free where possible** — atomics for shared state
4. **Mutexes when necessary** — short-held, documented

## Thread Pools

| Pool | Thread Count | Purpose |
| ------ | ------------- | --------- |
| UI | 1 (main, not a pool) | Render, input, signals |
| Metadata (`Priority::Background`) | max(1, N/2) | Metadata index, prefetch, preload |
| Decode (`Priority::Decode`) | max(1, N) | Full-image decode |
| Thumbnail (`Priority::Thumbnail`) | max(2, N) | Thumbnail generation |
| Analysis (`Priority::Analysis`) | max(1, N/2) | Stats, diff, PSNR, SSIM |
| IO (`Priority::UI`) | max(1, N) | File read, disk cache; also serves UI-priority tasks |

N = hardware concurrency (QThread::idealThreadCount). The five pools are the
`PoolType` values (`MetadataPool` / `DecodePool` / `ThumbnailPool` /
`AnalysisPool` / `IOPool`); `Priority` maps onto them via `poolFromPriority`
(`src/core/scheduler/TaskScheduler.h`), and the per-pool thread counts are set in
`TaskScheduler.cpp` (`setQueueMaxThreads`).

## Task Priority

| Priority | Queue | Rules |
| ---------- | ------- | ------- |
| Highest | UI task | Preempt others, never wait |
| High | Decode | Current image |
| Medium | Thumbnail | Visible items |
| Low | Analysis | Computationally heavy |
| Lowest | Background | Best-effort, cancellable |

## Cancellation

- Every background task gets an atomic<bool> cancel flag
- Tasks check flag between operations
- Cancelled task skips callback
- Cancel propagates to dependents

## Future Safety

- Tasks return expected<T> not exceptions (no unwind across threads)
- Task result marshalled back to UI thread via QMetaObject::invokeMethod

## Cache Thread Safety

- ImageCache: one `std::mutex mtx` per pool (exclusive, short-held; `src/core/image/ImageCache.h`)
- DiskCache: serialized (SQLite)
- ImageFrame: immutable after construction (safe to share across threads)
- CacheManager: thread-safe API over layer below
