# ADR-009: Why Split CompareEngine into Controllers

## Status
Accepted

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

## Rationale
- **Single Responsibility** — one reason to change per controller
- **Testability** — test controllers in isolation
- **Extensibility** — add new comparison modes without editing existing controllers
- **Parallelism** — controllers can run on different threads (diff vs. blink)

## Consequences
- ✅ Clean separation of comparison concerns
- ✅ Each controller tests independently
- ❌ More files (~6 vs 1)
- ❌ Wiring complexity slightly higher

## Related
- RFC-006 (Compare engine controllers)
- ADR-003 (CompareSession)
