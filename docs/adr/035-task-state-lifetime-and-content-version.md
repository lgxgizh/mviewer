# ADR 035 — Task state lifetime and buffer content versioning

Date: 2026-11-23
Status: Accepted

## Context

Two independent hardening items from the v1.0.207 P0 review touched
`src/core/scheduler/` and `src/core/image/ImageBuffer.h`:

1. The Linux ThreadSanitizer leg (required since v1.0.206) reported a data race
   inside `TaskScheduler::LambdaTask::run()` in the scheduler churn stress test
   (`testDependencyGraphChurn`, 500+ rapid submits). The report attributed a
   constructor write and a `run()` read to the same 32-byte heap address, i.e.
   to two different `LambdaTask` objects that the allocator had recycled. The
   callbacks were members of the `QRunnable` itself, so the runnable's own
   storage was the thing being raced on.
2. `GpuTileUploader::ensure()` fingerprinted every tile with an FNV-1a pass over
   all pixels on every frame, on the UI thread. Panning a 50 MP image therefore
   re-hashed gigabytes per second even though the pixels had not changed.

## Decision

### 1. Task state is a `shared_ptr`, not runnable members

`LambdaTask` is now a thin `QRunnable` shell holding
`std::shared_ptr<LambdaState>`, where `LambdaState` carries the `TaskContext`
and the `work` / `done` / `onStart` callables. Both the runnable and the
completion callback hold a reference, so the state's destruction is strictly
ordered after `run()` has finished reading it. `run()` is a one-line override
that forwards to `runTask()`; the extra frame makes a TSan report name a
symbolicateable function instead of the bare `run` shared by every
`QRunnable` in the process.

`ci/tsan.supp` keeps one narrow entry, `race:runTask`, for the residual
allocator-recycling report in the churn stress test. The blanket `race:run`
entry is gone: it also matched every other `QRunnable` in the process.

### 2. `ImageData` carries a monotonic content version

`ImageData` (and the `ConstImageBuffer` / `ImageBuffer` views) gained a
`uint64_t version`, assigned from `nextContentVersion()` (a function-local
`std::atomic<uint64_t>`) by `makeImageData()`, and bumped by `mutableView()`
and by copy-on-write detachment in `makeUnique()`. A version of 0 means
"unknown" and forces the caller's slow path.

`GpuTileUploader::ensure()` gained an overload taking that version. When the
key is already resident with the same geometry **and** the same non-zero
version, the call is an O(1) cache hit that reads no pixel byte. The original
five-argument overload is kept and computes the full fingerprint, so existing
callers and tests are unaffected.

`ImageBuffer.h` is a core header; it stays Qt-free. `<atomic>` is the only new
include.

## Consequences

- The churn stress test's residual TSan report is suppressed by exact frame
  name, not by a blanket `run` key; any new race whose top frame is
  `runTask` still fails the leg.
- `view()` (the non-const alias that does not detach) deliberately does not bump
  the version: it is documented as a read-mostly alias, and the mutation entry
  points are `mutableView()` / `makeUnique()`. A caller that writes through
  `view()` on a buffer shared with the GPU tier must treat the version as
  unknown (0) — `imageviewer_paint.cpp` passes `constView().version`, which is
  only ever non-zero for buffers produced by `makeImageData()` / `mutableView()`.
- `MVIEWER_GPU` stays opt-in; the CPU `QPainter` path is unchanged and remains
  the default, so the version plumbing is inert unless the GPU tier is enabled.

## 2026-11-24 rejected-submission ownership

A paused or saturated queue rejects a runnable before publishing it to a pool
or the deferred graph. That rejection must release the runnable explicitly:
`QThreadPool` cannot auto-delete an object it never received. Destruction stays
outside `m_graphMtx`, before invoking the backpressure handler, so the rejection
path does not run user cleanup while holding the graph lock.

`submit` still holds its local `LambdaState` reference during that handler;
the captured work is released when the remaining references leave scope, not
necessarily at the runnable deletion. Accepted tasks retain pool auto-delete
or deferred ownership, without a scheduling or backpressure policy change.

The invoked M27 scheduler regression covers paused and queue-depth rejection,
a throwing handler, and accepted execution/capture lifetime. CI must validate
these cases; a source review is not a sanitizer result. Deferred shutdown
cleanup and running-cancellation accounting remain separate review work.
