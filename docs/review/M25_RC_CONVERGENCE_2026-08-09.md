# M25 RC Convergence — Professional Browse Data Pipeline (2026-08-09)

> Phase 2 closure of M25. The Professional Browse / FastStone-style workspace
> data chain — Thumbnail pipeline → format identity → metadata/search/sort —
> was converged end to end. Every claim below was verified against the tree at
> commit `28ccef4` + this phase by running the full local gate (73/73) and the
> S1–S9 + T1–T4 Release soak (both default and one-worker configurations).
> Evidence: this document, `M25_STRESS_RESULTS_2026-08-09-rc.json`,
> `M25_STRESS_RESULTS_1THREAD_2026-08-09-rc.json`, the Phase-1 regression
> suites `test_browse_convergence` and `browse_convergence_ui_tests`, and the
> extended `filesystem_tests` / `workflow_ux_tests`.

## 1. Verified real risks (baseline audit, Phase 0)

The milestone spec's risk list was audited against the code before any fix.
All of the following were CONFIRMED real:

| # | Claimed risk | Where it lived | Confirmed |
|---|--------------|----------------|-----------|
| 1 | Thumbnail cache identity is path+mtime+size only — no requested size | `ThumbnailCache::keyFor` | ✅ a 64 px and a 240 px thumbnail shared one key |
| 2 | Ready/pending maps survive a size switch; old-size pixmaps render at the new size | `applyThumbSize`, panel `m_thumbReady` | ✅ ready 64 px was painted into a 240 px cell |
| 3 | In-flight old-size results land as new-size "cache hits" | pipeline memCache keyed by path only | ✅ 240 px request hit a 64 px mem entry |
| 4 | Decode lambda ignores `TaskContext`; stale queued work still decodes; cancelled results still delivered | pipeline `enqueueLocked` | ✅ old-directory results pollute after `clear()` |
| 5 | Visible-range disk probe = synchronous stat + PNG load per cell ON the GUI thread, duplicating the worker's cache read | `updateVisibleRange` | ✅ UI-thread I/O + two cache paths |
| 6 | Worker threads create QPixmap (GUI resource) | `ThumbnailProvider::squareFit/cache` | ✅ QPixmap off the GUI thread |
| 7 | PNG encode/write runs on the UI thread | result lambda → `produce()` on the GUI thread | ✅ UI-thread disk write |
| 8 | Format lists diverge: FileSystem (6), panel suffix list (8, no RAW), sortedEntries (18 incl. RAW), viewer (8), Open-File dialog (own list), SidecarStore (own list) | 6+ sites | ✅ RAW dirs counted differently in gallery vs status bar |
| 9 | Search re-index + panel meta index each re-parse the whole directory on the UI thread | `MainWindow::reindexSearch`, `ensureMetaIndex` | ✅ full-directory synchronous metadata I/O |
| 10 | Resolution/Camera/Lens sorts do O(N log N) file I/O inside comparators | `sortedEntries` | ✅ `QImageReader::size()` / `parseRawMetadata` inside `std::sort` |
| 11 | Recursive filename search walks the tree synchronously per keystroke | `applyFilter` | ✅ UI-thread directory walk |
| 12 | Camera/Lens filters match the concatenated blob (cross-field hits) | `applyFilter` blob contains | ✅ camera filter could match lens-only text |
| 13 | `rm.iso` never populated → ISO filter/details always 0 | `RawMetadata` parser | ✅ only `isoSpeed` was set |
| 14 | Browse workspace close saved the hidden state, not the pre-Browse state | `closeEvent` | ✅ only Focus Browse was guarded |
| 15 | 4 user-visible mojibake strings in the gallery | `thumbnailpanel.cpp` | ✅ garbled Chinese |

## 2. What landed (per phase)

### Phase 1 — regression baseline (all green, committed first-class)
- `core/test_browse_convergence.cpp` → ctest `browse_convergence_tests`:
  field-scoped metadata matching (camera/lens/ISO semantics), sort-key
  once-per-file contract (counting readers prove exactly N expensive reads for
  N files, only for the field that needs them), `MetadataIndexer` async /
  progressive / cancellable / cache-reuse contract.
- `test_browse_convergence.cpp` → ctest `browse_convergence_ui_tests`
  (RUN_SERIAL): ThumbnailCache identity (64→240 never reuses; mtime change
  invalidates; size coexists), pipeline size-switch (old-size results never
  delivered), pipeline generation cancellation (old-directory results never
  delivered after `clear()`), real-panel size switch (stale 64 px dropped,
  240 px arrives), RAW/mixed directory listing == FileSystem count, real-panel
  camera/lens/ISO field-scoped filters via fake-DNG metadata.
- `core/test_filesystem.cpp` `SupportedFormatSSOT`: RAW/WebP/GIF listed by
  `FileSystem`, every listed suffix claimed by a decoder.
- `workflow_ux_tests` close-while-Browse regression: closing inside the Browse
  workspace persists the pre-Browse Analysis/Search visibility (mirrors the
  existing close-in-Focus regression).

### Phase 2 — thumbnail pipeline convergence
- **Cache identity:** `ThumbnailCache` key = SHA1(path | mtime | size |
  requestedSize | schemaVersion); QImage payload (thread-safe off the GUI
  thread); PNG encode/write on the worker.
- **Threading contract:** `ThumbnailProvider::produce` is the single worker
  path (cache → decode → square-fit → cache store, QImage end to end). The GUI
  thread only converts the finished `ImageData` to a QPixmap for painting.
  The visible-range disk probe was deleted (one authoritative cache path).
- **Size switches:** `applyThumbSize` drops ready/pending maps, re-requests the
  visible window at the new size; the result callback drops any result whose
  size ≠ current thumb size (in-flight old-size results cannot land).
- **Lifecycle/cancellation:** every task carries its generation; `setSources`/
  `clear()` bump it; tasks check the scheduler cancel flag BEFORE decoding and
  drop results from superseded generations (no cache write, no delivery).
  The memCache is keyed by (path, size).
- `ThumbnailPipeline`'s `ResultFn` now carries the requested size
  (`(path, size, thumb)`); all callers (panel, 4 test/bench files) updated.

### Phase 3 — supported-format SSOT
- New `core/image/ImageFormats.{h,cpp}`: the decoder registry's extension set
  is the single truth (Qt formats + RAW + plugin decoders), cached with a
  pre-QCoreApplication guard (computed before the app exists → recomputed once
  when it appears; a static-initializer caller previously poisoned the cache —
  caught by the new mixed-format test).
- Migrated: `FileSystem::listImages/isImage/imageFilters`, panel directory
  scan + recursive search + suffix gate, `ImageViewer::listImages` (made lazy),
  MainWindow Open-File filter, `BatchDialog` file picker, `SidecarStore`
  export walk, both demo harnesses. RAW-only and mixed directories now count,
  browse, search and compare identically everywhere.

### Phase 4 — metadata / search / sort pipeline
- New `core/metadata/MetadataIndexer.{h,cpp}`: generation-scoped background
  indexing with per-path cache keyed by file identity, progressive main-thread
  delivery, cancellation, and one shared index for both consumers.
- `MainWindow::reindexSearch` now feeds the SearchPanel from the indexer (no
  UI-thread metadata I/O); `SearchEngine` gains `indexEntries` from
  `MetadataIndexEntry` blobs (identical blob text, one parse per file total).
- `ThumbnailPanel::ensureMetaIndex` consumes the same indexer (async, gen
  guarded) for meta-search + camera/lens/ISO maps; filters are field-scoped
  (`m_metaCamera`/`m_metaLens`/`m_metaIso`) via `core/search/MetadataFilter.h`.
- Recursive filename search runs off the UI thread with generation guards —
  and a real bug surfaced by T4: a completed scan re-entered `applyFilter` and
  relaunched itself forever instead of merging results (fixed with an
  explicit hits-currency state machine).
- Sort keys: `core/image/ImageSortKeys.{h,cpp}` computes one key per file
  (O(N) I/O, only the fields the sort needs), then a pure-memory comparator;
  the panel's `sortedEntries` uses it for all 8 sort modes.
- `RawMetadata` parser now populates `iso` (the UI's canonical field) from the
  ISO tag alongside `isoSpeed`.

### Phase 5 — browse polish
- Close-while-Browse persistence (see Phase 1 regression).
- Camera/Lens/ISO field-scoped filters + real sensor ISO.
- 4 user-visible mojibake strings fixed.
- Filter/search/model rebuild preserves ready thumbnails for paths that remain
  in the view (flicker reduction); stale paths drop their pixmaps.

## 3. Verification evidence

### Local gate (this machine, Release)
`.\build.ps1 Test` → **73/73 passed, 0 failed** (was 71 before this phase):
- New: `browse_convergence_tests`, `browse_convergence_ui_tests` (73 total).
- Hard gates green: `golden_image`, `bench_smoke` (93 s), `bench_enforce`
  (216 s, ±10% budget vs committed baseline — no performance regression),
  `version_consistency`, `workflow_ux_tests` (incl. the new close-while-Browse
  regression), `updatechecker_tests`, the four M24 acceptance suites.

### S1–S9 + T1–T4 Release soak (both scheduler configs)

| Scenario | Default | 1-thread | Result |
| --- | ---: | ---: | --- |
| S1 10K first entries | 727 ms | 729 ms | Pass |
| S2 rapid switch worst | 396 ms | 396 ms | Pass |
| S3 gallery max UI gap | 37.0 ms | 29.9 ms | Pass |
| S3b List max UI gap | 34.3 ms | 33.4 ms | Pass |
| S3c zero-viewport max UI gap | 21.8 ms | 35.6 ms | Pass |
| S4 24 MP JPEG | 921 ms | — | Pass |
| S5 4K TIFF | 404 ms | — | Pass |
| S8 Compare cycles | 50/50 | 50/50 | Pass |
| S9 workspace round trips | 500/500 | 500/500 | Pass |
| **T1 size churn total / worst stall / stale thumbs / RSS growth** | 847 ms / 37.2 ms / **0** / +6.3 MB | 816 ms / 33.9 ms / **0** / +2.4 MB | Pass |
| **T2 switch worst first-entries / full / stale rows** | 122 ms / 42 ms / **0** | 128 ms / 46 ms / **0** | Pass |
| **T3 RAW-only gallery == FileSystem / mixed == FS** | 300/300 / 300/300 | 300/300 / 300/300 | Pass |
| **T4 metadata work total / worst UI stall** | 10.9 s / 28.3 ms | 10.9 s / 28.1 ms | Pass |
| T4 camera 'sony' / lens 'LENS1' / ISO 800 / recursive 'target' | 500 / 143 / 100 / 50 | 500 / 143 / 100 / 50 | Pass |

T1: zero stale-sized ready thumbnails under continuous 64→240→180→140 churn
with decodes in flight. T2: no stale rows from old directories while Details
dimension scans and metadata indexing run in the background. T3: gallery
count == FileSystem count for RAW-only and mixed directories. T4: heavy
metadata workload holds the worst UI gap ≈ 28 ms (metadata I/O fully off the
UI thread), and the sort/filter/search counts match the synthetic corpus.

Results JSON: `docs/review/M25_STRESS_RESULTS_2026-08-09-rc.json` and
`..._1THREAD_2026-08-09-rc.json`.

## 4. Exit criteria checklist (automated portion)

1. Thumbnail cache identity distinguishes size/schema; 64→240 never reuses
   stale/blurry entries — ✅ (`browse_convergence_ui_tests`, T1).
2. Worker path is QImage-only; UI thread performs no thumbnail disk I/O — ✅
   (code contract + T1/T4 stall measurements; probe removed).
3. Directory change truly cancels/terminates obsolete thumbnail/dimension/
   metadata work — ✅ (pipeline generation + indexer cancellation + T2).
4. One Browse SSOT for shipped formats; RAW-only/mixed semantics identical —
   ✅ (`filesystem_tests`, `browse_convergence_ui_tests`, T3).
5. Metadata search/filter never do full-directory synchronous UI-thread I/O —
   ✅ (MetadataIndexer + T4 worst stall ≈ 28 ms).
6. Resolution/Camera/Lens sort keys computed once per file, no disk in
   comparator — ✅ (`browse_convergence_tests` counting readers).
7. Browse close persistence, Camera/Lens field correctness, garbled strings —
   ✅ (workflow_ux_tests close-in-browse, panel filter tests, T4 counts).
8. `.\build.ps1 Test` green — ✅ 73/73.
9. S1–S9 no regression; new T1–T4 stress reproducible — ✅ (this doc + JSON).
10. Docs reflect real state — ✅ (this document, CHANGELOG, STATUS, roadmap).
11. NOT marking M25 fully RC-signed — see below.

## 5. Remaining sign-off (cannot be automated here)

Per the milestone rules, the following remain explicitly
`AUTOMATED RC READY — TARGET HARDWARE / HUMAN UX SIGN-OFF PENDING`:

- **Human UX (UX Review Agent):** perceived smoothness of wheel/keyboard
  switching on 1000+ image folders; flicker on thumbnail-size changes and
  filter/search rebuilds; zoom feel; long-session (30 min+) behavior and
  memory trend on real hardware.
- **Target hardware:** the T1–T4 and S1–S9 numbers above were produced on the
  development machine (Release build). Re-run
  `build_msvc\bin\mviewer_m24_soak.exe --out results.json` on the target
  hardware and compare S1–S9 + T1–T4 with the committed baselines
  (`M25_STRESS_RESULTS_2026-08-09*.json`).
- **Beta checklist:** all automatable items in `docs/beta_checklist.md` are
  covered by the gate; the remaining perception items need the UX Review
  Agent's signature block.
