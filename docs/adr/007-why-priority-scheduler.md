# ADR-007: Why Priority Scheduler

## Status

Accepted

## Context

A single thread pool causes head-of-line blocking: a slow IO task can stall UI-critical decode/thumbnail work.

## Decision

TaskScheduler evolves into a 5-queue priority system. UI-facing tasks always preempt background work.

## Rationale

- **Responsiveness** — user actions never queue behind background preload
- **LIFO within queue** — latest user request served first
- **5 queues** align with distinct resource types (IO/Decode/Thumbnail/Analysis/Background)
- **Preemptive** — UI pool tasks can cancel background tasks

## Queue Priority

1. UI (highest) — navigation, zoom, user invoke
2. Decode — current-image decode
3. Thumbnail — visible thumbnails
4. Analysis — histogram, diff, PSNR
5. Background (lowest) — preload, prefetch

## Consequences

- ✅ Smooth 60fps during background scans
- ✅ UI never waits for background work
- ❌ Slightly higher single-image latency under extreme load
- ❌ Starvation possible for background queue

## Related

- RFC-004 (Scheduler priority)
