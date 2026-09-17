# ADR-009: Why Split CompareEngine into Controllers

## Status

Accepted (Updated in Pass #10)

## Context

A monolithic CompareEngine accumulates responsibilities: sync, blink, diff, selection, viewport, layout. Hard to test, extend, or parallelize.

## Decision

CompareEngine splits into dedicated controller subsystems:

- **SyncController** — shared zoom/pan/scroll
- **BlinkController** — alternating highlight
- **DifferenceEngine** — pixel diff + heatmap
- **SelectionController** — ROI box sync
- **ViewportController** — per-cell transform state

Each controller owns one concern. CompareSession provides state; controllers consume it.

## Evolution (Pass #10 Hardening)

In Pass #10, the compare pipeline subsystems were hardened for performance and correctness:

- **DifferenceEngine & Histogram Vectorization**: Added AVX2/SSE2 vector accumulation to `DifferenceEngine::computeStats` and optimized inner loops for grayscale/RGB diffing, reducing diff metric calculation latency on large buffers. Clamped ROI coordinates using 64-bit bounds checking to eliminate integer overflow risks.
- **SyncController Lifecycle & Frame Swapping**: Added `swapCells(int a, int b)` to synchronize independent cell transforms whenever frames are reordered via `CompareEngine::swapFrames()`. Eliminated mutable shared static default cell state by isolating fallback references to thread-local storage, preventing cross-cell and cross-thread state pollution on out-of-bounds queries.
- **Aligner Channel Correctness & Vectorization**: Fixed multi-channel downsampling in `Aligner::downscaleBy()` by pre-converting RGB/BGR/RGBA/BGRA inputs to single-channel Grayscale8 prior to decimation when downscaling (`scale > 1`), preventing channel interleaving artifacts. Accelerated block matching with SSE2 `_mm_sad_epu8` (`PSADBW`) instructions and replaced per-pixel shifting with continuous 2D scanline memory copies.

## Rationale

- **Single Responsibility** — one reason to change per controller
- **Testability** — test controllers in isolation
- **Extensibility** — add new comparison modes without editing existing controllers
- **Parallelism** — controllers can run on different threads (diff vs. blink)

## Consequences

- ✅ Clean separation of comparison concerns
- ✅ Each controller tests independently
- ✅ SIMD acceleration keeps full-frame diffs and stats responsive under heavy load
- ✅ Auto-alignment and transform synchronization remain strictly correct across frame operations
- ❌ More files (~6 vs 1)
- ❌ Wiring complexity slightly higher

## Related

- RFC-006 (Compare engine controllers)
- ADR-003 (CompareSession)
- ADR-M22.3 (Compare Auto-Alignment before Diff Metrics)
