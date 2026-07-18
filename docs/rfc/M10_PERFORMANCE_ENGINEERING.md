# RFC: M10 — Performance Engineering (Benchmark Suite + MemoryTracker)

**Status:** RFC (draft for Tech Lead approval) — 2026-07-18.
**Depends on:** M3–M9 (frozen, verified on disk), ADR-001..011, `docs/performance.md`.
**Does NOT touch:** `build.ps1`, `CMakePresets.json`, `.github/workflows/ci.yml` (frozen).
**CI impact:** NONE gating. CI stays Phase-1 (Format → Build → CTest → Package). M10
adds a *local* benchmark runner only; the benchmark regression gate is roadmap Phase-4
and is explicitly NOT built here.

---

## 0. Context: why this milestone exists at all

`docs/roadmap.md` (M5 — Scale & Performance) **already lists** as a deliverable:

> Benchmark suite in CI with regression gates (`benchmark --baseline --threshold`).

That was the *intent* of M5. Reality (verified 2026-07-18 by file search across the
tree): **no benchmark executable, no `MemoryTracker`, no `benchmark/` directory, no
perf-assert anywhere.** M5 delivered the *pipeline scale* (1000-img non-blocking,
disk cache, hit-ratio) and deferred the *measurement* half. M9-§8 formally handed the
measurement half to M10:

> **M9 §8 — Out of scope / deferred to M10 (Performance Engineering)**
> - Benchmark suite (`benchmark/`: 1000 JPEG/PNG/TIFF; startup, thumbnail gen,
>   memory, cache-hit, scroll latency).
> - `MemoryTracker` (ImageFrame alloc, cache mem, thumbnail mem, peak).

So M10 is **not new scope** — it closes the open half of M5 that M9 punted. This RFC
keeps M10 strictly to that two-item list. It does NOT add GPU timing, Tracy/Perfetto
wiring, or a CI gate (all roadmap Phase-4/deferred).

---

## 1. Current substrate (verified by file read, 2026-07-18)

The measurement hooks already exist — M10 only exposes them through a repeatable harness:

- `src/core/image/ImageCache.h` — already exposes the exact counters M10 needs:
  - `size_t totalUsedBytes() const;` — all memory pools combined.
  - `size_t usedBytes(Level) const;` / `size_t entryCount(Level) const;` per level
    (`Metadata` / `Thumbnail` / `Preview` / `Viewer`).
  - `Level` enum is the 4-tier LRU (`Metadata`, `Thumbnail`, `Preview`, `Viewer`).
  - Hard caps already defined: Meta 16 MB, Thumb 64 MB, Preview 256 MB, Viewer 512 MB.
- `src/core/image/ImageRepository.h` — `load(path)`, `loadDirectory(...)`,
  `loadDirectoryAsync(...)`, `prefetchVisible(...)`. The async 1000-image path is the
  thing under test.
- `src/core/image/ImageFrame.h` — `pixels()`, `metadata()`, `decodeState()`,
  `setThumbnail/getThumbnail`. The unit of "one image's memory".
- `src/core/scheduler/TaskScheduler.h` — 3 pools: Decode / Thumbnail / Analysis.
  Submissions are the unit of "work done".
- `docs/performance.md` — the **authoritative budget**. M10 measures against these
  exact numbers (cold start <300 ms, first thumbnail <100 ms, 10k folder <2 s, image
  switch <16 ms preloaded / <100 ms cold 24 MP, thumbnail throughput >100/s, base
  memory <50 MB, per-24 MP-image ~96 MB uncompressed).

**No new product feature is built in M10.** M10 is measurement + a memory ledger.

---

## 2. Deliverable A — `MemoryTracker` (`core/perf/MemoryTracker.h` + `.cpp`)

A Qt-free, header-only-friendly singleton ledger. It does NOT allocate or hook the
allocator; it *samples* the existing counters at call sites the benchmarks control.
This is deliberate YAGNI — full allocator interposition (heaptrack-style) is Phase-4+.

### API
```cpp
namespace mviewer::perf {

struct MemorySnapshot {
    size_t imageCacheTotalBytes = 0;   // ImageCache::totalUsedBytes()
    size_t cacheByLevel[4] = {0,0,0,0}; // ImageCache::usedBytes(Level) x4
    size_t cacheEntryCount[4] = {0,0,0,0};
    size_t liveImageFrames = 0;          // tracked via ImageFrame ctor/dtor hook
    size_t peakBytes = 0;                // max imageCacheTotalBytes seen
    size_t processWorkingSetKB = 0;      // OS RSS (best-effort, platform guarded)
};

class MemoryTracker {
  public:
    static MemoryTracker &instance();

    // Sample now from ImageCache + OS; update peak; return the snapshot.
    MemorySnapshot sample();

    // Manual ledger for things the cache doesn't count (e.g. in-flight decode
    // buffers held outside ImageCache during a loadDirectory sweep).
    void addExternal(size_t bytes);
    void removeExternal(size_t bytes);

    void reset();                 // clears peak + external; does NOT touch ImageCache
    MemorySnapshot peak() const;  // last-seen max
};

} // namespace mviewer::perf
```

### Wiring (minimal, non-invasive)
- `MemoryTracker::sample()` calls `ImageCache::instance().totalUsedBytes()` etc.
  directly — zero new state in `ImageCache`.
- `liveImageFrames`: a `std::atomic<size_t>` incremented in `ImageFrame`'s
  constructor and decremented in its destructor (one-line each, in `ImageFrame.cpp`).
  This is the only change to an existing file, and it is additive (no API change).
- Process working-set read is `QProcess`/`GetProcessMemoryInfo` guarded by
  `#ifdef Q_OS_WIN` / `#ifdef Q_OS_LINUX`; if unavailable it reports 0 rather than
  failing the build. Best-effort only.
- RSS is reported but **never** used to fail a budget check (OS RSS is noisy);
  budget checks use `ImageCache` bytes + `liveImageFrames × frameBytes`, which are
  deterministic.

### Tests (`test_memorytracker.cpp` → `memorytracker_tests`)
- `sample()` returns `imageCacheTotalBytes == ImageCache::totalUsedBytes()`.
- After `put` N frames of known size into `ImageCache::Viewer`, `peak().imageCacheTotalBytes`
  ≥ N × frameBytes and `liveImageFrames` tracks ctor/dtor exactly (construct K frames,
  destroy half, assert count == K/2).
- `addExternal`/`removeExternal` adjust `sample()`'s external component and roll into peak.
- `reset()` clears peak; subsequent `sample()` re-establishes it.

---

## 3. Deliverable B — Benchmark suite (`benchmark/`)

A **standalone console executable** `mviewer_bench` (NOT a CTest unit test — it is a
timing harness; CTest would impose its own timeout/parallel semantics that fight
benchmarks). Registered in `src/CMakeLists.txt` as `add_executable(mviewer_bench ...)`
+ `add_test(NAME bench_smoke COMMAND mviewer_bench --smoke)` so it at least *builds*
and a 1-scenario smoke runs in CI (no budget assertions in CI — CI just proves it
links and runs; the full suite is for local/perf runs).

### Corpus
- Generated at bench-runtime into a temp dir (no committed binaries): 1000 JPEG +
  1000 PNG + 200 TIFF synthesized via `QImage` fill + `QImageWriter` (same path
  `test_decoder` golden images use). Sizes: JPEG/PNG 24 MP (6000×4000) to match the
  `performance.md` "24 MP JPEG" budget; TIFF 4 MP (lighter, since TIFF codec is
  gated). A `--corpus-size N` flag scales it down for the `--smoke` (N=20) CI run.
- Golden 4-format images already exist in the repo; the bench reuses the decoder's
  known-good paths.

### Scenarios (each prints p50/p95/p99 + pass/fail vs `performance.md`)
| # | Scenario | Measures | Budget (from performance.md) |
|---|----------|----------|------------------------------|
| B1 | Startup-to-first-paint | `main()` entry → first `QWidget` paint (offscreen) | Cold <300 ms / Warm <100 ms |
| B2 | Folder load, first thumbnail | `loadDirectoryAsync` return → first `ThumbnailPipeline` emit | First thumb <100 ms; 10k folder <2 s to show visible thumbs |
| B3 | Decode latency | decode 100× each format, p50/p95/p99 | p50 <50 ms, p95 <150 ms (24 MP JPEG) |
| B4 | Thumbnail throughput | generate 1000 thumbnails | >100 / second |
| B5 | Cache-hit ratio | simulate 1000 nav ops, Zipf | L2 >90%, L3 >95% |
| B6 | Memory budget | `loadDirectory` of 1000 24 MP, sample `MemoryTracker` | base <50 MB; per-image ~96 MB; returns to baseline after eviction |
| B7 | Image-switch latency | navigate fwd 100× preloaded vs cold | preloaded <16 ms; cold <100 ms (24 MP) |

Each scenario prints a one-line verdict `[PASS]/[FAIL] Bn <metric> <value> budget=<x>`
and the binary exits non-zero **only in `--enforce` mode** (local perf gate, opt-in).
Default mode (CI `--smoke`) prints numbers and always exits 0 — CI does not enforce
budgets (roadmap Phase-4 defers the regression gate).

### Tests (`test_benchmark.cpp` → `benchmark_tests`)
- The scenario *functions* are unit-testable: given a 20-image corpus, `B3` returns
  finite p50; `B5` with a rigged Zipf navigator returns a ratio in [0,1]; `B6`
  asserts `MemoryTracker` peak ≥ N×frameBytes and that after `ImageCache::clear()` the
  next `sample()` drops to baseline. These run in CTest (no timing flakiness — they
  assert structural correctness, not wall-clock).

---

## 4. What M10 explicitly does NOT do (YAGNI / deferred)

- ❌ **CI regression gate** — roadmap Phase-4. CI only builds + smoke-runs `mviewer_bench`.
- ❌ **Tracy / Perfetto integration** — deferred; M10 uses plain `std::chrono` + OS RSS.
- ❌ **GPU/texture timing** — `performance.md` lists it; M10 has no GPU hook yet.
- ❌ **SQLite / 5-level-cache rework** — frozen; M10 measures the existing 4-tier `ImageCache`.
- ❌ **Predictive-preload algorithm changes** — M10 *measures* preload effectiveness (B5/B7),
  does not alter the preload logic.
- ❌ **Allocator interposition / heaptrack** — `MemoryTracker` samples existing counters.
- ❌ **Any QWidget / UI change** — benchmarks drive `core/` directly (headless offscreen).

---

## 5. File plan
```
src/core/perf/MemoryTracker.h      (new, Qt-free header)
src/core/perf/MemoryTracker.cpp    (new; samples ImageCache + OS RSS)
src/core/image/ImageFrame.cpp      (add atomic liveImageFrames ++/-- ; additive)
benchmark/main.cpp                 (new; argparse: --smoke/--enforce/--corpus-size)
benchmark/scenarios_b1_b7.cpp/.h  (new; scenario functors, unit-testable)
benchmark/corpus.cpp/.h           (new; temp-dir 1000-JPEG/PNG/TIFF generator)
test_memorytracker.cpp             (new → memorytracker_tests)
test_benchmark.cpp                (new → benchmark_tests)
src/CMakeLists.txt                (+ mviewer_bench exe, + 2 add_test entries)
```

---

## 6. Acceptance criteria (M10)

- [ ] `MemoryTracker` samples `ImageCache` totals/per-level + tracks live `ImageFrame`
      count; `peak()` monotonic; `reset()` clears it. `memorytracker_tests` green.
- [ ] `mviewer_bench` builds and `--smoke` (20-image corpus) runs and exits 0 in CI
      (proves it links on the gating pipeline).
- [ ] All 7 scenarios (B1–B7) run on a 1000-image corpus locally and print
      p50/p95/p99 + budget verdicts readable by a human.
- [ ] `benchmark_tests` green (structural correctness of each scenario functor).
- [ ] Memory budget B6 verified locally: peak ≥ N×~96 MB during a 1000×24 MP sweep and
      returns to within 10% of baseline after `ImageCache::clear()` + eviction.
- [ ] `MemoryTracker` adds **no** decode logic to any QWidget; `core/perf/` is Qt-free
      at the header level (RSS read is `.cpp`-guarded).
- [ ] No change to `build.ps1` / `CMakePresets.json` / `ci.yml`. CI stays Phase-1 green.
- [ ] `CHANGELOG.md` + `STATUS.md` + roadmap M10 row updated (Definition of Done).

---

## 7. Verification plan (local, per AGENTS.md)

1. `powershell -ExecutionPolicy Bypass -File D:/mviewer/build.ps1 Test` → 100% (the 2
   new CTest suites included).
2. `mviewer_bench --smoke` runs and exits 0 (also proven by CI `bench_smoke`).
3. `mviewer_bench` (full 1000-corpus) prints B1–B7 verdicts; spot-check B6 memory
   returns to baseline. Run 2× to confirm numbers are stable (not a single lucky pass).
4. `git diff --stat` confirms only `core/perf/*`, `benchmark/*`, `ImageFrame.cpp`
   (+2 lines), `CMakeLists.txt`, and docs changed — no frozen-infra files touched.

---

## 8. Review checklist (for Tech Lead approval)

- [ ] Scope strictly = M9-§8 two items (benchmark + MemoryTracker)? No scope creep?
- [ ] CI stays Phase-1 (no benchmark gate added)? `bench_smoke` only proves it links?
- [ ] `MemoryTracker` samples existing counters (no allocator interposition)?
- [ ] `ImageFrame.cpp` change additive only (atomic ctor/dtor counter)?
- [ ] Budgets in §3 table taken verbatim from `docs/performance.md`?
- [ ] No QWidget / UI code in the benchmark or perf module?
