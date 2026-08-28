# M54 — Perceived Performance & Instant Browse Closure

Date: 2026-08-29
Milestone: M54
Platform: Windows x64, MSVC 2022, Qt 6.10.3
Baseline: `abef794`
Final tree: source-stable before the M54 commit

## Verdict

**Measured Browse closure: PASS.** The default new-directory Browse path now
publishes the first real rows progressively, before a large-directory scan
finishes. Thumbnail cache payload I/O is outside the global bookkeeping lock,
cache hits no longer write file timestamps, viewport demand is latest-wins and
bounded, and selected preview visual delivery runs on the foreground Decode
path before optional statistics.

The M54 benchmark passed with real `ThumbnailPanel`/`PreviewPanel` instances,
100/1,000/10,000-image fixtures, a 50,000-image enumeration fixture, cache
cold/warm repetitions, a rapid viewport demand test, and a PNG encoding
comparison. The final local canonical gate and native physical UX rows are
listed separately below; physical monitor/GPU/DPI/ICC/UNC and prolonged human
perception remain MANUAL/BLOCKED.

## BEFORE / AFTER

The BEFORE values are from the M54 Phase 0 recorder before optimization. The
AFTER values are one final representative run of the same end-to-end harness;
the full CTest gate repeats the deterministic assertions, while absolute
milliseconds remain machine-sensitive.

| Metric | Before | After | Improvement |
| --- | ---: | ---: | ---: |
| 10k first gallery row | 1,023 ms cold | 27 ms cold | 37.9× faster; warm 1,065 → 27 ms |
| 10k scan complete | Not independently observable before rows | 1,561 ms cold | New metric; first row leads by 1,534 ms |
| Cold first thumbnail E2E | 1,052 ms | 79 ms | 13.3× faster |
| Cold first-screen 50% | 1,188 ms | 228 ms | 5.2× faster |
| Cold first-screen 90% | 1,316 ms | 369 ms | 3.6× faster |
| Warm first-screen 90% | 1,183 ms | 158 ms | 7.5× faster |
| Scroll-jump visible p95 | Not measured | 52 ms | New metric; latest target delivered |
| Selected preview p50 | Not measured | 2 ms | New metric; 10k cold sample set |
| Selected preview p95 | Not measured | 6 ms | New metric; 10k cold sample set |
| Fullscreen Next p50 | Not measured by M54 | Not measured by M54 | Existing M47 path retained |
| Fullscreen Next p95 | Not measured by M54 | Not measured by M54 | Existing M47 path retained |
| Thumbnail wasted work | Not measured | 44 cancelled/obsolete decodes | New bounded-demand metric |
| Peak Thumbnail queue | Not measured | 112 pending requests | Bounded by visible + predictive window |
| Peak memory | Not measured by M54 | Not measured by M54 | M47 large-source RSS gates retained |

The final run's complete Browse observations were:

| Fixture | Run | First row | First thumb | Screen 50% | Screen 90% | Preview p50/p95 | Scan complete | Stable |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 images | cold | 23 ms | 3,738 ms | 3,841 ms | 3,939 ms | 2 / 4 ms | 21 ms | 21 ms |
| 100 images | warm | 21 ms | 53 ms | 104 ms | 161 ms | 2 / 5 ms | 17 ms | 17 ms |
| 1,000 images | cold | 24 ms | 80 ms | 204 ms | 324 ms | 2 / 6 ms | 138 ms | 142 ms |
| 1,000 images | warm | 46 ms | 80 ms | 139 ms | 187 ms | 2 / 5 ms | 190 ms | 190 ms |
| 10,000 images | cold | 27 ms | 79 ms | 228 ms | 369 ms | 2 / 6 ms | 1,561 ms | 1,617 ms |
| 10,000 images | warm | 27 ms | 52 ms | 107 ms | 158 ms | 5 / 13 ms | 1,561 ms | 1,613 ms |
| 50,000 images | cold | 42 ms | 106 ms | 257 ms | 375 ms | 2 / 9 ms | 6,435 ms | 6,734 ms |
| 50,000 images | warm | 33 ms | 55 ms | 116 ms | 156 ms | 2 / 7 ms | 6,205 ms | 6,512 ms |

The 50,000 fixture in the AFTER run contains 50,000 tiny real PNGs. It is
intentionally reported as a stress observation, not compared to the
lightweight mixed-entry BEFORE proxy.

PNG encoding was also sampled without changing the cache schema:
`QImageWriter` p50 was 15 ms at default compression and 8 ms at compression
1, but the fast output was 788,300 bytes versus 19,287 bytes for the default
output. That is not a clear first-screen/disk-size win, so the existing PNG
payload and fidelity contract remain unchanged.

## Implementation and answers to the M54 questions

### 1. Why was a new directory slow before?

The worker performed complete enumeration, filtering, sort-key computation,
sort, display-container construction, and model publication before the first
real row could be emitted. The UI shell appeared quickly, but the first useful
gallery state waited for the whole directory operation.

### 2. Why did the first filename appear late?

The old `QDir::entryInfoList()`/complete-result path had no publication point
until the full sorted `Entry` vector existed. M54 adds chunked `QDirIterator`
publication in batches of 128 for the safe default Browse sort/filter path.
The UI appends batches and coalesces visible-range updates; the final sorted
result is applied once for controlled convergence.

The progressive contract is deliberately limited while a non-default sort or
filter is active. Such a mode continues to use the complete sorted result so
that selection and ordering cannot drift while the scan is provisional.

### 3. Was thumbnail cache access globally serialized?

Yes. Before M54 the global `ThumbnailCache` mutex covered cache indexing,
filesystem checks, PNG load/save, and pruning-related work. M54 keeps the
mutex for short key/accounting/LRU state transitions. Normal payload existence
checks, reads, PNG decode, PNG encode, and atomic writes run outside that
bookkeeping lock. Bounded cap pruning remains a maintenance path under the
cache's synchronization boundary.

### 4. Did a warm cache hit have write amplification?

Yes. A successful hit best-effort opened the payload for writing and changed
its file timestamp for cross-process LRU recency. M54 removes that per-hit
write. Each process now maintains a session-local approximate recency order;
startup still seeds persistent recency from file metadata, while cache
correctness remains exact through the existing identity/schema checks.

### 5. Could old predictive work block the current viewport?

Yes. A rapid viewport move could leave old predictive demand in the same
thumbnail scheduling history. `setVisibleRange()` now cancels outstanding
handles, clears stale pending demand, and reschedules the newest visible
window. The M54 stress path measured `peak_thumbnail_queue=112`, delivered all
ten current jump targets, and recorded `scroll_jump_p95=52 ms`; obsolete work
was bounded and the scheduler drained.

### 6. Did selected Preview queue behind thumbnail backlog?

Yes. It previously used Thumbnail priority, so a selected uncached image could
wait behind gallery/predictive work. M54 submits selected-preview decode to the
foreground Decode pool. This keeps the change within the existing scheduler
contract and leaves the gallery Thumbnail pool intact.

### 7. Is visual Preview delivery decoupled from statistics?

Yes. The worker posts the display-ready scaled `QImage` and metadata to the UI
first. The panel presents the bounded preview immediately; preview statistics
are computed and posted in a second guarded event. Request generation,
lifetime, and cancellation checks remain on both delivery paths.

### 8. What is the main fullscreen Next/Previous latency now?

M54 did not change or claim a new fullscreen sequential-navigation number.
For large sources the existing M47 path is the main contract: probe and
bounded display-raster/LOD materialization, then bounded paint, while exact
source materialization remains separate and optional. M47's recorded 100 MP
JPEG result is about 0.5 s to display with +53 MB RSS and no full source frame;
its 100 MP Compare pair is about 0.9 s with +82 MB RSS. A new offscreen-only
viewer benchmark was rejected after it proved unable to provide a reliable
display result, so no unsafe M54 fullscreen claim was made.

### 9. How much did display-raster preload improve things?

No new M54 display-raster neighbor preload was shipped, so its improvement is
**not measured / 0 claimed**. Existing M47 LOD and bounded region-raster
behavior remains green, and M54's Browse/preview changes do not pretend that
a thumbnail is a fullscreen raster. The next navigation optimization should
be profiled on a real viewer surface before adding bounded current/next/prev
display-raster retention.

### 10. Which profiled optimizations were not worth implementing?

- A dedicated `ThumbnailListModel` was not introduced. The profile showed the
  first-row delay was dominated by waiting for complete discovery, and the
  current implementation performs incremental insertion plus one final
  controlled model convergence. Replacing the established model would add
  selection/anchor risk without a measured first-row win.
- A source-identity shortcut was not added across scan/cache/decode. It could
  remove a stat, but external modification after scan makes a stale cache hit
  a correctness failure; the safe identity revalidation remains.
- A separate internal worker-count dispatcher was not introduced. Latest-wins
  cancellation plus the bounded visible/predictive window held the measured
  queue to 112. A new dispatcher remains a follow-up only if real hardware
  profiling shows scheduler submission pressure beyond this bound.
- Direction-aware predictive scoring and a velocity/ML predictor were not
  added. The current fixed forward halo is retained until a reliable viewport
  direction/velocity signal and a user-visible hit-rate profile exist.
- Display-raster neighbor preload and navigation-direction prediction were not
  added because M54 did not obtain a trustworthy viewer first-usable-frame
  measurement. Existing M47 bounded LOD behavior remains the safety boundary.
- PNG schema/format changes were rejected: fast PNG encoding saved about 8 ms
  in this sample but expanded the payload from 19,287 to 788,300 bytes.

## Regression and safety coverage

M54 adds `m54_perceived_performance_tests` to the CTest matrix. It exercises:

- real default Browse publication at 100/1,000/10,000/50,000 images;
- cold and warm filesystem/thumbnail-cache passes;
- progressive first-row-before-scan-complete ordering;
- selected preview visual delivery plus p50/p95 samples;
- rapid viewport cancellation, latest-target delivery, bounded queue, and
  final thumbnail-pool drain;
- PNG encode-speed/size evidence without changing the durable format.

The existing M27 preview lifetime/rejection assertions and canonical Browse
workflow assertions were updated for the foreground Decode priority. M47
Viewer/Compare/exact-source/restore regressions, M53 large-source parity and
soak, Unicode paths, filters/sorts, selection, A→B→A lifetime, and scheduler
drain coverage remain in the full matrix. No build-system, CI, scheduler,
CacheManager, DecoderRegistry, or plugin-framework redesign was introduced.

## Final local gates

The source-stable final tree was required to pass `build.ps1 Test` twice in a
row. Architecture violations must be zero and complexity hard failures must
be zero; advisory complexity warnings remain visible accepted baseline debt.

| Run | Result | CTest real time |
| --- | --- | ---: |
| 1 | 100% tests passed, 0 failed out of 118 | 779.82 s |
| 2 | 100% tests passed, 0 failed out of 118 | 782.14 s |

Both source-stable runs completed with zero architecture violations and zero
complexity hard failures. The focused M54 regression set also passed 23/23.

Physical monitor/GPU/ICC/DPI/Explorer/UNC, installer interaction, and
long-session perceived-smoothness checks remain MANUAL/BLOCKED unless run on
the target Windows environment. M54 closes the measured automated instant
Browse scope without converting those rows into automated PASS claims.
