# STATUS — MViewer

> Snapshot: 2026-09-05 · Version: **1.0.19 product-workflow correctness patch** · Release tag: **not yet published**
> Single source of truth for "what the product is right now". For plans, see
> `docs/roadmap.md` (engineering) and `docs/ROADMAP_PUBLIC.md` (public).
> Evidence for the claims below: `docs/review/M24_BASELINE_2026-08-05.md`,
> `docs/review/M24_TEST_CREDIBILITY_2026-08-05.md`,
> `docs/review/M24_PERFORMANCE_2026-08-05.md`,
> `docs/review/M24_FINAL_VERDICT_2026-08-05.md`,
> `docs/review/M35_COMPARE_BROWSE_CONVERGENCE_2026-08-13.md`,
> `docs/review/M36_BROWSE_HOTPATH_DISPLAY_FIDELITY_2026-08-13.md`,
> `docs/review/M38_VIEWER_RENDER_CONVERGENCE_2026-08-14.md`,
> `docs/review/M39_REALWORLD_RELIABILITY_2026-08-14.md`,
> `docs/review/M40_INTERACTION_CANCELLATION_CLOSURE_2026-08-15.md`,
> `docs/review/M41_STORAGE_RUNTIME_TEST_CREDIBILITY_CLOSURE_2026-08-15.md`,
> `docs/review/M42_REALWORLD_RESOURCE_BOUNDEDNESS_2026-08-15.md`,
> `docs/review/M43_COMPARE_ANALYSIS_FIDELITY_2026-08-15.md`,
> `docs/review/M44_RELEASE_READINESS_STRUCTURAL_CONVERGENCE_2026-08-15.md`,
> `docs/review/M45_NATIVE_WINDOWS_PRODUCT_QUALIFICATION_2026-08-16.md`,
> `docs/review/M46_RELIABILITY_SOAK_2026-08-17.md`,
> `docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md`,
> `docs/review/M47_PHASE0_BASELINE_2026-08-17.md`,
> `docs/review/M47_SOURCE_BACKED_DISPLAY_2026-08-17.md`,
> `docs/review/M49_WINDOWS_UX_UNICODE_COMPARE_CLOSURE_2026-08-26.md` and
> `docs/review/M50_RELEASE_CANDIDATE_CONVERGENCE_2026-08-27.md`,
> `docs/review/M51_NATIVE_LAUNCH_RELEASE_CONTRACT_WINDOWS_QUALIFICATION_2026-08-28.md` and
> `docs/review/M52_RELEASE_QUALITY_WORKFLOW_CONVERGENCE_2026-08-28.md` and
> `docs/review/M53_PHASE0_BASELINE_2026-08-28.md` and
> `docs/review/M53_LARGE_SOURCE_NATIVE_RELEASE_CLOSURE_2026-08-29.md`,
> `docs/review/M54_PERCEIVED_PERFORMANCE_BASELINE_2026-08-29.md`,
> `docs/review/M54_INSTANT_BROWSE_FINAL_2026-08-29.md`,
> `docs/review/M55_PHASE0_BASELINE_2026-08-29.md`,
> `docs/review/M55_INTERACTIVE_LATENCY_MEMORY_NAVIGATION_CLOSURE_2026-08-29.md` and
> `docs/review/M57_PHASE0_MULTIFRAME_BASELINE_2026-08-29.md`,
> `docs/review/M57_MULTIFRAME_MULTIPAGE_CLOSURE_2026-08-29.md`, and
> `docs/review/M58_PHASE0_LARGE_DIRECTORY_QUERY_BASELINE_2026-09-01.md`,
> `docs/review/M58_LARGE_DIRECTORY_QUERY_CLOSURE_2026-09-01.md`,
> `docs/review/M59_PHASE0_COLOR_METADATA_BASELINE_2026-09-01.md`,
> `docs/review/M59_COLOR_MANAGED_PRESENTATION_CLOSURE_2026-09-02.md`,
> `docs/review/M60_LINKED_ROI_PHASE0_LINKED_ROI_2026-09-02.md`,
> `docs/review/M60_LINKED_ROI_CLOSURE_2026-09-02.md`,
> `docs/review/M61_PHASE0_ROI_WORKFLOW_BASELINE_2026-09-02.md`,
> `docs/review/M61_PROFESSIONAL_LINKED_ROI_CLOSURE_2026-09-02.md`, and
> `.\build.ps1 Test`.

## 1.0.19 — Browse/Compare/ROI correctness patch (2026-09-05)

- Viewer metadata overlay is `I` / `M` / 图片信息 only. Left-click and hover no
  longer cover the image and steal pan or Fit↔100% double-click.
- Compare toolbar/menu/`C` require a 2–8 image selection, matching 比较选中.
  Folder-first-8 fallback is gone. Space uses the current 2–8 selection when
  present, otherwise current+next.
- Linked ROI geometry stays in original source pixels through crop/rotation
  display projection. Canvas empty-release clears the engine selection.
- Rename cannot escape the source directory. M58/compare-session/project
  Release gates use live `CHECK` counters.

## M61 — Professional linked ROI workflow convergence (2026-09-02)

- One canonical half-open source-coordinate ROI is visible and editable in
  Grid, Split, Overlay, Swipe, and Checkerboard. Right-drag creates; dragging
  inside moves; four edges and four corners resize with clipping and no
  Canvas/Grid geometry drift.
- ROI measurement has explicit `Idle`, `Measuring`, `Ready`, `Unsupported`,
  `Failed`, and `Backpressured` states. Row-level cancellation bounds stale
  scan work, and submit rejection is visible instead of leaving a false busy
  state.
- Exact-source measurement requires a decoder-declared bounded region path.
  JPEG and Windows unrotated TIFF qualify; PNG/BMP/unproven plugin paths report
  the reason per pane. Results remain source RGB at the honest 8-bit analysis
  boundary and are independent of display ICC conversion and Compare edits.
- A compact clickable HUD preserves current results when Analysis is hidden;
  the full table adds stable columns, tooltips, numeric alignment, and TSV copy.
  Focused real UI and 24/60/100 MP benchmark evidence is green; two consecutive
  source-stable Release `build.ps1 Test` runs passed **131/131** in 814.15 s and
  795.36 s. Full evidence is recorded in the M61 closure review.

## M60 — Linked ROI measurement (2026-09-02)

- Compare now treats a ROI as a canonical half-open rectangle in source
  coordinates. Equal-dimension panes mirror the geometry live during a drag;
  unequal dimensions retain only a local non-measuring ROI and explain why
  linked measurement is unavailable.
- The Analysis panel exposes **ROI Measurement — Source RGB** with per-image
  means, R/G and B/G ratios, pixel counts, and a B−A delta for exactly two
  images. Results are latest-generation asynchronous work, so stale or
  cancelled jobs cannot overwrite a newer ROI.
- Statistics are computed from source/analysis pixels rather than ICC-converted
  display LODs. Source-backed panes use bounded region decode where the source
  provider supports it; high-bit input remains explicitly limited by the
  existing 8-bit `ImageData` boundary.
- Focused geometry/format/ratio/analyzer/large-region coverage is gated by
  `m60_linked_roi_tests`; Compare acceptance also covers mode persistence and
  histogram consistency. Two source-stable Release `build.ps1 Test` runs passed
  **129/129** in 820.73 s and 822.00 s; the GitHub Actions packaging result is
  recorded in the M60 closure review.

## M59 — Color-managed presentation contract (2026-09-02)

- Source ICC, EXIF orientation, channel count, and bits-per-channel now use
  one shared Qt-internal semantics helper across ordinary, LOD/region, probe,
  and FrameSequenceReader paths. Untagged and malformed profiles have an
  explicit sRGB fallback.
- `DisplayColorContext` makes the target monitor profile, fingerprint, and
  generation explicit. Windows discovery uses the current `QWindow` device
  context; Viewer and Compare cancel stale display work and invalidate only
  presentation caches when the target changes.
- CPU and optional GPU paths share the same source-to-target presentation
  conversion. Source/analysis bytes remain immutable and Pixel Inspector stays
  source-backed. The focused M59 gates cover metadata truth, CPU parity,
  malformed ICC, 16-bit probe, static/sequence parity, and Browse latest-wins
  behavior at 10k/50k plus a real 660-file panel.
- Physical monitor LUT/HDR/mixed-DPI and long-session native GUI behavior are
  intentionally **MANUAL/BLOCKED** in this environment.
- Final qualification: two consecutive source-stable Release `build.ps1 Test`
  gates passed **128/128** (742.80 s and 743.22 s), including the M59 focused
  tests, workflow/architecture/complexity gates, golden-image, and benchmark
  enforcement.

## M58 — Large-directory query and search responsiveness (2026-09-01)

- Browse filter state is represented as a Qt-free value query with one
  generation per logical intent. Text input uses a real coalescing debounce;
  background filtering/sorting consumes immutable entry/store snapshots and
  drops stale generations before touching the model.
- Sort and type filters now rebuild from the committed in-memory directory
  listing; changing them never calls `setDirectory()` or rescans the folder.
  SearchPanel uses an immutable SearchEngine snapshot, cancellable latest-wins
  evaluation, bounded table population, and an explicit `reindexRequested()`
  host contract.
- MetadataIndexer batches 256 entries per GUI callback. RatingStore and
  TagStore expose single-copy snapshots for filter workers.
- The deterministic 10k/50k M58 acceptance executable is part of CTest. Two
  source-stable Release `build.ps1 Test` runs passed 125/125 (762.13 s and
  761.78 s). Native first-paint/typing cadence and extended GUI feel remain
  **MANUAL/BLOCKED** in this environment; see the Phase-0 baseline and closure
  report for the exact boundary.

## M57 — Multi-frame / multi-page image architecture and playback (2026-08-29)

- Post-closure Viewer polish gives each frame/page a distinct render identity, invalidates stale
  derived rendering work on transitions, preserves the viewport, and shows frame/page state inside
  the canvas so fullscreen playback does not depend on the hidden window title.
- Multi-page TIFF context menus now use document-specific First/Previous/Next page actions, disable
  impossible boundary moves, and omit animation-only playback controls.
- Viewer file-navigation actions now mirror the Browse sequence position, with unavailable Previous/
  Next directions disabled instead of silently doing nothing.
- Viewer image-dependent context actions now disable themselves until a current image is available;
  full-screen remains available for an empty Viewer.
- Browse menu and toolbar commands now follow their real prerequisites: Image Information requires
  a current image, while Compare requires at least two available images and gives keyboard users a
  clear status message when that boundary is not met.
- Viewer zoom menu actions now follow the Viewer window lifecycle: they are enabled on visible
  presentation and disabled again after closing, while Browse selection remains intact.
- Slideshow menu state now follows Browse prerequisites: it remains disabled until a current image
  and directory are available, while keyboard invocation retains a clear status message.
- Browse navigation actions now reflect their actual boundaries: image/directory history, Up,
  Refresh, and Add/Remove Favorite are disabled when no corresponding operation is available,
  while restored image history and favorite commands refresh their state immediately.
- `FrameSequenceReader` provides one core capability for static images,
  animated GIF/WebP, and multi-page TIFF: bounded probe, explicit frame/page
  decode, scaled decode, per-frame duration, and source dimensions. Qt reader
  details stay in the core implementation TU; the Viewer does not open an
  image reader directly.
- `FrameIdentity` combines the M56 file revision with frame/page index and
  decode variant. Repository, Viewer, Compare, metadata, persistence, and
  export now carry the selected frame rather than relying on path-only state.
- GIF/WebP play in the Viewer with monotonic-timeline scheduling, worker-side
  decode, latest-generation cancellation, and two bounded neighbor prefetches.
  TIFF pages use the same navigation surface but remain paused and are never
  materialized as gallery files. Zoom/pan is retained across frame changes.
- Preview and thumbnails intentionally use frame/page zero and do not animate;
  thumbnails show compact `Nf`/`Np` badges. Compare is explicitly
  frame/page-addressed per pane, and workspace/project restore persists the
  current frame plus animated playing state. Legacy workspace schemas default
  to frame zero and paused.
- M57's deterministic real-plugin test covers qgif, qwebp, qtiff, repository
  cache separation, Compare/session round-trip, playback catch-up, workspace
  persistence, and legacy defaults. The focused real-plugin test and the two
  required source-stable Release Test rounds are green: `build.ps1 Test`
  passed **124/124** in 755.11 s and 753.71 s, with zero architecture
  violations and zero complexity hard failures.
- Physical native GUI feel, clean target-machine package launch, hostile
  disposal/dirty-rectangle corpus, 100MP multi-page TIFF RSS, and extended
  playback soak remain **MANUAL/BLOCKED** in this environment.

## M55 — Interactive latency, memory and navigation closure (2026-08-29)

- The UI-owned thumbnail surface is now an exact `(path, size)` LRU with hard
  384-entry / 96 MiB bounds, and thumbnail results invalidate only contiguous
  row spans instead of republishing the entire model on every decode.
- Thumbnail demand retains the visible+predictive intersection during small
  scrolls. Unique enqueue owners close the same-generation ABA hole, so a late
  cancelled decode cannot erase a replacement request.
- Scan probes use one immutable callback snapshot and progressive sorting no
  longer allocates `std::string` values in every comparator call. On-disk
  thumbnail bootstrap is asynchronous for exact get/put hits; maintenance APIs
  still provide complete indexed accounting and cap enforcement.
- LOD-first Viewer navigation prewarms at most two source-backed 1024-edge
  display rasters under a 64 MiB cache and promotes matching work safely.
  TagStore edits use a coalescing worker while `save()` remains the explicit
  synchronous flush boundary.
- M55 adds deterministic pipeline ABA/overlap tests, a real 420-image UI cache
  test, and the standalone `m55_interactive_baseline` recorder. Native
  fullscreen sequential-next feel and extended physical GUI smoothness remain
  **MANUAL/BLOCKED** pending a real desktop session.
- Final local Release verification passed twice consecutively: 119/119 in
  749.06 s and 119/119 in 750.28 s, with zero architecture violations and zero
  complexity hard failures.
- Evidence: `docs/review/M55_PHASE0_BASELINE_2026-08-29.md` and
  `docs/review/M55_INTERACTIVE_LATENCY_MEMORY_NAVIGATION_CLOSURE_2026-08-29.md`.

## M53 — Large-source format parity & native release reality closure (2026-08-29)

- The Qt TIFF `ClipRect` claim was characterized and rejected as unsafe: the
  Qt 6.10 TIFF handler still attempted a full 100MP allocation before clipping.
  On Windows, the decoder now uses a narrow WIC frame → clip → scale → 24-bit
  conversion path. `NativeLod` is claimed only for unrotated TIFFs on Windows;
  regions remain `BoundedRasterRegion`, and PNG/BMP retain their honest
  `FullDecodeScaled` classification.
- `RawDecoder::extractPreview()` is now chunked/seek-based. It preserves the
  largest valid embedded JPEG behavior, rejects truncated candidates, and no
  longer calls `QFile::readAll()` for the RAW container. The M53 peak-buffer
  diagnostic proves storage is tied to parser windows plus the selected preview.
- Viewer and Compare now have M53 large-TIFF workflow and five-round lifecycle
  soak coverage. Compare bounded-display failures retain the last valid raster
  when possible and expose a user-facing reason on an initial blank pane.
- The deterministic corpus now includes a 16-bit TIFF, large PNG/BMP boundary
  fixtures, a large synthetic RAW preview container, and Unicode-path coverage.
- Focused M53 verification is green: 31 core parity/RAW assertions, Viewer /
  Compare UI qualification, and the five-round large-source soak. The final
  Release two-run gate, package contract, and complexity/architecture results
  are recorded in the closure review below.

## M54 — Perceived performance & instant Browse (2026-08-29)

- The real `ThumbnailPanel`/`PreviewPanel` Browse path now publishes safe
  default-sort directory batches progressively, so large directories show
  real rows before enumeration and final sort convergence complete. The M54
  recorder covers 100/1,000/10,000/50,000-image cold/warm cases and reports
  first row, first thumbnail, first-screen readiness, preview p50/p95, scan
  completion, and stability.
- Thumbnail cache payload I/O and PNG work no longer run under the global
  bookkeeping mutex; warm hits no longer update file timestamps. Session-local
  approximate recency preserves correctness without per-hit write amplification.
- Viewport changes cancel obsolete demand and resubmit the latest visible
  window. The stress benchmark measured a 112-request peak, 52 ms visible
  jump p95, and 44 obsolete decode attempts. Selected Preview uses the
  foreground Decode pool and posts visual pixels before optional statistics.
- M54 focused coverage passed 23/23, and two consecutive source-stable full
  `build.ps1 Test` gates passed 118/118 in 779.82 s and 782.14 s, with zero
  architecture violations and zero complexity hard failures. Evidence is
  recorded in `docs/review/M54_INSTANT_BROWSE_FINAL_2026-08-29.md`. Existing
  M47 Viewer/Compare LOD behavior remains the large-source display contract;
  new display-raster neighbor preload and physical perceived-UX qualification
  remain explicit follow-up scope.

## M52 — Release quality truthfulness & workflow boundary convergence (2026-08-28)

- PowerShell 5.1 quality generators now resolve the repository from the launch
  directory, emit explicit UTF-8 without a BOM, and keep generated runtime
  text ASCII-safe. A regression invokes the real Windows PowerShell entry
  points, validates strict UTF-8 round trips, and rejects common mojibake.
- Complexity reporting no longer grants split responsibility TUs a broad
  1500/2500-line exemption. MainWindow/CompareWorkspace/ThumbnailPanel
  responsibility files use a measured 800-line warning and 1000-line hard
  ceiling; production functions over 120 lines or CC over 25 are hard failures.
  The regression proves an oversized TU fails and a legal split passes.
- Compare display materialization now delegates its source-backed planning to
  a pure value-based planner. Table-driven coverage includes placeholder/fit,
  independent and uniform scales, zoom/pan edges, DPR 1/1.25/1.5/2, crop and
  rotation, resize, large JPEGs, duplicate panes, and A→B→A replacement.
- MainWindow session update/crash notifications live in a dedicated
  responsibility TU. The real M50 workflow regression now exercises rapid
  A/B/C navigation, a disappearing source, invalid external targets, async
  Compare replacement, ROI retention, and all scheduler-pool drains.
- The release evidence distinguishes **gate PASS** (zero hard failures and
  green automated checks) from **accepted baseline debt** (advisory warnings).
  Native DPI/ICC/UNC/installer UX and extended physical GUI soak remain
  explicitly **MANUAL/BLOCKED** unless run on the target machine.
- Final local qualification passed **114/114 CTest tests twice consecutively**
  on the source-stable tree (694.59 s and 690.70 s); `m51_rc_soak` passed in
  both runs. `build_health` is 92.7 / grade A with 0 complexity hard failures
  and 107 visible advisory warnings.
- Evidence: `docs/review/M52_PHASE0_BASELINE_2026-08-28.md` and
  `docs/review/M52_RELEASE_QUALITY_WORKFLOW_CONVERGENCE_2026-08-28.md`.

## M51 — Native launch, release contract & Windows qualification closure (2026-08-28)

- Command line, shell associations, and drag-and-drop share one external-open
  classifier for images, directories, `.mvws`, and `.mvproj`. Missing,
  unsupported, and mixed targets are rejected atomically.
- Strict RC packaging validates Qt/SQL/runtime dependencies, excludes
  development payloads, runs packaged `--selftest`, checks installer PE
  version identity, and verifies SHA256 for both shipping artifacts.
- Windows crash reporting is always on under AppData. The SEH path is
  pre-initialized, fixed-buffer/Win32-only, recursion-guarded, and covered by
  a real child-process crash smoke test.
- Automated evidence is recorded in
  `docs/review/M51_NATIVE_LAUNCH_RELEASE_CONTRACT_WINDOWS_QUALIFICATION_2026-08-28.md`.
  Physical target-machine DPI/UNC/install-uninstall/long-path and extended
  GUI soak rows remain explicitly **MANUAL/BLOCKED** unless run on that target.

## M50 — Release candidate workflow convergence (2026-08-27)

- Directory transitions now have one committed `directoryChanged` owner for
  gallery/model/history/recents/status/reindex and Sidecar scheduling. Sidecar
  completion is latest-wins, lifetime-safe, UI-marshalled, and re-applies the
  active rating/flag filters.
- Compare materialization, Diff, and canvas/Blink rendering are separated into
  bounded translation units; the largest is below the 800-line responsibility
  guardrail.
- Unix absolute command-line paths are accepted on Unix while Windows slash
  switches remain reserved; drive and UNC paths are covered by regression
  tests.
- Focused evidence is recorded in
  `docs/review/M50_RELEASE_CANDIDATE_CONVERGENCE_2026-08-27.md`. Physical
  target-machine Windows/DPI/UNC/installer qualification remains
  **MANUAL/BLOCKED**, never an automated PASS.

## M49 — Windows UX / Unicode / Compare reliability closure (2026-08-27)

- Unicode filesystem boundaries now use one UTF-8/native-path adapter across
  persistence, sidecars, batch/export, image probing, plugins, and startup;
  expected filesystem failures are contained at their owning boundary.
- Browse selection has one gallery publication owner for plain/Ctrl/Shift
  gestures, and sidecar import is cancellable background work so first-screen
  browsing does not wait for a directory-wide import.
- Compare now has strict equal-width default panes, elided captions with full
  tooltips, a columns-driven `R rows × C cols` indicator, and a display-only
  momentary A←B override shared by the button and Space key. Continuous Blink
  remains independent.
- Automated evidence and the native qualification boundary are recorded in
  `docs/review/M49_WINDOWS_UX_UNICODE_COMPARE_CLOSURE_2026-08-26.md`.

## M47 — Production-scale data path hardening (historical phase record)

- **Phase 0 complete — baseline, contract & failure reproduction:**
  - **Reproduction (measured):** 100 MP JPEG/TIFF **cannot be opened at all**
    by the current Viewer/Repository path — Qt 6.10's 256 MB QImage allocation
    limit rejects the full decode (`decodeFull` returns null). The 100 MP JPEG
    *scaled* decode (256 px) works in ~112 ms; the 100 MP TIFF scaled decode
    also fails.
  - **Measured:** 24 MP JPEG open = 811–978 ms decode; viewer first usable
    frame 1020–1030 ms with a full 6000×4000 frame retained (+243.6 MB RSS
    while held); every fit/100%/zoom repaint stalls the UI thread 126–164 ms
    (full-frame scaling on the paint path); repeated open/close ~1 s each with
    cache retention.
  - **Audit:** every current display path (Viewer `TileGrid` crop, Compare
    per-pane display LOD) is a client-side operation over an already
    materialized full-resolution RGB frame. `loadPixels()` always calls
    `decodeFull`.
  - **Contract:** `docs/rfc/M47_SOURCE_BACKED_DISPLAY.md` fixes that display
    representation != analysis source, with an additive capability model that
    does not touch the frozen DecoderRegistry/plugin ABI.
  - **Tooling:** deterministic large-image corpus
    (`testdata/generate_large_fixtures.py`, CTest `large_fixture_gate`),
    baseline recorder (`benchmarks/m47_large_image_baseline_main.cpp`, CTest
    `m47_large_image_baseline`), evidence
    `docs/review/M47_PHASE0_BASELINE_2026-08-17.md`.
  - Next phases: source-backed image abstraction → Viewer/Compare LOD/tile
    pipelines → exact-source consumers → transactional Workspace/Project
    restore → large-image gates → qualification.

## M47 — Production-scale data path hardening (historical phase record)

- **Phase 1 complete — source-backed image abstraction** (see below).
- **Phase 2 complete — Viewer LOD-first display pipeline:**
- Large sources display through a bounded raster over `SourceImage`:
    viewport LOD while zoomed out (`NativeLod` for JPEG), visible-region
    rasters while zoomed in (`BoundedRasterRegion`) — progressive,
    generation-cancelled, lifetime-safe, one in-flight request with debounced
    view-churn upgrades, and NO UI-thread file I/O / decode / full-frame
    scaling.
- The Phase-0 reproduction is fixed: **a 100 MP JPEG now opens and
    displays** (displayReady with the full source dims, bounded raster,
    display-path RSS < ~100 MB vs ~286 MB full materialization), while the
    full frame stays the analysis/Inspector source only (loaded when
    feasible, skipped when > Qt's 256 MB allocation limit — Phase 4 adds
    explicit materialization).
- Small images keep the existing fast path; zoom/pan/fit stay fully
    interactive in LOD mode (`hasDisplayImage`); M46 preload-promotion and
    rejection contracts preserved (raster work rides the Thumbnail pool).
- New CTest `m47_viewer_lod_tests` (100 MP open, zoom-in regions, A→B→A,
    destroy mid-request/post-display, idle convergence, small-image fast
    path). Full gate **100/100 green**.

## M47 Phase 1 — source-backed image abstraction (2026-08-17)
- New Qt-free capability interface `ISourceImageCapabilities` (probe without
    pixel decode, native-LOD, bounded region decode) discovered by
    `dynamic_cast` — the frozen `DecoderRegistry`/`IDecoder`/plugin ABI is
    untouched; non-capability decoders fall back cleanly.
- `SourceImage` (`core/image/SourceImage.{h,cpp}`) represents a file as
    metadata + capability handle; opening never decodes pixels. Every
    operation records its RFC classification (`NativeLod` / `BoundedRasterRegion`
    / `FullDecodeScaled` / `FullDecodeCrop` / `ProbeMetadata`) plus raw
    full-decode counts in `SourceDecodeStats` — tests can prove which path ran.
- `QtDecoder` implements the interface: JPEG advertises `NativeLod`
    (evidence-based from Phase 0), TIFF honestly does not; region decode uses
    the bounded-memory clipRect path classified `BoundedRasterRegion`, never
    claimed as native.
- Measured (test-verified): 100 MP JPEG probes without decoding, LOD(256)
    succeeds as NativeLod with `fullDecode==0`; a 512×512 region of the 100 MP
    JPEG succeeds (bounded memory) where full decode is rejected by Qt's
    256 MB limit; TIFF LOD fails today with the honest fallback classification;
    clip-path region is pixel-identical to the full-decode crop; EXIF-oriented
    probe/LOD report the rotated geometry.
- New CTest `m47_source_tests` (8 groups); full gate 99/99 green.
- Next: Phase 2 Viewer large-image pipeline (viewport LOD/tile over
    `SourceImage`).

## M47 Phases 3–6 complete (2026-08-17) — Compare source-backed display, exact-source consumers, transactional restore, soak & benchmark evidence

- **Phase 3 — Compare source-backed display:** infeasible sources (> 60 MP,
  full RGB beyond Qt's 256 MB limit) skip the full load; a metadata-only
  placeholder frame keeps the requested pane index in the engine, so the grid
  keeps every requested pane and skips never count as load failures (duplicate
  paths accounted per request). Panes display through bounded per-pane
  viewport LOD rasters over `SourceImage` (native LOD for JPEG, honest blank
  fallback for TIFF); wheel-zoom re-materializes at a denser edge with no full
  decode. New CTest `m47_compare_lod_tests` (6 groups); gate 101/101.
- **Phase 4 — exact-source consumers:** with source-backed panes present, the
  Pixel Inspector reports the exact full-resolution pixel for feasible panes
  and 无效 (never the display LOD) for infeasible ones; diff/PSNR/SSIM on an
  all-infeasible pair degrade to "—" with zero decodes; report export records
  placeholder pairs non-comparable in the requested pane order without
  materializing the infeasible source. New CTest `m47_exact_source_tests`
  (3 groups); gate 102/102.
- **Phase 5 — transactional async Workspace/Project restore:** `.mvws` /
  `.mvproj` opens read + deserialize on a background worker; the UI applies the
  parsed document atomically for the current generation only. A newer open
  supersedes an in-flight one (stale deliveries dropped), missing/corrupt
  files report without touching the live session, and closing mid-restore is
  safe. Dialog-free `openWorkspaceFile`/`openProjectFile` are public; the
  duplicated inline project restore is removed. New CTest `m47_restore_tests`
  (5 groups); gate 103/103.
- **Phase 6 — deterministic soak & benchmark evidence:** `m47_large_soak`
  (6 rounds of Viewer/Compare 100 MP display churn: open → zoom → A→B→A
  supersede → destroy mid-request, per-round pool drain, zero full-decode
  fallbacks, bounded + plateau-stable RSS, converged scheduler graph). The
  baseline recorder gained the post-Phase-2 100 MP viewer display section and a
  Compare source-backed section. Measured: **100 MP JPEG opens to display in
  ~0.5 s with +53 MB RSS (Phase 0: cannot open at all); a 100 MP compare pair
  displays both panes with +82 MB and zero full decodes**. Gate 104/104.
- Remaining: Phase 7 release-evidence convergence (this file, roadmap,
  test matrix, review docs) and Phase 8 final qualification (3 consecutive
  full-gate runs); native hardware rows stay MANUAL/BLOCKED per the M46
  qualification doc.

## M47 — COMPLETE (2026-08-17)

- **Phase 7 complete — release-evidence convergence:** the auto test matrix
  was regenerated (103 test sources incl. all M47 suites), the source-backed
  display RFC is marked implemented through Phase 6, the Phase 1–6 evidence
  review was added (`docs/review/M47_SOURCE_BACKED_DISPLAY_2026-08-17.md`),
  and this file/roadmap/changelog converge on the final code.
- **Phase 8 complete — final qualification:** `.\build.ps1 Test` passed
  **104/104 three consecutive times** (runs 756.9 s / 734.9 s / 749.7 s),
  including golden-image, `bench_enforce`, workflow, Browse/Compare
  acceptance, all M47 suites, soak gates and the architecture gate (0
  violations); complexity strict gate 0 hard failures. Native hardware rows
  (physical ICC, mixed-DPI, long-session GUI) remain **MANUAL / BLOCKED** —
  see `docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md`.

## M46 — Real-world workflow reliability & long-session release qualification (2026-08-17)

- **Async lifetime contract:** ImageRepository now enforces a strict
  cancellation/delivery contract — consumer `AsyncLifetimeToken` (Qt-free,
  invalidated in the owner destructor), a delivery gate that makes
  `cancelAsync()` wait for an already-started delivery (so after cancel
  returns no callback is running or will start), and no raw-`this` captures in
  the ImageViewer completion path. Deterministic latch/barrier tests cover
  decode-done vs destroy, cancel during terminal delivery, re-entrant cancel,
  A→B→A, queued/running cancel and 300-cycle churn (`m46_repository_tests`).
- **Browse latest-intent priority:** Details/Thumbnail delegates paint
  EXCLUSIVELY from scan-cached Entry data (no `QFileInfo` size/mtime in any
  paint path — proven by pixel-identical renders after the source files are
  deleted). Directory scans, dimension probes and recursive walks stop
  cooperatively on supersession (shared generation token; bounded-iteration
  probes), the busy cursor is ref-counted UI-side (balanced even when queued
  scans are dropped by destruction), and `ThumbnailPipeline::setThumbSize()`
  makes a size change a real supersession boundary (old-size in-flight work is
  cancelled, not decoded to completion).
- **Persistence crash safety:** new Qt-free `AtomicFile` helper
  (temp → flush/close → atomic replace on Windows via MoveFileExW); failed
  writes leave the previous official file byte-identical. RatingStore edits
  coalesce on its owned worker with an explicit `save()` flush boundary and a
  retry-on-failure policy; TagStore/SidecarStore use atomic replace. Fault
  injection covers temp-create/write/replace failures, stale temps, rapid
  updates and the shutdown flush (`m46_persistence_tests`).
- **Workflow soak:** `mviewer_m46_workflow_soak` drives the real product loop
  (Browse → scroll/size churn → Viewer → next/prev → zoom → Compare → pair
  change → Analyze → Export/cancel → directory switch) with
  warm-up → steady → idle-convergence verdicts: pools/dependency graph/
  pipeline → 0, cache obeys caps, RSS ≤ max(128 MiB, 15%), handles ≤ +64, no
  temp growth. Short form is a CTest; `--extended` is the manual Release form.
- **Architecture gate:** the stale imageviewer/preview loading-boundary
  exemption is removed; R2 now covers the Compare layer too; Preview/Compare
  load through the Application/Core facades. The gate's 0 warnings is backed
  by a regression test that plants violations and checks the real tree
  (`architecture_gate_regression`). Qt claims reconciled in README/docs
  (min 6.8 CI / dev-verified 6.10.3).
- **Manual sign-off:** interactive GUI, physical ICC, mixed-DPI, two-volume,
  native dialogs, long-path/special-filename and multi-hour soak remain
  **MANUAL / BLOCKED** — see `docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md`.

## M45 — Native Windows qualification (2026-08-16)

- **Automated:** CommandStack callback re-entry is watchdog-safe; callbacks
  run outside the state mutex, file transfers report progress/cancellation,
  and PNG clipboard encoding is off the UI key handler with generation and
  temporary-file guards. Core regression: **54/54**; callback probe: pass;
  the full Release gate passed **90/90**. Architecture gate: **0 warnings**;
  complexity strict gate: **0 hard failures**.
- **Production UI:** MainWindow Undo/Redo registration, rename/undo/redo,
  and asynchronous clipboard paste are covered by the isolated UI acceptance.
- **Delete semantics:** the current implementation is explicitly **MViewer
  回收站**, a per-user reversible staging directory. Native Windows Recycle Bin
  integration is not claimed.
- **Manual sign-off:** Native Release GUI, physical ICC, mixed DPI, two-volume
  behavior, native dialogs, and long-session observations remain **MANUAL /
  BLOCKED** until run on the target Windows desktop hardware.

## Positioning

A **visual analysis platform for image algorithm engineers** — compare, validate,
and analyze image-processing outputs (camera ISP, CV pipelines, SDK versions).
The core workflow is **compare → analyze**; browsing is only the entry point.
This is **not** a general-purpose image viewer.

## Version (single source)

- `CMakeLists.txt` `project(VERSION)` is the **only** version source (M24).
- Consumers of the same source: About dialog, UpdateChecker, workspace
  `appVersion`, session-start log banner, `app.setApplicationVersion()`,
  `build_msvc/version_info.txt` → portable ZIP / installer naming.
- Generated at configure time: `build/generated/MViewerVersion.h`
  (`MVIEWER_VERSION_STRING`, `MVIEWER_VERSION_FULL`).
- Gate: CTest `version_consistency` fails if code hard-codes a version literal,
  or STATUS/installer/package scripts disagree with CMake.
- Release process: bump `project(VERSION)` → tag `vX.Y.Z` → `release.yml` builds,
  packages, and attaches artifacts; local equivalent `scripts/package_release.ps1`.

## Architecture (frozen)

```
UI (Qt Widgets) → Application (UseCases) → Core → Domain
```

- **Domain**: zero-dependency `std` types; no Qt.
- **Core**: Qt-free headers; `mviewer_core` is a **SHARED** library (one vtable
  shared host↔plugin, required by the Plugin ABI). `.cpp` internals may use Qt.
- **UI**: Qt 6 Widgets boundary only (no D3D11/Vulkan direct compositing).
- **Build**: `build.ps1` (CMake + Ninja, MSVC 2022). Never invoke
  cmake/ninja/cl directly. Qt 6.10.x / MSVC 19.44 verified on the dev machine.

## Shipped capabilities (1.0.x, CTest-verified)

### M44 Release readiness, data safety & structural convergence (2026-08-15)

- File rename/move/delete commands now have collision preflight, verified
  cross-volume fallback, injectable fault handling, explicit rollback state,
  and Unicode-path regression coverage. CommandStack preserves unresolved
  disk state instead of reporting a misleading clean failure.
- User-triggered report, workspace, project, batch-analysis, Copy, and Save
  persistence paths use the cancellable worker-side/atomic ExportJob contract
  with generation and lifetime guards; the obsolete ThumbnailPanel sync
  export path is gone.
- Preview/viewer loading is behind thin Application/Core facades, reducing UI
  knowledge of CacheManager and ImageRepository policy. Architecture warnings
  are **0**.
- Production and acceptance-suite responsibility splits bring
  `complexity_gate.ps1 -Strict` to **0 hard failures** (92 advisory warnings).
- Final Release build and full CTest are green: **88/88**, including golden,
  benchmark-enforcement, workflow, Browse/Compare acceptance, and async
  lifetime gates. Health score is **92.7 / A**.
- Native Release GUI, physical ICC, mixed-DPI, long-session, and real
  multi-volume validation remain **MANUAL PENDING**; see the full evidence in
  `docs/review/M44_RELEASE_READINESS_STRUCTURAL_CONVERGENCE_2026-08-15.md`.

### M42 Real-world resource boundedness & large-image Compare closure (2026-08-15)

- Raw16 Pixel Inspector buffers are byte-bounded, included in deterministic
  memory accounting, and cleared by all cache lifecycle operations.
- Compare Fit-state panes use viewport-sized display LODs while full-resolution
  ImageFrames remain the source for analysis, diff, ROI, and Pixel Inspector.
- RAW preview scanning uses an immutable span over `QByteArray` without a
  second full-file vector copy.
- Focused and full-gate evidence is recorded in
  `docs/review/M42_REALWORLD_RESOURCE_BOUNDEDNESS_2026-08-15.md`.

### M43 Compare analysis fidelity & pixel-accurate inspection closure (2026-08-15)

- Pixel Inspector and 1×1/3×3/5×5/7×7 neighborhoods now read exact source
  `ImageFrame::pixels()` samples rather than bounded display LOD pixels.
- Crop/rotation inverse mapping and brightness/contrast/gamma/white-balance
  semantics are centralized in the Qt-free `PixelInspector` sampler; ICC
  remains display-only, and identity RAW16 reads remain exact.
- Adversarial high-frequency source-vs-LOD Compare acceptance coverage and
  source-backed core tests are green. Canonical CTest run #1 passed 88/88;
  runs #2 and #3 also passed 88/88; three-run evidence is maintained in the
  M43 review report.
- Manual Windows mixed-DPI, physical ICC and long-session UX review remains
  **PENDING** in this offscreen environment.
- Evidence: `docs/review/M43_COMPARE_ANALYSIS_FIDELITY_2026-08-15.md`.

### M41 Storage / runtime / test credibility closure (2026-08-15)

- DiskCache now uses unique per-thread SQLite connections, exact blob-byte
  accounting, enforced entry/byte limits, safe initialization failure, and
  deterministic connection teardown. `CacheConfig::diskCacheSize` is wired to
  the disk byte limit, and the concurrent cache stress test covers eight
  threads and 1,280 operations.
- Runtime storage has one writable-path contract with a per-run temporary
  fallback. QSettings is explicitly INI-backed under the runtime config path,
  and state rewrites use QSaveFile with commit checks.
- CTest assigns each registered test an isolated runtime root and relevant
  environment variables. Weak assertions in the touched test paths now observe
  real state rather than tautologies.
- The complete 88-test matrix passed **88/88 in three consecutive runs**,
  including golden-image, benchmark-enforcement, lifecycle, workflow, Browse,
  and Compare acceptance gates. Full evidence is recorded in
  `docs/review/M41_STORAGE_RUNTIME_TEST_CREDIBILITY_CLOSURE_2026-08-15.md`.

### M39 Real-world reliability & export convergence (2026-08-14)

- Scheduler-rejected tiles remain pending and retry with bounded backoff, with
  separate image generations and viewport revisions.
- ImageViewer paint consumes zero-copy tile views, while derived overlays and
  ROI statistics stay asynchronous and latest-wins guarded.
- All export modes share cancellable asynchronous ExportJob execution. CSV,
  JSON and HTML are escaped/parseable and committed atomically; directory
  enumeration is worker-side rather than UI-side.
- Fullscreen requested state is safe across repeated F/F11 transitions.
- Performance enforcement now uses an explicit CPU-aware profile: local `auto`
  adapts CPU-sensitive latency/throughput budgets on <=4 logical-core hosts,
  while nightly regression remains pinned to the `ci` profile. CTest leaves one
  logical core available and benchmark tests run serially.
- The historical M39 managed-runtime write restriction was closed by the M41
  runtime-path and hermetic-test work; the original M39 behavior and evidence
  remain documented in its review report.

### M35 Compare / Browse convergence (2026-08-13)

- Ordinary Compare presents source images without an implicit heatmap;
  PSNR/SSIM/statistics still run asynchronously, while the explicit
  `显示差异` state controls visualization.
- Compare opens fullscreen and performs one coalesced post-layout Fit. Each
  pane fits its own viewport; synchronized zoom shares the relative ratio to
  Fit, while Uniform Pixel Scale remains an explicit independent mode.
- Browse preview is two-stage: an already materialized gallery thumbnail is
  shown synchronously and a <=512 px preview replaces it atomically. Existing
  generation/cancellation/QPointer latest-wins guards remain authoritative.
- Analysis pixels remain decoded numeric values. Embedded ICC is retained as a
  frame metadata sidecar and applied only to display copies; Compare panes use
  the same display materializer.
- `build.ps1 Test` propagates the original non-zero CTest exit code; CTest
  includes `build_test_exit_gate` to prevent warning-only regressions.

### M38 ImageViewer render pipeline closure (2026-08-14)

- Viewer paint no longer materializes missing tiles synchronously. A dedicated
  async tile manager de-duplicates canonical requests, tracks image/view
  generations, drops stale results and marshals one coalesced repaint per burst.
- TileKey is stable across continuous zoom and includes a render-resolution/DPR
  policy. TileCache now tracks bytes and applies an LRU byte budget with hit,
  miss and eviction diagnostics.
- Browse double-click can present the ready gallery thumbnail immediately while
  cancellable FullImage decode and DecodePool tile scaling/ICC conversion run in
  the background. Analysis still consumes only the FullImage frame.
- Fullscreen max-fit, format-aware Copy Color, display-correct Copy/Save and
  derived overlay caching are covered by focused deterministic tests.

The full M38 evidence, including the canonical gate result and remaining
environment blockers, is recorded in `docs/review/M38_VIEWER_RENDER_CONVERGENCE_2026-08-14.md`.

### M40 Interaction cancellation & UI-thread purity closure (2026-08-15)

- Compare loading owns cancellable repository handles and exactly-once batch
  accounting; superseded and destroyed work cannot adopt frames.
- ImageViewer fullscreen has one authoritative requested-state API/property.
  Copy and Save As use worker-side ExportJob decode, display conversion and
  encoding; MainWindow no longer materializes a full image synchronously.
- ExportDialog modes share one cancellable runner with generation/lifetime
  guards. Tile retry uses one stoppable worker, and Contact/PDF staging has a
  bounded memory contract.
- Focused M40 verification remains green, including Compare cancellation,
  retry teardown and ExportJob budget cases. Its 82/88 clean-build result was
  the baseline for M41's storage/runtime closure; the follow-ups are now
  closed by the M41 evidence above.

### M36 Browse hot path & display fidelity closure (2026-08-13)

- Selection fan-out is memory-first: warm thumbnails and known gallery identity
  are reused synchronously; preview, viewer, metadata and status upgrades are
  generation-guarded background work. Hidden metadata consumers do no
  presentation build.
- MetadataOverlay, MetadataPanel and status share the single-flight
  `MetadataPresentationService`; `MetadataIndexer::cached()` is memory-only;
  same-directory tree navigation short-circuits; recents persistence is
  debounced/coalesced with shutdown flush.
- Thumbnail schema 3, Preview scaled metadata, ImageViewer CPU/GPU tile
  materialization and Compare use one ICC display-copy contract. Analysis
  pixels remain unchanged. `m36_display_tests` covers sRGB, AdobeRGB,
  Display-P3 and no-profile samples.
- Compare has one host per MainWindow with host-bound queued loads. Sync
  persistence retains Off/Zoom/Drag/All and toggles no longer Fit/reset the
  viewport. Batch analysis runs sequentially in a cancellable bounded worker.
- Current generated CTest registration is **88 tests**. The local focused gate
  passes the M36 display, Compare, lifetime and RatingStore suites. The full
  baseline remains environment-blocked by restricted temporary/cache file
  writes in existing workflow/cache tests; see the M36 evidence report.

- **Decode**: `DecoderRegistry` dispatches to `QtDecoder` (JPEG/PNG/BMP/TIFF/…),
  `RawDecoder` (embedded-JPEG preview for CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF/…,
  graceful fallthrough), and `QtFallbackDecoder`. RAW = **preview-only**, no
  demosaic (libraw deferred — M18).
- **Cache**: 5-level (disk/memory/…) + predictive preload.
- **Scheduler**: `TaskScheduler` + `DecodePool`; background async decode, UI never blocks.
- **Compare**: 2–8 images, synchronized zoom/pan/selection, blink, swipe, split,
  overlay, difference maps, layout presets, pixel inspector, session
  persistence (incl. blink state — M24 fix).
- **Analyze**: histogram, RGB mean, PSNR, SSIM, noise estimation, entropy,
  sharpness, MTF50, dead-pixel detection, ColorChecker Δ-E, ROI statistics —
  via `AnalysisEngine`; History + Pinned results persist across restarts.
- **Plugin SDK**: `AnalyzerRegistry` + `extern "C"` ABI (`apiVersion=1` /
  `abiVersion=1` / `sdkVersion=10000`, M14.2); kinds: Analyzer, Decoder,
  Exporter, Importer; reference examples in `plugins/example` (round-trip
  verified by `pluginregistry_tests`).
- **Robustness**: always-on Windows minidump crash handler (`CrashHandler`;
  dumps in `%APPDATA%/MViewer/crash-reports/`); one-time 崩溃报告 dialog on
  next launch; `--selftest` headless decode→metadata gate (CTest `selftest`).
- **Update check**: `UpdateChecker` (WinHTTP, GitHub Releases), Help menu
  「检查更新」 + silent check 8 s after startup; current version from
  `MViewerVersion.h` (M24).
- **Browse**: Directory tree + breadcrumb + recent/favorites/history,
  filter/sort/search, Thumbnail/List/Detail/Filmstrip views, unified Selection
  Model, Metadata overlay (`I`/`ESC`), batch rename/move/delete,
  rating store.
- **Export**: PNG/JPEG/TIFF/CSV/HTML/Report via unified `ExportJob`; batch
  export with progress/cancel; edited output-directory state is authoritative; whole-batch
  destination conflict/hard-link checks and atomic replacement prevent source or prior-output
  loss; UTF-8 paths round-trip safely through native Windows filesystem calls (see Phase 4D notes).
- **Workspace**: `.mvws`/`.mvproj` persistence incl. compare session, ROI,
  analysis text; crash-recovery autosave.
- **UX**: viewer zoom command system, fullscreen, mouse back/forward, slideshow,
  gallery keyboard loop, Ctrl+wheel thumbnail sizing, drag & drop, status-bar
  dimensions, decode-failure feedback.
- **GPU Stage A (opt-in)**: `ImageViewer` is a `QOpenGLWidget`;
  `GpuTileUploader` uploads tiles via `glTexImage2D` when `MVIEWER_GPU=1`;
  CPU `drawImage` is the verified default.
- **CI**: `ci.yml` (PR gate: clang-format + cppcheck + clang-tidy +
  build/zero-warnings + CTest incl. `bench_enforce` + `golden_image`),
  `nightly.yml` (ASan/UBSan/clazy/benchmark regression/golden/Perfetto),
  `release.yml` (perf report / UI regression / Dr.Memory / package / release).
- **Packaging**: portable ZIP + NSIS installer; SHA256SUMS + auto release
  notes (`scripts/release_manifest.ps1`); both artifacts launch on a clean
  Windows (PATH stripped) — verified 2026-08-05.

## Deferred / future (not shipping now)

- Full RAW demosaic (libraw) — M18.
- GPU Stage C/D: custom shaders, multi-pass, D3D11/Vulkan direct compositing
  (UI boundary frozen).
- Linux/macOS native installers (Linux CI artifacts build; only Windows ships
  an installer).
- AI workflows (captioning, embeddings, similarity search) — M18, planned.

## Known gaps (honest, not hidden)

- RAW = preview-only (no demosaic); some large/edge RAW containers fall
  through to the fallback decoder.
- GPU Stage A is opt-in (`MVIEWER_GPU=1`); default is the CPU tile path.
- 100 MP first-viewport fill measured 1480 ms on a 2-core Xeon VM (vs 392.8 ms
  on the 2026-07-24 baseline box) — B10 is report-only by design
  (`performance_budget.json`); regression axis is `--regression`.
- C4819 warnings in a few sources (UTF-8 without BOM) and C4530 in some
  test/bench TUs are tracked in Phase 6 of M24.
- Installer/portable ship no `plugins/` directory; on first run the app now
  creates an empty plugin home next to the executable (third-party
  plugins can be dropped in later; startup log stays informative).

## Product-force progress (2026-08-05 state)

| 工作项 | 状态 | 说明 |
|--------|------|------|
| A-1~A-10 对账 | ✅ | `docs/review/A_ITEMS_COMPLETION_AUDIT_2026-07-24.md` (~91%) |
| M16 Compare 收尾 | ✅ | Pixel Link / Overlay 透明度 / Diff 高亮 / 1~8 布局预设 / 编辑↔指标联动 / 每格直方图叠加 |
| Browse 门禁 | ✅ | Selection 统一 + 大目录渐进 fetch + 万级缩略图预测窗口 |
| M19 UI Model 收敛 | ✅ | `DirectoryModel` / `ImageListModel` / `WorkspaceModel` / `AnalyzerModel`；Metadata Overlay `I`/`ESC` + Lens/ICC |
| M20 Compare 键盘流 | ✅ | Ctrl+2/4/8 布局预设；B/S/W/O/H/Z/D/C/L 模式键；连续导航保留模式/ROI；`?` 快捷键帮助 |
| M21 Analysis+Export | ✅ | AnalysisPanel↔AnalyzerModel History/Pin；`ExportJob` 统一路径；Memory Timeline；Dashboard |
| Workspace 恢复 | ✅ | 布局/缩放/Compare/崩溃恢复（A-6 已落地） |
| M17 高价值子项 | ✅ | 批量分析导出 / 插件 rescan / 评分过滤导出 |
| M16 分析持久化 | ✅ | `AnalyzerModel` 历史/钉住/结果序列化，重启保留 |
| M17 自动更新 | ✅ | `UpdateChecker`（GitHub Releases，WinHTTP），版本来自 CMake SSOT（M24）；离线 `updatechecker_tests` 覆盖 JSON、点分版本、传输错误与安全下载链接策略 |
| M23 质量自动化 | ✅ | ADR/Bug 门禁、自动 dashboard、test matrix、benchmark trend（ADR-015） |
| M24 稳定性收敛 | ✅ | 2026-08-05 完成：70/70 CTest、干净环境打包、异步生命周期/工作流验收/测试可信度/性能/RC 验证（verdict: READY WITH NON-BLOCKING LIMITATIONS） |
| M25 RC 收敛 | ✅ 完成（2026-08-09） | Compare 顶栏三行防溢出；`thumbnailpanel.cpp` 682 行（ADR-014 内）；S1–S9 + T1–T4 Release 压测双配置 PASS；73/73 本地门禁（新增 `browse_convergence_tests` / `browse_convergence_ui_tests`）；缩略图缓存身份（path+mtime+size+requestedSize+schema）、generation 级取消、worker 纯 QImage、UI 线程零缩略图磁盘 I/O；格式 SSOT 统一（RAW/WebP/GIF 全链路一致）；MetadataIndexer 异步索引 + 排序键单次计算 + 字段级 Camera/Lens/ISO 过滤；Browse 关闭持久化；乱码修复。**剩余：目标硬件与人工 UX 签名**（见 `docs/review/M25_RC_CONVERGENCE_2026-08-09.md`） |
| M26 RC 可靠性收口 | ✅ 自动化完成 | 2026-08-09 完成：**78/78 CTest 全绿**。TaskScheduler exactly-once finalize（deadline/cancel/deferred/reject 全路径）、依赖反向图 cancelTree（transitive dependents）、metrics 无 underflow（修复后不再静默拒绝后续提交）、callback 线程契约 worker-thread 并文档化；MetadataIndexer per-request ownership（搜索重建与 Camera/Lens 过滤并发互不取消）、bounded cache、value-semantics cache 读取；ThumbnailPipeline 切换代际真正取消 + pending/handle 有界；ImageRepository 饱和/拒绝下 exactly-once 回调 + 有界同步加载（不再改全局 queue depth）；Preview 全图统计移出 UI 线程（worker-side `ImageStats`）。见 `docs/review/M26_RC_RELIABILITY_2026-08-09.md` |
| M27 异步生命周期收尾 | ✅ 自动化完成 | 2026-08-10 完成：**84/84 CTest 全绿**。TaskScheduler 故障注入（work/onProgress/done 异常不外逃、空 work 拒绝、cancelTree 抑制 done、drain 墙钟、graphMetrics、execution/callback failures）；ImageRepository 拒绝 exactly-once + 同步预算超时安全；QObject 析构时序（子进程崩溃检测、A→B→A、24MP UI 延迟）；100 轮生命周期压力（RSS/句柄/线程泄漏检查）。另含 UX 收尾：空状态/空文件夹提示、缩放中心回归、快捷键速查表与 Ctrl+C 双绑定修复、比较 2-8 守卫。 |

### M25 closure addendum (2026-08-09)

- Compare histogram consistency is closed: pane overlays repopulate after layout,
  navigation, pane swaps, and Blink rebuilds. Blink hides whole pane containers
  and synchronizes engine/UI state while rebuilding; main and pane histograms
  share adjusted-image and ROI scope even with the side panel hidden or per-pane
  overlays enabled. Direct `compare_acceptance_tests` regressions close the
  baseline's partially asserted overlay gap.
- **M25 Phase 2 — Browse data-pipeline convergence closed (automated part).**
  Thumbnail cache identity now covers path + mtime + size + requested thumbnail
  size + schema version (no 64→240 stale reuse); thumbnail work is QImage-only
  on the worker (PNG encode/write and cache reads off the UI thread, the
  visible-range disk probe removed); scheduled thumbnails carry their directory
  generation so switches cancel queued work and drop stale results. The shipped
  formats have one SSOT (`ImageFormats` over the decoder registry) across
  listing / gallery / viewer / recursive search / file dialogs / sidecars.
  Metadata indexing is one shared background, cancellable, generation-scoped
  pass (search panel + gallery filters); sort keys are computed once per file
  and compared in memory; recursive filename search runs off the UI thread.
  Camera/Lens filters are field-scoped and ISO reads the real sensor value.
  Closing inside the Browse workspace persists the pre-Browse panel state.
   Verdict: `AUTOMATED RC READY — TARGET HARDWARE / HUMAN UX SIGN-OFF PENDING`
   (see `docs/review/M25_RC_CONVERGENCE_2026-08-09.md`).

### M26 closure addendum (2026-08-09)

- **M26 — RC Reliability Closure (automated part).** No new features; the
  async runtime was hardened and every claim is pinned by new regression tests
  (`m26_scheduler_tests`, `m26_metadata_tests`, `m26_thumbnail_tests`,
  `m26_repository_tests`, `m26_stats_tests`, plus `workflow_ux_tests`
  Workflow 6) — **historical 78/78 CTest green**, golden/benchmark hard gates unchanged.
  Baseline bugs that were reproduced first and then fixed: scheduler
  deadline tasks never finalized (stuck pending/handles), deferred-task
  counter underflow poisoning later submissions (silent rejection), cancelTree
  walked prerequisites instead of dependents (also enabled a use-after-free of
  stale deferred captures), MetadataIndexer single-generation mutual
  cancellation (stuck gallery filters when the search re-index runs),
  ThumbnailPipeline obsolete-work decode waste + permanent pending/handle
  growth, ImageRepository silently-dropped submissions (aggregate callback
  could never fire under saturation) + infinite sync busy-wait + global
  queue-depth clobbering, Preview full-image statistics on the UI thread.
  Verdict: `AUTOMATED RC READY` — remaining sign-off is target-hardware
  stress re-run and the human UX Review Agent signature
  (see `docs/review/M26_RC_RELIABILITY_2026-08-09.md`).

## Plugin SDK (frozen)

- ABI triple: `apiVersion=1` / `abiVersion=1` / `sdkVersion=10000` (M14.2).
- Plugin kinds: Analyzer, Decoder, Exporter, **Importer** (A-9.3).
- Examples: `plugins/example/{ExampleAnalyzer,ExampleDecoder,ExampleExporter,ExampleImporter}Plugin.cpp`.
- Docs: `docs/sdk/PLUGIN_SDK.md`, `docs/sdk/PLUGIN_ABI.md`.
- Per-analyzer metadata version (`0.1.0`) is a plugin component version, not the
  app version; the app version is `MVIEWER_VERSION_STRING` only.

## Release process (current)

1. Bump `CMakeLists.txt` `project(VERSION)` — the single source of version.
2. `.\build.ps1 Test` must be green (currently 88 registered CTest tests incl.
   `version_consistency`, `updatechecker_tests`, `browse_convergence_tests`,
   `browse_convergence_ui_tests`, `m26_*_tests`, `bench_enforce`,
   `golden_image`, and the four M24 workflow acceptance suites).
3. Tag `vX.Y.Z`; `release.yml` builds, packages, attaches artifacts.
4. Verify `dist/MViewer-<X.Y.Z>-portable.zip` and `dist/MViewer-<X.Y.Z>-Setup.exe`.
5. Attach `SHA256SUMS.txt` (M14.8) and release notes from CHANGELOG.

## Status verdict

**M24 (Product Reality & Stability Convergence) complete — 2026-08-05.**
Verified: clean build from scratch (497.6 s, 0 errors, C4530 warnings 47 → 0
after the MSVC defaults were restored), **70/70 CTest green**, real-display
launch + graceful exit, portable ZIP and NSIS installer both launch on a
PATH-stripped clean environment and the installer uninstalls cleanly, SHA256
manifests generated. All detached UI workers removed (bounded pools +
QPointer marshaling); large-folder UI stall fixed (2.7 s → 184 ms on
10 000-image folders); Browse/Compare/Analyze/Export workflows exercised by
four new acceptance suites; version is a single source; STATUS/roadmap/
installer/code agree.

Open items (non-blocking, see the M24 verdict doc): 24 MP JPEG cold decode
~3.5 s and 4 K TIFF ~2.1 s on the 2-core VM (single-threaded decode);
C4819 warnings in 6 UTF-8-without-BOM sources; installer/portable ship no
`plugins/` dir (benign startup log message); in-process throw isolation for
plugin analyzers requires /EHsc (now restored on this machine) plus a
subprocess runner for hard crashes. Verdict:
**READY WITH DOCUMENTED NON-BLOCKING LIMITATIONS** — see
`docs/review/M24_FINAL_VERDICT_2026-08-05.md`.
