# M46 — Real-World Workflow Reliability & Long-Session Release Qualification

Date: 2026-08-17 · Milestone: M46 · Version line: 1.0.9 (no version bump —
release process is unchanged)

## Verdict

**AUTOMATED PASS.** The high-risk async cancellation/lifetime race windows are
closed by a strict, deterministically-tested contract; Browse paint paths do no
filesystem metadata I/O; superseded directory/dimension/thumbnail work stops
cooperatively (latest user intent wins); Rating/Tag/Sidecar persistence is
crash-safe with fault-injection regressions; the workflow soak proves idle
resource convergence; and the Architecture gate's 0 warnings no longer depends
on a stale loading-boundary exemption. Native Windows hardware-only rows
(interactive GUI, physical ICC, mixed-DPI, two physical volumes, native
dialogs, long-path/special filenames, multi-hour session) are explicitly
**MANUAL / BLOCKED** in
`docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md` — they are NOT
converted to PASS by green CTest runs.

## A. Real product risks closed

1. **A worker completion racing viewer/compare/panel destruction can no longer
   start a client callback** — the repository suppresses delivery before the
   callback begins when the consumer's lifetime token is dead, and
   `cancelAsync()` waits for an already-started delivery, so after cancel
   returns no callback is running and none will start.
2. **Cancellation during terminal delivery** (the check-then-call race) is
   closed by the delivery gate — previously a cancel that landed between the
   worker's cancel check and the callback invocation would still start the
   callback.
3. **Details/Thumbnail view scrolling and repaint no longer stat every visible
   file** (2 stats per row per repaint previously) — NAS/network/AV/locked-file
   stalls on the paint path are structurally impossible now.
4. **A→B→C directory churn no longer runs superseded scans/dimension probes to
   completion** — walking, header probing and recursive walks stop at the next
   generation checkpoint, so the latest folder gets the worker resources.
5. **The busy cursor can no longer be stranded** — the override cursor is
   ref-counted UI-side; queued scans dropped by destruction drain their refs.
6. **Thumbnail-size drags no longer decode the old size to completion** — a
   size change is a supersession boundary (old-size results neither cached nor
   delivered; stale workload bounded to the in-flight batch).
7. **A crashed write can no longer truncate ratings/flags/tags/sidecars** —
   every user-state file goes through atomic replace; failed writes leave the
   previous official file byte-identical; a failed worker write is retried,
   never silently dropped.
8. **Long sessions converge** — after the real-workflow soak, scheduler
   pending/active/queue/waiting and the dependency graph return to zero,
   ThumbnailPipeline bookkeeping returns to zero, cache obeys caps, RSS growth
   stayed 8.1 MB (allowance 128 MB) and handles shrank by 31.

## B. Important changes

### B1. Async lifetime & cancellation contract (P0)

- New Qt-free `core/async/AsyncLifetimeToken.h` — consumer-owned token,
  invalidated in the owning widget's destructor.
- `ImageRepository` (core, not frozen): requests carry an optional weak token;
  the terminal delivery path runs through a **delivery gate** (mutex + CV)
  that (a) rejects delivery when the request is cancelled, the token is
  expired/invalidated, or a delivery already started (exactly-once), and (b)
  makes `cancelAsync()` wait for an in-flight delivery to finish — except for
  re-entrant calls from the delivering thread itself, which skip the wait.
  Client callbacks and test hooks are invocation-guarded so a throwing/empty
  callback can never strand the gate or kill a worker.
- `ImageViewer::makeImageLoadCallback()` no longer captures raw `this`; the
  delivery helpers (`queueLoadedImage`/`queueImageLoadFailure`) are static and
  marshal only QPointer+path+generation. Viewer/CompareWorkspace/PreviewPanel
  each own a token invalidated before cancellation in their destructors.
- CompareWorkspace and PreviewPanel load through the Application/Core loading
  facades (`ImageLoadingService` / `ImageLoadingFacade`), which now pass
  tokens and expose the preview-cache boundary.

### B2. Browse latest-intent priority (P0)

- Delegates (`ThumbDelegate`, `DetailsDelegate`, `ListDelegate`) and the
  Preview panel paint ONLY from scan-cached `Entry` data (size, mtime, suffix,
  dimensions). `Entry`-backed display list (`m_displayEntries`) is rebuilt by
  `buildModel`, so filtered/recursive rows are covered too; suffix/fileName
  are lexical string splits.
- Directory scans, dimension probes and recursive searches re-check a shared
  generation token every iteration (`m_scanGenToken`) and abort cooperatively;
  a deterministic iteration probe (mutex-protected, exception-safe) proves
  superseded scans/probes run at most the in-flight iteration.
- Busy cursor: `m_busyCursorRefs` (UI-side refcount). `setDirectory` adds one
  ref; every completion/abort marshals one restore; the destructor drains refs
  whose queued workers were dropped by `m_scanPool.clear()`.
- `ThumbnailPipeline::setThumbSize()` is a new supersession boundary: bumps the
  generation, cancels in-flight old-size handles, clears the old-size memory
  cache and pending keys.

### B3. Crash-safe persistence (P1)

- New Qt-free `core/filesystem/AtomicFile.{h,cpp}`: unique same-directory temp
  → write/flush/close with stream-state checks → atomic replace
  (`MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)` on Windows). On any failure
  the previous official file is untouched and the temp is removed; aged stale
  temps are swept; stale temps are never read as state. Deterministic fault
  injection (`setAtomicWriteFaults`).
- `RatingStore`: all edits (rating/colorLabel/rejected/picked/recents) coalesce
  on the existing owned worker with a 100 ms debounce; `save()`/`flushSave()`
  is the explicit flush boundary; a failed worker write re-arms the dirty bit
  (retried, never silently dropped); shutdown flushes.
- `TagStore`/`SidecarStore`: synchronous writes via atomic replace.

### B4. Workflow soak (P1)

- `benchmarks/m46_workflow_soak_main.cpp` — one driver, two durations:
  `--iterations 8` (CTest, RUN_SERIAL) and `--extended` (40 iterations, manual
  Release qualification). Loop: browse A/B → scroll + size churn → viewer
  next/prev + zoom → compare pair change → analyze (stats) → export (full /
  pre-cancelled / mid-flight cancelled) → directory switch. Verdicts:
  scheduler pools + dependency graph → 0, ThumbnailPipeline handles/pending →
  0, cache ≤ configured caps, RSS growth ≤ max(128 MiB, 15%), handles ≤ +64,
  export artifacts stable, no busy cursor at idle.
- Measured short run: idle rss=40.6 MB, handles=218, liveFrames=0,
  cacheMem=cacheDisk=5.5 MB, pipe(h=0,p=0), sched(p=0,a=0,q=0,w=0,g=0);
  RSS growth 8.1 MB (allow 128 MB), handle growth −31 (window +64); 16.3 s.

### B5. Architecture gate honesty (P2)

- `scripts/architecture_gate.ps1`: the `imageviewer.cpp`/`previewpanel.cpp`
  loading-boundary exemption is removed (the facade exists); R2 now applies to
  the Compare layer too. `scripts/architecture_gate_test.ps1` plants a UI R2
  violation, a Compare R2 violation, a Domain R4 violation and a clean
  facade-based include, then asserts the real tree reports 0 warnings —
  registered as CTest `architecture_gate_regression`.

### B6. Performance/Qt claim reconciliation (P2)

- README: separate tables for the automated gate (`performance_budget.json`),
  the ±10% regression axis, and the manual product targets; Qt matrix
  explicit (minimum/CI 6.8.0, dev-verified 6.10.3, legacy 6.11.1 fallback path
  documented as such). `docs/build.md` carries the same matrix.

## C. Regression evidence

| New test | What it proves (and why it failed before the fix) |
|---|---|
| `m46_repository_tests` (50/50) | Delivery gate: dead-token suppression; prompt cancel for not-yet-delivered requests; cancel DURING terminal delivery waits and the started delivery completes exactly once (pre-fix: the callback could start after cancel); re-entrant cancel from inside the callback returns (pre-fix: deadlock — the gate waited for its own completion; fixed via delivery-thread-id skip); A→B→A supersession (superseded request never delivers); rejection with dead token; 300-cycle submit/cancel churn converges with zero callbacks after cancel. Also caught two real defects during development: empty-token requests were wrongly suppressed (empty weak_ptr reads as expired) and invoking empty test hooks threw `bad_function_call` inside the worker (silently aborting delivery). |
| `m46_thumbnail_tests` (29/29) | Mid-flight resize: old-size results neither delivered nor cached, cache holds exactly the new-size keys, no further old-size scheduling; rapid A→B→C→D size churn converges with only the final size; `setSources` mid-decode drops all stale results; idle → pipeline handles/pending 0 and ThumbnailPool metrics 0. Pre-fix: old-size decodes ran to completion and repopulated the cache (memCacheSize was 2× and stale-size deliveries occurred). |
| `m46_browse_tests` (41/41) | B1/B2: Details and Thumbnail renders are pixel-identical after the source files are deleted (pre-fix: paint stat()ed files → "0 B"/empty dates → renders diverged). B3/B4: superseded scans/dimension probes run at most the in-flight iteration (pre-fix: they ran the whole directory; also exposed a probe-reset race that crashed the worker — fixed with mutex-copied, exception-safe probe invocation). B5: busy cursor balanced when queued scans are dropped by destruction. B6/B7/B8: viewer destroy mid-decode, viewer A→B→A newest-wins, compare swap-then-destroy — deterministic latches, pools converge. |
| `m46_persistence_tests` (46/46) | Atomic replace: success writes complete new content; injected temp-create/write/replace failures return false, leave the previous file byte-identical and clean the temp; stale temps never read as state and aged ones are swept; RatingStore rapid updates coalesce with last-write-wins after the flush boundary; failed flush preserves the file; failed worker write is retried and persisted; Tag/Sidecar same semantics. |
| `m46_workflow_soak` | See B4 numbers. Pre-fix there was no end-to-end convergence gate at this granularity. |
| `architecture_gate_regression` | Planted violations are flagged (UI R2, Compare R2, Domain R4); facade-based include is clean; real tree = 0 warnings. |
| Existing gates re-run | `test_m27_repository` 109/109, `m26_repository` 11/11, `ratingstore` 12/12, `flags` 10/10, `m27_lifetime` 30/30, `async_lifetime` PASS, `browse_convergence_ui` PASS, `m26/m27_thumbnail` PASS, `test_thumbnailpipeline` 7/7 — none weakened. |

## D. Performance / resource evidence

- Soak (8 iterations, this machine — 4-logical-core VM): see B4 numbers.
  RSS growth 8.1 MB vs 128 MB allowance; handles −31 vs +64 window; every
  scheduler pool and the dependency graph at 0; pipeline 0; cache 5.5 MB vs
  ~850 MB aggregate memory cap; 16.3 s wall.
- Paint path: previously 2 `QFileInfo` stats per Details row per repaint; now
  zero filesystem calls in any delegate/preview paint (structural + the
  delete-files render test).
- `bench_enforce` (the absolute-cap performance gate) remains part of the full
  gate and its budget numbers are unchanged — this milestone did not loosen
  any budget; only the README's description of what each number means was
  made explicit.

## E. Remaining risks (automated evidence cannot cover)

See `docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md` for the full
checklist. Key rows: interactive desktop workflow + perceived smoothness,
100/125/150/200% DPI, mixed-DPI dual monitor, physical ICC profile changes,
two real physical volumes, native open/save dialogs, long-path (>260) and
reserved-name filenames, and the multi-hour interactive long-session
observation — all **MANUAL / BLOCKED** on this offscreen single-volume VM.
Unicode (Chinese+emoji) rename round-trip and cross-volume fallback are
covered automatically (`test_commandstack`).

## F. Final gate

Full `.\build.ps1 Test` runs (96 tests incl. the six new M46 gates):
runs #1/#2/#3 all passed (100% tests passed, 0 failed) — recorded in
`build_msvc/m46_gate_run{1,2,3}.log`. Architecture gate 0 warnings;
complexity strict gate 0 hard failures.
