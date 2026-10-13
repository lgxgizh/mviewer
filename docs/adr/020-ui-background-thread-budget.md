# ADR 020 — UI and background queue thread budget

## Context

After ADR 018 the scheduler still allowed `n` UI-queue threads and `n / 2`
background threads next to `n` decode and `n` display-resample threads
(~36 runnable workers on 8 cores). UI-queue work is short file I/O and the
background queue runs metadata indexing and adjacent prefetch.

## Decision

- UI queue: `max(2, n / 2)` threads.
- Background queue: `max(1, n / 4)` threads.
- Decode, thumbnail, analysis and the resample pool are unchanged.

## Consequences

- On 8 cores the worker cap drops from 36 to 30; decode and HQ resample see
  less contention.
- Bulk metadata indexing for several folders at once progresses with fewer
  threads; each request is already a single sequential task.
