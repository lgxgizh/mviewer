# M26 Async Reliability Baseline (2026-08-09)

> Phase 0 of M26 — RC Reliability Closure. Reality audit + failing regression
> tests ONLY; no product implementation was changed. Every finding below was
> reproduced against the M25 tree (commit `dba045b` + the new regression tests
> in this phase) with `.\build.ps1 Release` and the new test executables.
>
> All five new regression targets FAIL exactly as documented here; the pre-M26
> gate (`.\build.ps1 Test`, 73 tests) still passes because none of these paths
> were covered.

## 1. Method

- Re-read `docs/spec/*`, `docs/contracts/module_contracts.md` against the real
  implementation of `TaskScheduler`, `MetadataIndexer`, `ThumbnailPipeline`,
  `ImageRepository`, `PreviewPanel` and their consumers.
- Wrote failing tests FIRST (`src/core/test_m26_scheduler.cpp`,
  `src/core/test_m26_metadata.cpp`, `src/core/test_m26_thumbnail.cpp`,
  `src/core/test_m26_repository.cpp`, workflow6 in `src/test_workflow_ux.cpp`),
  wired into `src/CMakeLists.txt` (new ctest names `m26_*_tests`).
- Ran each binary; the failures below are the reproducible, deterministic
  baseline. No production code was modified during this phase.

## 2. Reproducible bugs (each with a failing regression)

### 2.1 TaskScheduler — 15 failures in `test_m26_scheduler`

| # | Bug | Evidence (failing check) | Root cause |
|---|-----|--------------------------|------------|
| 1 | Deadline-expired task never finalizes | `expired task still finalizes`; `pending returns to 0`; `active_tasks returns to 0`; `deadline_exceeded metric incremented`; `expired task handle is removed` | `LambdaTask::run()` returns at the deadline check **before** invoking the finalize wrapper — `onTaskComplete` never runs, `pending`/`active_tasks` stay 1 forever, the handle stays in `m_handles`, and the `deadline_exceeded` PoolMetric is never incremented anywhere |
| 2 | Deferred-task `pending` counter never incremented → **size_t underflow** | `pending == 0 after chain`; `pending == 0 (no underflow from submit-time release)` | submit-with-deps stores the deferred entry but never does `pending++`; on completion `onTaskComplete` does `pending--` on a zero counter → wraps to `SIZE_MAX` |
| 3 | Deferred task released at submit time skips all metrics | `active_tasks == 0 (no underflow from submit-time release)` | the submit-time release loop starts the runnable without `active_tasks++` (the `onTaskComplete`-time release path does increment — inconsistent) |
| 4 | `cancelTree(root)` walks **prerequisites**, not dependents | `dependent B cancelled by cancelTree(A)`; `transitive dependent C cancelled by cancelTree(A)`; `cancelled metric counts the whole subtree`; `no residual handles` | `m_depGraph[id]` stores `dependencies`; `cancelTree` BFS-follows them, so `cancelTree(A)` cancels only A (B/C stay live), and `cancelTree(B)` wrongly cancels B's prerequisite A |
| 5 | `cancelTree` on a deferred/waiting task underflows `active_tasks` and `queue_depth` | `3c`: `active_tasks == 0`; `queue_depth == 0` | deferred tasks never incremented those counters; `cancelTree` decrements them anyway |
| 6 | **Metric poisoning rejects every later submit** | `submit accepted after previous cancelTree (metrics not poisoned)` | consequence of #5: after one `cancelTree` of a deferred task, `queue_depth + active_tasks` wraps to ≥ `max_queue_depth`, so **every subsequent submission to that pool returns nullptr**. In the test this manifested as a deterministic access violation (`a->id` on a null handle); in the product it silently kills every later submit on that pool |
| 7 | Cancelled subtree work can still run via stale deferred entries | 3b `B work never runs` | the cancelled subtree's deferred runnables are never deleted and not released; a later `releaseReadyTasks` in any `submit` releases them |

**Bonus crash (use-after-free) reproduced during baseline debugging:** with
tests 3 → 3b back-to-back (no per-test cleanup), the stale deferred runnables
from test 3 (their lambdas capture freed stack of the finished test) were
released by test 3b's first `submit`, and the worker ran a freed `LambdaTask`
→ `0xC0000005`. Root-caused via file-based trace: `cancelTree(A)` never
touches B/C, their runnables stay in `m_deferred`, and the next submit's
`releaseReadyTasks` starts them. This is a real product hazard (a consumer
that cancels a subtree while holding captured state, then any later submit
anywhere on the process, can run freed captures).

### 2.2 MetadataIndexer — 4 failures in `test_m26_metadata`

| # | Bug | Evidence | Root cause |
|---|-----|----------|------------|
| 8 | Consumer B silently cancels consumer A | `consumer A completion NOT silently dropped by B's request`; `shared cache holds every indexed path` | `index()` bumps the **single global** generation and calls `TaskScheduler::cancel(m_handle)` unconditionally — a MainWindow search re-index cancels the ThumbnailPanel filter index mid-flight and vice versa |
| 9 | Consumer A silently cancels consumer B (reverse order) | `consumer B completion NOT silently dropped by A's request`; `consumer B receives exactly its 1200 entries` | same root cause; B's `onDone`/entries are dropped when A starts |
| 10 | (UI-level, workflow6) Panel `m_metaIndexing` stuck → camera filter never applies | `camera filter 'sony' applies while MainWindow re-index runs concurrently` in `workflow_ux_tests` with 4000-DNG fixture | real MainWindow: `reindexSearch` (500ms after folder load) supersedes the panel's in-flight `ensureMetaIndex` generation; its `onDone` is dropped, `m_metaIndexing` stays true, `applyFilter` early-returns forever |

Design-level (not yet runtime-testable pre-fix, will get tests with the fix):

- `cached()` returns `const Entry*` into `m_cache` after releasing the lock;
  a concurrent index pass (worker thread writes `m_cache`) can rehash the map
  and invalidate the returned pointer before the caller reads it.
- `m_cache`/`m_identity` are unbounded — a long session keeps every indexed
  path forever (no budget).

### 2.3 ThumbnailPipeline — 14 failures in `test_m26_thumbnail`

| # | Bug | Evidence | Root cause |
|---|-----|----------|------------|
| 11 | `setSources(B)` does not cancel obsolete A work | `obsolete generation-A queued work stopped before decoding (1 in flight only)` | `setSources` only bumps the generation; the 39 queued A tasks still decode fully, then drop the result — wasted decode work, exactly the "decode-then-drop" anti-pattern |
| 12 | Same path in both generations is permanently pending-blocked | `path visible in both generations delivered for the CURRENT generation` | A's task keeps the `(path,size)` key in `m_pending`; B's `scheduleLocked` sees it and skips; when A's task finishes it drops (gen mismatch) and nothing re-schedules for B |
| 13 | Rejected submit poisons the key forever | `all 10 keys decoded after resume + retry`; `no keys stuck pending after resume` | `enqueueLocked` inserts into `m_pending` **before** `submit`; on `nullptr` (paused pool) the key is never erased → the path can never be scheduled again |
| 14 | Completed handles accumulate forever | `handles do not accumulate the whole browse history` (10×) | `m_handles[k]` is written in `enqueueLocked` and never erased on completion/cancel — the map grows with every path ever decoded |

### 2.4 ImageRepository — 7 failures in `test_m26_repository`

| # | Bug | Evidence | Root cause |
|---|-----|----------|------------|
| 15 | `loadDirectoryAsync` aggregate callback never fires under saturation | `aggregate callback fires under pool saturation`; `EXACTLY once`; `one Result per file`; `rejected submissions become explicit failure Results` | submissions beyond `max_queue_depth` return `nullptr` and are dropped silently; `completed` never reaches `n` so the last-task callback never runs |
| 16 | Sync `loadDirectory` busy-waits forever under pause/saturation | `sync loadDirectory returns while the pool is paused (bounded)`; `bounded sync load still produces a Result per file` | `while (completed < n) sleep(100µs)` has no bound; rejected/paused tasks never increment `completed`. (Repro harness hangs pre-fix; the test bounds it and detaches the zombie.) |
| 17 | `loadDirectory` clobbers a caller-configured queue depth | `caller-configured queue depth preserved after loadDirectory` | `setMaxQueueDepth(DecodePool, 0)` then restores hard-coded `1000` — user-configured values are lost; also not exception-safe |

### 2.5 Callback thread contract (doc/impl drift, no runtime test yet)

- Spec (`docs/spec/TaskScheduler.spec.md`): `done` "Invoked on main thread".
- Contract (`docs/contracts/module_contracts.md`): dependencies are
  "Poll-based waiter inside LambdaTask" (no such thing exists).
- Implementation: `done`/`onProgress` run on the **worker** thread
  (`LambdaTask::run` calls them directly); consumers (`PreviewPanel`,
  `imageviewer.cpp`) marshal to the UI thread themselves. The tests pin the
  worker-thread behavior (`done/progress callbacks run on the worker` PASS in
  the current baseline).

## 3. Verified-correct paths (not part of the failures)

- `drain()` is bounded and converges for normal (non-deferred) workloads.
- Rejected submissions (paused pool) leave `pending`/`active_tasks` clean and
  notify the back-pressure handler.
- A/B/C dependency ordering works when the tree completes naturally.
- `ThumbnailPipeline` cache identity (path,size) and generation result-dropping
  (M25) still hold.
- `ImageRepository::loadDirectory` returns correct results in the happy path.
- Empty-directory `loadDirectoryAsync` callback fires exactly once.

## 4. Planned implementation responses (next phases)

| Finding | Phase |
|---------|-------|
| Exactly-once finalize for every terminal path; pending incremented for ALL accepted tasks; single consistent start accounting; deadline-expired tasks finalize + `deadline_exceeded` metric; reverse-dependents map for `cancelTree`; no counter underflow; back-pressure handler invoked outside the lock | Phase 1 |
| Per-consumer request ownership in `MetadataIndexer` (same-consumer supersede only), bounded cache, value/stable cache-read API, MainWindow+ThumbnailPanel linkage regression | Phase 2 |
| `setSources` cancels outstanding work + clears pending + reschedules; rejection erases the pending key; completed handles removed; bookkeeping bounds | Phase 3 |
| `loadDirectoryAsync` rejected→explicit error results + exactly-once aggregate; sync `loadDirectory` bounded without global queue-depth mutation | Phase 4 |
| PreviewPanel stats off the UI thread | Phase 5 |
| Doc/contract/spec sync (worker-thread callbacks, deps contract, stale/mojibake comments) | Phase 6 |

## 5. Gate status

Pre-fix, `.\build.ps1 Test` would run **77 tests with 5 failing** (the four
`m26_*_tests` plus `workflow_ux_tests`). All 72 pre-existing tests still pass.
This is the intended test-first state; Phases 1–4 turn these reds green without
touching the existing gate configuration.
