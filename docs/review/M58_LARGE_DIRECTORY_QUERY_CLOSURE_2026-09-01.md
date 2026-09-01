# M58 — Large-directory query closure (2026-09-01)

## Result

M58 automated closure is complete. The Browse query path now uses a value
`BrowseQuery`, immutable store/index snapshots, cancellable latest-wins worker
evaluation, and a real text debounce. Sort/type changes operate on the
committed directory snapshot. Metadata callbacks are delivered in 256-entry
batches, and SearchPanel result rows are populated incrementally. Native
first-paint, keystroke cadence, and long-session feel still require the
interactive desktop qualification described in the Phase-0 baseline.

## BEFORE / AFTER evidence

The BEFORE column is deliberately source-backed. No native GUI number is
invented where the recorder could not run in this environment.

| Metric | BEFORE | AFTER / evidence |
| --- | --- | --- |
| 10K filename query | No deterministic query gate; native timing MANUAL/BLOCKED | Immutable snapshot search: 11 ms in `m58_large_directory_query_tests`; native gallery timing MANUAL/BLOCKED |
| 50K filename query | No deterministic query gate; native timing MANUAL/BLOCKED | Immutable snapshot search: 63 ms in `m58_large_directory_query_tests`; native gallery timing MANUAL/BLOCKED |
| Rating filter | Per-entry singleton calls; native latency MANUAL/BLOCKED | One RatingStore snapshot per generation; background latest-wins evaluation |
| Tag filter | Per-entry mutex lookup; native latency MANUAL/BLOCKED | One TagStore snapshot per generation; background latest-wins evaluation |
| Recent filter | Per-entry `recents()` vector copy + linear search | One snapshot with ordered vector plus `recentSet` membership lookup |
| Type filter | Changed directory via `setDirectory()` and rescanned | In-memory query over `m_allEntries`; no query-triggered rescan |
| Cached sort | Sort changes re-entered directory lifecycle | Stable in-memory sort over the committed snapshot |
| Metadata filter | Per-file callback storm; native cold/warm latency MANUAL/BLOCKED | 256-entry batches, progressive query refinement; native latency MANUAL/BLOCKED |
| SearchPanel query | Synchronous UI-thread index traversal | Cancellable worker over one SearchEngine snapshot |
| SearchPanel large-result first publication | Synchronous creation of every `QTableWidgetItem` | 256-row event-loop chunks; native first-publication timing MANUAL/BLOCKED |
| Query cancellation / wasted work | No latest-wins generation guard on the old synchronous path | One active filter/search handle per panel, cancellation token + generation guard |
| UI-thread max blocking slice | Native recorder unavailable (MANUAL/BLOCKED) | Heavy large-directory evaluation leaves the UI thread; native slice MANUAL/BLOCKED |
| Directory rescan count for sort/type | One `setDirectory()` scan per change | 0 for query-only sort/type changes; explicit refresh/delta still owns scans |
| Metadata UI callbacks (N entries) | N per-entry callbacks | `ceil(N/256)` batches (10K: 40; 50K: 196); cancellation drops queued batches |
| Peak query working set | Multiple synchronous rebuilds; no bounded query evidence | One source snapshot + one result + compact store snapshots; scheduler queue bounded |

The existing M54 50K browse benchmark remains the performance context for the
display pipeline: cold first row 41 ms, first thumbnail 73 ms, screen-50 153 ms,
screen-90 227 ms, scan complete 5,240 ms, and stable 5,427 ms. Those numbers
are not relabeled as gallery-filter timings.

## Final answers

1. Gallery live search could stall because `textChanged` reached a synchronous
   O(N) `applyFilter()` that rebuilt the model and took global-store locks in
   the entry loop. Large evaluation is now off-thread and latest-wins.
2. The old comment promised debounce, but the signal was directly connected to
   `ThumbnailPanel::setFilter()`. The panel now uses a single-shot 25 ms timer.
3. Previously each flag setter caused one full filter rebuild. A sequence of
   separate UI signals could therefore cause one rebuild per signal; the
   grouped clear operation is now one logical query commit.
4. For a matching entry, the old path could take five RatingStore locks
   (rating, color, reject, pick, recents) and one TagStore lock: six locks total.
5. Yes. `recents()` copied the vector for each entry and then linearly searched
   it. The snapshot retains order but adds an O(log N) membership set.
6. No. `setSortMode`, `setSortAscending`, and `setTypeFilter` no longer call
   `setDirectory()`.
7. No. Query-only sort/type changes use the existing `m_allEntries` snapshot;
   filesystem enumeration remains owned by refresh, directory changes, and
   live-folder deltas.
8. It is improved: a non-default query can evaluate the currently available
   listing immediately and refine as scan/metadata batches arrive. Final order
   is only committed by the guarded query result; native first-paint timing is
   MANUAL/BLOCKED.
9. MetadataIndexer changed from one callback per indexed entry to batches of
   at most 256 entries (`ceil(N/256)` callbacks for a complete run).
10. No. SearchPanel searches an immutable SearchEngine snapshot on a background
    task; only bounded result chunks touch the UI table.
11. Yes. The button emits `reindexRequested()`, and MainWindow owns the real
    `reindexSearch` operation and feeds the resulting entries back.
12. No stale query can commit: cancellation is cooperative and the UI
    publication checks both the query generation and the panel lifetime token.
13. Selection and current-image identity are restored by path; incremental
    live updates preserve the scroll anchor where possible. Preview and
    thumbnail sources follow the committed visible path list.
14. Yes. M56 directory/sidecar deltas still feed the active query through the
    incremental path, with directory generation and query generation guards.
15. The deterministic M58 snapshot search measured 11 ms for 10K and 63 ms for
    50K. Native BEFORE/AFTER gallery typing/filter latency is MANUAL/BLOCKED;
    the M54 50K display context is recorded above without conflation.
16. The final source-stable tree passed the complete CTest suite in two
    consecutive `build.ps1 Test` runs: 125/125 each run.
17. Yes. Architecture and complexity gates report zero hard failures.
18. Native first-paint, keypress-to-correct-state, typing cadence, SearchPanel
    first publication, DPI/ICC/native-dialog behavior, hostile metadata, and
    extended long-session feel remain MANUAL/BLOCKED until run on the target
    Windows desktop.

## Reproduction

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test
```

The M58 executable additionally prints the 10K/50K snapshot timings and the
metadata batch/cancellation checks. The build entry pins the UTF-8 console page
before CMake probes MSVC `/showIncludes`; `ninja -t deps` consequently records
the full header dependency set instead of silently accepting zero dependencies.
