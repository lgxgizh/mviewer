# ADR 018 — Thumbnail CPU budget and foreground work

## Context

The 2026-10 performance review measured up to ~4n runnable worker threads on
an n-core machine while scrolling a large folder: the `TaskScheduler`
thumbnail queue alone could run `n` threads, next to `n` decode threads and the
`n`-thread display-resample pool used for HQ zoom and compare panes. Thumbnail
decoding competed with the image the user is actually looking at.

## Decision

- The thumbnail queue is capped at `max(2, n / 2)` threads.
- Thumbnail, analysis and background queues run at `QThread::LowPriority`, so
  the OS scheduler prefers decode / UI / resample threads.
- `TaskScheduler::ForegroundWork` is a process-wide RAII marker. Decode-priority
  tasks and multi-band display resamples hold it while they run. A thumbnail
  task that starts while it is held waits in 4 ms steps, at most 200 ms, and
  stops waiting as soon as it is cancelled.

## Consequences

- The wait is bounded and cancellation-aware, so `drain()`, handle convergence
  and the M26/M27 lifecycle contracts are unchanged: every task still reaches
  exactly one terminal transition.
- Thumbnails may appear slightly later while a large image is decoding; the
  current image and compare HQ passes get the CPU first.
