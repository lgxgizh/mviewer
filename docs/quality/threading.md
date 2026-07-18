# Threading Model

## Principles

1. **UI runs on main thread only** — never block
2. **Background work uses thread pools** — TaskScheduler
3. **Lock-free where possible** — atomics for shared state
4. **Mutexes when necessary** — short-held, documented

## Thread Pools

| Pool | Thread Count | Purpose |
| ------ | ------------- | --------- |
| UI | 1 (main) | Render, input, signals |
| Decode | N/2 | Full-image decode |
| Thumbnail | N/2 | Thumbnail generation |
| Analysis | N | Stats, diff, PSNR, SSIM |
| IO | 2 | File read, disk cache |
| Background | 1 | Prefetch, preload |

N = hardware concurrency (QThread::idealThreadCount)

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

- ImageCache: per-pool shared_mutex (read-heavy)
- DiskCache: serialized (SQLite)
- ImageFrame: immutable after construction (safe to share across threads)
- CacheManager: thread-safe API over layer below
