# MViewer Performance Specification

## Performance Philosophy

Performance is the primary design goal of MViewer. Every architectural decision, data structure choice, and algorithm selection must be evaluated against its impact on the user-perceived responsiveness of the application.

**The user should never wait for the viewer. The viewer should always be waiting for the user.**

---

## Performance Targets

### Startup

| Metric | Cold Start | Warm Start |
| -------- | ----------- | ------------ |
| Target | < 300 ms | < 100 ms |
| Measurement | Process main() to first paint | Process main() to first paint |

**Cold start:** First launch after system boot (no filesystem cache).
**Warm start:** Subsequent launch (filesystem cache warm).

### Folder Loading

| Metric | Target |
| -------- | -------- |
| Time to first thumbnail | < 100 ms |
| UI responsiveness | No frame drops during scan |
| 10,000 image folder | < 2s to display all visible thumbnails |

**Requirement:** Directory scanning must be asynchronous. The UI must remain interactive at 60fps during folder loading.

### Image Switching

| Metric | Target |
| -------- | -------- |
| Perceived latency | < 16 ms (one frame at 60fps) |
| Decode + display (preloaded) | < 16 ms |
| Decode + display (cold) | < 100 ms for 24MP JPEG |

**Requirement:** Neighboring images should be preloaded. Switching to a preloaded image must feel instantaneous.

### Zoom & Pan

| Metric | Target |
| -------- | -------- |
| Frame rate | 60 fps sustained |
| Frame time | < 16.6 ms |
| Large images (50MP+) | No degradation in zoom/pan smoothness |

**Requirement:** GPU-accelerated rendering. CPU should not be involved in zoom/pan operations after texture upload.

### Thumbnail Generation

| Metric | Target |
| -------- | -------- |
| First thumbnail decode | < 50 ms |
| Thumbnail resize | < 10 ms |
| Throughput | > 100 thumbnails/second (background) |

### Memory Usage

| Metric | Target |
| -------- | -------- |
| Base memory (empty window) | < 50 MB |
| Per-image cache (24MP RGBA) | ~96 MB (uncompressed) |
| GPU texture budget | Configurable, default 512 MB |
| Thumbnail cache (disk) | Configurable, default 1 GB |

---

## Profiling Methodology

### Tools

| Platform | Tool | Use Case |
| ---------- | ------ | ---------- |
| Windows | Tracy Profiler | CPU/GPU timeline, memory tracking |
| Windows | Intel VTune | Micro-architectural analysis |
| Windows | GPUView | GPU scheduling analysis |
| Linux | perf | CPU profiling |
| Linux | Tracy Profiler | CPU/GPU timeline |
| Linux | heaptrack | Memory profiling |
| Linux | Valgrind (Massif) | Heap profiling |

### Profiling Workflow

1. **Identify** — Use benchmarks to detect regressions
2. **Measure** — Profile with Tracy to find hot spots
3. **Analyze** — Determine root cause (algorithm, data structure, cache miss, allocation)
4. **Optimize** — Apply targeted fix
5. **Verify** — Confirm improvement with benchmarks
6. **Prevent** — Add regression test to CI

### What to Measure

- **Wall-clock time** — User-perceived latency
- **CPU time** — Total processor usage
- **Frame time** — Time between rendered frames
- **Cache misses** — L1/L2/L3, TLB
- **Memory allocations** — Count and size
- **GPU time** — Draw call duration, texture upload time
- **I/O wait** — File read latency

---

## Benchmark Suite

### Startup Benchmark

```
Measure: Time from process entry to first frame rendered
Method: Run 10 times, discard first 3 (warmup), average remaining
Target: Cold < 300ms, Warm < 100ms
```

### Folder Loading Benchmark

```
Measure: Time to display first thumbnail after folder open
Method: Use test corpus of 10,000 images (mixed formats)
Target: < 100ms to first thumbnail
```

### Decode Latency Benchmark

```
Measure: Time from decode request to raw pixels available
Method: Decode 100 images of each format, measure p50/p95/p99
Target: p50 < 50ms, p95 < 150ms (24MP JPEG)
```

### Thumbnail Generation Benchmark

```
Measure: Time from file path to thumbnail bitmap ready
Method: Generate 1000 thumbnails from test corpus
Target: > 100/second throughput
```

### Cache Hit Ratio Benchmark

```
Measure: Cache hits / total requests during simulated browsing
Method: Simulate 1000 navigation operations with Zipf distribution
Target: > 90% hit ratio for L2 cache
```

### Memory Usage Benchmark

```
Measure: Peak and steady-state memory consumption
Method: Open 100 images sequentially, record RSS and VRAM
Target: Memory returns to baseline after cache eviction
```

### Image Switching Benchmark

```
Measure: Time from navigation request to new image displayed
Method: Navigate forward 100 times, measure frame-to-frame time
Target: < 16ms for preloaded images
```

---

## Optimization Hierarchy

Apply optimizations in this order. Never skip levels.

### 1. Algorithm & Data Structure

- Choose O(1) or O(log n) over O(n) for hot paths
- Use hash maps for lookups, not linear search
- Prefer contiguous memory (vectors) over linked structures
- Use spatial data structures for image operations

### 2. Parallelism

- Decode on thread pool (one thread per core minus UI)
- Parallelize independent operations (decode + metadata + thumbnail)
- Use SIMD for pixel operations (color conversion, resize)
- Lock-free data structures for inter-thread communication

### 3. Memory

- Minimize allocations (object pools, arena allocators)
- Pre-allocate buffers where size is known
- Use `std::move` to avoid copies
- Release memory promptly (don't hold decoded images unnecessarily)

### 4. GPU

- Upload textures once, reuse for multiple draws
- Use GPU for resize/scaling (hardware bilinear/trilinear)
- Batch draw calls where possible
- Minimize CPU-GPU data transfer

### 5. I/O

- Use memory-mapped files for large reads
- Read ahead (sequential access pattern)
- Compress on-disk cache to reduce I/O
- Async file operations (never block UI thread)

---

## What NOT to Optimize

- **Cold paths** — One-time operations (settings load, plugin discovery)
- **Code clarity** — Readability matters more than micro-optimizations
- **Unmeasured code** — Profile first, optimize second
- **Speculative optimization** — Don't optimize for hypothetical bottlenecks
- **Below threshold** — If it's already under target, move on

---

## Performance Regression Policy

### CI Gates

- Benchmarks run on every PR
- Results compared against baseline (main branch)
- **Fail:** > 5% regression on any primary metric
- **Warn:** > 2% regression on any secondary metric

### Regression Response

1. Block merge if CI gate fails
2. Author investigates root cause
3. Fix or revert before merge
4. Update baseline if improvement is intentional

### Baseline Updates

- Baselines updated only on merge to `main`
- Requires explicit approval from maintainer
- Documented in commit message

---

## Cache Performance Targets

| Cache Layer | Hit Ratio Target | Eviction Policy |
| ------------- | ----------------- | ----------------- |
| L1 GPU Texture | > 85% | LRU, VRAM-bounded |
| L2 Decoded Image | > 90% | LRU, RAM-bounded |
| L3 Thumbnail (memory) | > 95% | LRU, count-bounded |
| L4 Thumbnail (disk) | > 99% | Persistent, size-bounded |

---

## Preloading Strategy

| Navigation Direction | Preload Count | Priority |
| --------------------- | --------------- | ---------- |
| Forward | 3 images | High |
| Backward | 1 image | Medium |
| Thumbnail scroll | Visible + 20 buffer | High |

**Rationale:** Users more often navigate forward. Forward preloading hides decode latency.

---

## Memory Budget Defaults

| Resource | Default Limit | Configurable |
| ---------- | -------------- | -------------- |
| L2 Decoded Image Cache | 256 MB | Yes |
| L1 GPU Texture Cache | 512 MB | Yes |
| L3 Thumbnail Memory Cache | 64 MB | Yes |
| L4 Thumbnail Disk Cache | 1 GB | Yes |
| Thread Pool Size | N-1 cores | Yes |

**Note:** All limits are soft. The system may exceed limits transiently during decode operations but must converge to limits within a bounded time.
