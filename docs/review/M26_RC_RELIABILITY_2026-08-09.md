# M26 RC Reliability Closure (2026-08-09)

> The async-runtime closure of MViewer's RC candidate: every lifecycle path of
> the scheduler, the metadata index, the thumbnail pipeline and the image
> repository was audited against its spec, reproduced with failing tests first,
> and hardened without adding features, changing the build/CI, or touching the
> frozen infrastructure beyond the correctness fixes explicitly allowed by the
> milestone. Full local gate **78/78 green**; M25 S1–S9 + T1–T4 re-run in both
> scheduler configs; new M26 T5–T10 stress all green.

## 1. Baseline: what was actually broken (reproduced before any fix)

Baseline audit + failing regressions: `docs/review/M26_ASYNC_BASELINE_2026-08-09.md`,
pinned by 5 new CTest suites (`m26_scheduler_tests`, `m26_metadata_tests`,
`m26_thumbnail_tests`, `m26_repository_tests`, `m26_stats_tests`) and a new
real-MainWindow workflow (`workflow_ux_tests` Workflow 6). Pre-fix the gate
was 77 tests with 5 failing; 72 pre-existing tests stayed green throughout.

| # | Bug reproduced | Root cause | Fix |
|---|----------------|-----------|-----|
| 1 | Deadline-expired task never finalized: `pending`/`active_tasks` stuck at 1, handle leaked, `deadline_exceeded` metric never incremented | `LambdaTask::run()` returned at the deadline check before the finalize wrapper | deadline path now reports `deadline_exceeded`, still invokes the terminal wrapper (metrics + handle removal + `done`) |
| 2 | Deferred (dependency-waiting) tasks never incremented `pending` → **size_t underflow** on completion; submit-time-released tasks skipped all metrics | metrics updated only in the non-deferred submit path | `pending` incremented for EVERY accepted task; one consistent start-accounting helper for both release sites |
| 3 | `cancelTree(root)` walked **prerequisites**, not dependents: cancelled A's dependencies (3b), missed A's dependents (3) | `m_depGraph` stores prerequisites; BFS followed it | reverse-dependency map `m_dependents`; BFS over dependents; edges cleaned on release/cancel |
| 4 | `cancelTree` on a waiting task underflowed `active_tasks`/`queue_depth` → **every later submit on that pool silently rejected** (nullptr) — in the test this crashed with `0xC0000005` on a null handle; the same bug also released stale deferred runnables with dead captures (**use-after-free** in a chained scenario) | waiting tasks never counted as active/queued but `cancelTree` decremented both | `waiting` metric added; cancelTree decrements exactly what each state holds; no counter can underflow |
| 5 | MetadataIndexer: consumer B's `index()` unconditionally cancelled consumer A's in-flight request and vice versa; the gallery filter could stay `indexing=true` forever when MainWindow's 500 ms search re-index fired mid-pass (Workflow 6) | single global generation + single handle | per-request ownership: requests are independent; `cancelRequest(id)` supersedes only the owner's own stale request; consumers wired (MainWindow `m_reindexRequestId`, panel `m_metaRequestId`) |
| 6 | `MetadataIndexer::cached()` returned a raw pointer into the cache map after releasing the lock — rehash-invalidated by any concurrent index pass | pointer escape | value-semantics `std::optional<Entry>` API; call sites updated |
| 7 | Metadata cache unbounded (whole session history) | no budget | bounded FIFO cache (`setCacheLimit`, default 100 000) with directory-working-set eviction |
| 8 | ThumbnailPipeline: `setSources(B)` never cancelled A's queued work (decode-then-drop waste); same path in both generations permanently pending-blocked; scheduler-rejected submit poisoned the key forever; completed handles accumulated with the whole browse history | `setSources` only bumped the generation; `m_pending` inserted before submit and never erased on rejection; `m_handles` never erased on completion | `setSources` cancels outstanding handles + resets pending; generation-tagged pending/handle bookkeeping (old-gen tasks can't clobber new-gen keys); rejection erases the key (retryable); handles removed at completion → bounded working set |
| 9 | ImageRepository: `loadDirectoryAsync` aggregate callback could **never fire** under saturation (dropped submissions); sync `loadDirectory` busy-waited forever when the pool was paused and **clobbered caller-configured queue depth** (set 0, restored hard-coded 1000) | unchecked submits + unbounded busy-wait + global config mutation | every submit checked; rejected → explicit failure Result + counted; aggregate fires exactly once; sync load bounded (rejected items fail fast + defensive budget) and touches no global scheduler configuration |
| 10 | PreviewPanel computed full-image luminance/RGB statistics on the UI thread inside the load callback; ROI stats converted the full QPixmap on the UI thread | stats over QPixmap in a GUI callback | worker-side `core/image/ImageStats.{h,cpp}` over `ImageData`; UI receives only the small struct and paints; ROI stats computed from the frame pixels (no full-image conversion) |
| 11 | Doc/impl drift: spec claimed `done` on the main thread (impl: worker), contract claimed a non-existent "poll-based waiter" | — | contract pinned to the implementation (worker thread), both spec and contracts updated; all UI consumers already marshal themselves |

## 2. Regression tests (test-first)

| Suite | Covers |
|-------|--------|
| `m26_scheduler_tests` (64 checks) | deadline-before-start finalize + metric; A→B→C ordering + metrics convergence; submit-time deferred release; `cancelTree(A)` cancels transitive dependents; `cancelTree(B)` never cancels prerequisites; waiting-task cancel; rejection residue-free + backpressure; callback-thread contract |
| `m26_metadata_tests` (20 checks) | dual-consumer both orderings; same-consumer supersede; `cancelRequest` isolation; bounded cache + value semantics |
| `m26_thumbnail_tests` (37 checks) | generation switch stops obsolete decode; shared path delivered for current generation; rejected submit retry; handle/pending bookkeeping bounded across 10×1000 browse churn |
| `m26_repository_tests` (11 checks) | saturated exactly-once aggregate; explicit failure results; paused sync load bounded; queue-depth config preserved; empty dir exactly once |
| `m26_stats_tests` (10 checks) | stats correctness across all pixel formats; 24 MP worker budget (~90 ms) |
| `workflow_ux_tests` Workflow 6 | real MainWindow + ThumbnailPanel: camera filter applies while the 500 ms search re-index runs concurrently (4000-DNG fixture) |

## 3. Implementation changes

`src/core/scheduler/TaskScheduler.{h,cpp}`, `src/core/metadata/MetadataIndexer.{h,cpp}`,
`src/core/thumbnail/ThumbnailPipeline.h`, `src/core/image/ImageRepository.cpp`,
`src/core/image/ImageStats.{h,cpp}` (new), `src/previewpanel.{h,cpp}`,
`src/imageviewer.cpp`, `src/mainwindow.{h,cpp}`, `src/thumbnailpanel.{h,cpp}`,
`src/thumbnailpanel_filters.cpp`, `src/CMakeLists.txt` (test registration),
`benchmarks/m24_soak_main.cpp` (T5–T10). Complexity guards hold:
`mainwindow.cpp` 639 / `compareworkspace.cpp` 731 / `thumbnailpanel.cpp` 724
(ADR-014). No changes to `build.ps1`, `CMakePresets.json`, CI.

## 4. Full gate

`.\build.ps1 Test` → **78/78 passed, 0 failed** (was 73 pre-M26):

- 5 new `m26_*` suites + Workflow 6 green.
- Hard gates green and unchanged: `golden_image`, `bench_smoke` (≈90 s),
  `bench_enforce` (≈222 s, ±10% budget vs committed baseline — no performance
  regression), `version_consistency`, `workflow_ux_tests`, `async_lifetime_tests`,
  the four M24 acceptance suites, `browse_convergence*`.

## 5. Stress results (both scheduler configs)

Results JSON: `docs/review/M26_STRESS_RESULTS_2026-08-09.json` (default) and
`..._1THREAD_2026-08-09.json` (one worker per pool).

### M25 regression re-run (unchanged behavior)

| Scenario | Default | 1-thread | M25 baseline | Status |
| --- | ---: | ---: | ---: | --- |
| S1 10K first entries | 761 ms | 743 ms | 727/729 ms | Pass |
| S2 rapid switch worst | 418 ms | 398 ms | 396 ms | Pass |
| S3 max UI gap | 38.5 ms | — | 37.0 ms | Pass |
| S4 24 MP JPEG | 968 ms | 951 ms | 921 ms | Pass |
| S5 4K TIFF | 416 ms | 407 ms | 404 ms | Pass |
| S8 Compare cycles | 50/50 | 50/50 | 50/50 | Pass |
| S9 Workspace round trips | 500/500 | 500/500 | 500/500 | Pass |
| **T1** size churn / worst stall / stale / RSS | 740 ms / 12.7 ms / **0** / +2.3 MB | 751 ms / 14.5 ms / **0** / +1.1 MB | 847 ms / 37.2 ms / 0 / +6.3 MB | Pass (stall improved) |
| **T2** switch worst first/full / stale rows | 90 ms / 10 ms / **0** | 90 ms / 11 ms / **0** | 122 ms / 42 ms / 0 | Pass |
| **T4** metadata work / worst stall / counts | 10.9 s / 17.2 ms / 500·143·100·50 | 10.9 s / 13.0 ms / 500·143·100·50 | 10.9 s / 28.3 ms / 500·143·100·50 | Pass (stall improved) |

### New M26 stress (T5–T10)

| Scenario | Default | 1-thread | Requirement |
| --- | ---: | ---: | --- |
| T5 scheduler dep/cancel/deadline churn (120 iters) | 0 converge failures, 0 drain timeouts | same | every pool converges to 0 pending/waiting/active/queue_depth |
| T6 saturation/rejection/drain (20 rounds × 200) | 3960 rejected, all metrics 0 after | same | bounded drain, no residue |
| T7 MetadataIndexer dual-consumer race (6 rounds × 1500 files) | 6/6 both consumers complete; cache ≤ limit (3000) | same | no mutual cancellation |
| T8 thumbnail generation/backpressure churn | 35 delivered (last window + settle), pending 0, handles 0 | 41 delivered, pending 0, handles 0 | no permanent pending, no retained handles, current window decoded |
| T9 repository saturated async completion (8 rounds × 40 files) | 8/8 exactly-once, ≥38 explicit failures | same | exactly-once under real backpressure |
| T10 long-session trend (12 cycles) | RSS +29.6 MB, handles −8, scheduler/pipeline state 0, cache 3800 ≤ limit | RSS +29.8 MB, handles −4, state 0 | bounded resource trend, internal state converges |

T10's RSS growth is the thumbnail memory LRU warming to its bound
(512 × 256 px entries); the scheduler/pipeline bookkeeping and the metadata
cache are all converged at the end — the requirement was "state converges",
not "zero cache".

## 6. Performance before/after

- No gate regression: `bench_enforce` green against the committed baseline.
- UI-thread stalls improved (not caused by M26 alone): T1 worst stall 37.2 →
  12.7 ms, T4 worst stall 28.3 → 17.2 ms.
- Preview stats: 24 MP full-image statistics now run on the worker
  (~90 ms off the UI thread; the UI applies a 32-byte struct + repaints).
- Thumbnail obsolete-work: directory switches now cancel queued decodes
  before they start (previously decode-then-drop).

## 7. Remaining known limitations

- A scheduler-rejected thumbnail key is retried on the next viewport
  request (`setVisibleRange`/`request`) — the panel already re-requests on
  every scroll/resize/directory load, so a fully idle viewport after a
  rejection storm is the only case with delayed thumbs (no permanent loss).
- `loadAsync` (single-image) still has no failure delivery on scheduler
  rejection — unchanged behavior, out of M26 scope (callers show "loading"
  state, and rejects are rare at default depth).
- Metadata cache eviction is FIFO, not LRU — fine for directory working sets,
  documented in the header.
- Deadline expiry is only honored at task start (mid-run expiry is not
  preempted) — unchanged, as specified.
- T10 soak numbers were produced on the dev machine; target-hardware re-run
  remains a sign-off item.

## 8. Exit criteria checklist

1. Scheduler deadline/cancel/dependency/rejection exactly-once finalize — ✅ (`m26_scheduler_tests`).
2. pending / active / queue_depth converge to zero, no underflow — ✅ (tests + T5/T6/T10).
3. `cancelTree(A)` cancels transitive dependents — ✅ (tests 3/3b/3c).
4. Drain/shutdown bounded, no bogus-count pass — ✅ (T5/T6; drain unchanged semantics).
5. Callback thread contract pinned by tests AND docs — ✅ (test 5 + spec + contracts).
6. MainWindow Search + gallery filter use MetadataIndexer concurrently — ✅ (`m26_metadata_tests`, Workflow 6).
7. Metadata cache API has no dangling pointers — ✅ (value semantics).
8. ThumbnailPipeline: no permanent pending, no retained completed handles, no stale delivery — ✅ (37-check suite + T8).
9. Rejected thumbnail submits recover/retry — ✅ (`m26_thumbnail_tests` 3, T8 settle).
10. `loadDirectoryAsync` exactly-once under saturation/rejection — ✅ (`m26_repository_tests` 1, T9).
11. No permanent Repository busy-wait — ✅ (test 2, bounded budget).
12. Preview/full-image heavy statistics off the UI thread — ✅ (ImageStats + PreviewPanel).
13. M25 S1–S9 / T1–T4 no regression — ✅ (table above).
14. New M26 stress all green — ✅ (T5–T10 both configs).
15. `.\build.ps1 Test` 78/78 — ✅.
16. golden/benchmark hard gates not relaxed — ✅ (unchanged config, green).
17. Complexity guard not degraded — ✅ (ADR-014 numbers above).
18. Docs consistent with implementation — ✅ (specs, contracts, STATUS, roadmap, CHANGELOG).
19. No new product features — ✅ (no capability additions).
20. Build/CI untouched — ✅ (only `src/CMakeLists.txt` test registration).
21. No forged human UX signature — ✅ (UX sign-off explicitly pending below).

## 9. Verdict

**AUTOMATED RC READY — TARGET HARDWARE / HUMAN UX SIGN-OFF PENDING.**

The automated evidence for RC reliability is now complete: `.\build.ps1 Test`
(78/78) means the async semantics are correct, cancellable, exitable, and
convergent under long sessions. What remains is exactly what automation cannot
provide: a target-hardware re-run of `mviewer_m24_soak.exe`
(S1–S9 + T1–T10, both scheduler configs, against the committed baselines) and
the UX Review Agent's human signature on `docs/beta_checklist.md`.
