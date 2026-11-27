# STATUS — MViewer

> Snapshot: 2026-11-27 · Version: **1.0.237** · Release tag: **v1.0.237**
> Single source of truth for "what the product is right now". For plans, see
> `docs/roadmap.md` (engineering) and `docs/ROADMAP_PUBLIC.md` (public).
> Current state summary: Beta, Windows x64 installer + portable zip, English and
> Simplified Chinese UI. Release evidence (milestone review and qualification
> reports, M24–M61) is indexed in [`docs/review/INDEX.md`](docs/review/INDEX.md);
> the live gate is `.\build.ps1 Test` plus GitHub CI.

## Unreleased — Dr.Memory CI tooling

- Automatic Dr.Memory execution in the tooling and Release workflows is
  **disabled/skipped: not executed, no memory verdict**, not a memory pass.
- The November 18 bounded comparison used the same `core_tests` binary/PDB on
  Windows 20348 / Qt 6.8.0: both 2.6.0 and maintained 2.6.20434 timed out after
  180 seconds, with 869 / 748 raw records. Unknown-kernel and delete-classification
  warnings remain unresolved. These are not confirmed-bug counts or proof that
  all findings are false positives; neither run establishes memory-clean.
- [Comparison run and retained reports](https://github.com/lgxgizh/mviewer/actions/runs/44344380527).
  Strict runner scripts remain available for manual use; `workflow_dispatch`
  runs contracts only, not Dr.Memory. ASan/UBSan/TSan, ordinary tests, and release
  signing/dependencies are unchanged.

## 1.0.237 - multi-pane thumbnail refresh, pipeline size mismatch (2026-11-27)

- Shared `ThumbnailProvider` invalidation is now once per batch; each pane drops
  only its own cache (`dropLocalThumbnailCache`). Previously the second pane's
  invalidate cancelled the first pane's enqueued decode and the cell stayed white.
- A hidden/empty pane receiving a shared-directory delta no longer touches the
  shared pipeline at all.
- A result whose size differs from the panel's `m_thumbSize` is accepted at its
  own size instead of being dropped (the singleton pipeline can hold another
  pane's size); `thumbReady()`'s by-path fallback serves it.
- `invalidateThumbnailCacheFor` normalises the path before matching cache keys.
- `browse_rotate_thumbnail_tests` covers four scenarios, including MainWindow's
  exact multi-pane order and a forced pipeline-size mismatch.

## 1.0.236 - review follow-up: Fft2D padding, export job threading (2026-11-27)

- **P0** `Fft2D`: the column sweep must cover all `paddedW` columns (the row pass
  spreads energy into the padded region); restored, with a 12x10 vs naive-DFT
  regression test.
- **P0** `CompareAdjustedExportJob`: worker callbacks no longer touch `this`;
  they carry a shared alive/generation/status block by value and deliver via
  `mvcore::postToMainThread`.
- **P1** `DisplayAdjustController`: Reset now syncs the panel widgets; the anchor
  is only installed when auto-anchoring is on; `setAdjustPanelVisible(true)` on a
  visible panel no longer hides it.
- **P2** attach/detach cleans up the old panel/anchor/action;
  `UpdateChecker` uses `!= 5` for the canonical release URL segments.

## 1.0.235 - Browse rotate no longer blanks the gallery thumbnail (2026-11-27)

- `Ctrl+R` in Browse mode left the rotated image's cell blank/white and it did not
  recover. Root cause was two compounding defects on the live-folder path:
  `applyDirectoryDelta` dropped the cached thumbnail without re-declaring the
  visible range (so nothing re-requested it), and the deferred source invalidation
  bumped the pipeline's path revision after an enqueue, so the fresh decode was
  discarded as stale.
- Invalidation now runs inline and the visible range is re-declared after the delta.
- `browse_rotate_thumbnail_tests` covers both the explicit host refresh and a real
  `DirectorySnapshot` diff.

## 1.0.234 - compareworkspace.h off the ceiling via Private (2026-11-27)

- `compareworkspace_p.h` gains `CompareWorkspace::Private`: all 99 panel widget
  pointers (`d-><name>`, same leaf names) move out of `compareworkspace.h`
  (798 -> 728 lines). Two stay in the header because a test friend struct reaches
  them (`m_swipeChk`, `m_compareCanvas`).
- Deliberately not grouped sub-structs or controllers: flat passive state, and
  every compare TU keeps reading the handles directly.
- Also fixes the ROADMAP_PUBLIC version string that v1.0.233 left stale
  (caught by `version_consistency`).

## 1.0.233 - imageviewer.h off the ceiling via Private, not controllers (2026-11-27)

- Display-raster/neighbour-preload value types move to `imageviewer_displayraster.h`
  (`ImageViewer` keeps `using` aliases so tests still compile).
- New `imageviewer_p.h` holds `ImageViewer::Private`; profile / scope / annotate /
  tool state moves into `d->profile|scope|annotate|tools`. The paint and markup TUs
  read those fields directly - no callback boundaries invented around them.
- `imageviewer.h` 801 -> 737 lines; drops `annotationstore.h`, `cvoverlay.h`,
  `<QLineF>`.

## 1.0.232 - drag & drop gets its own TU (2026-11-27)

- `mainwindow_view.cpp` was at 878 lines. Drag & drop (dragEnter/dragMove/
  dragLeave/drop, external-open handling) moves to `mainwindow_dragdrop.cpp`
  (135 lines); view drops to 754. Behaviour-preserving.
- No responsibility TU in `src/` is above 850 lines afterwards.

## 1.0.231 - histogram computation gets its own TU (2026-11-27)

- `compareworkspace_analysis.cpp` was at 941 lines against the 1000-line
  responsibility ceiling. Histogram computation (title text, refresh scheduling,
  batched background worker) moves to `compareworkspace_histogram.cpp`
  (387 lines); analysis drops to 567. Behaviour-preserving.

## 1.0.230 - pixel-link mode gets its own TU (2026-11-26)

- `compareworkspace_interact.cpp` was at 948 lines against the 1000-line
  responsibility ceiling. The pixel-link mode (markers, crosshairs, numeric
  deltas, dashed connectors) moves to `compareworkspace_pixellink.cpp`
  (202 lines); interact drops to 760. Behaviour-preserving.

## 1.0.229 - ThumbnailTrash gets its own subsystem (2026-11-26)

- `ThumbnailTrash` owns the app-private fallback trash: directory, bounded prune
  policy (30 days / 2 GiB) and the OS recycle-bin move. Extracted from
  `thumbnailpanel_fileops.cpp` (734 -> 679 lines). Behaviour-preserving.

## 1.0.228 - ThumbnailPanel dimension probe gets its own TU (2026-11-26)

- `thumbnailpanel_async.cpp` was at 860 lines. The M46 cooperative dimension probe
  (viewport-first order, thread-pool worker, batched publication,
  `ensureDimensions`) moves to `thumbnailpanel_dimensions.cpp` (299 lines); async
  drops to 578. Behaviour-preserving.

## 1.0.227 - ThumbnailPanel batch export gets its own responsibility TU (2026-11-26)

- `thumbnailpanel_fileops.cpp` had reached 948 lines against the 1000-line
  responsibility ceiling. The batch analysis-export subsystem (~213 lines:
  analyzer picker, background run, progress dialog, CSV/JSON writer) moves to
  `thumbnailpanel_batch_export.cpp` (236 lines).
- Behaviour-preserving: every moved method keeps its declaration in
  `thumbnailpanel.h`; fileops drops to 735 lines.

## 1.0.226 - adjusted export becomes a job; compareworkspace.h back under the cap (2026-11-26)

- `CompareAdjustedExportJob` owns cancellation flag, full-resolution decode handle,
  worker task and stale-completion generation for "export this pane's tuning
  result". CompareWorkspace keeps the UI and index/frame resolution; its header
  loses four members plus the launch method and is at 799 lines again.
- Status reporting and task-handle writes stay on the owning thread (the job
  marshals them), so the extraction adds no cross-thread member access.
- New `compare_adjusted_export_tests` covers the file being written at full
  resolution, the adjustment actually being applied, the success message and
  repeated `cancel()`.

## 1.0.225 - one DisplayAdjustController for viewer and compare (2026-11-26)

- `DisplayAdjustController` replaces the compare-only controller and absorbs the
  image viewer's adjust flow: panel, anchor, five signal handlers and LUT loader
  are now one component instead of two near-identical copies.
- `ImageViewer` no longer owns an adjust panel or `syncAdjustPanel()`; its public
  surface forwards to the controller. Compare keeps the top-right anchor, the
  viewer keeps `layoutFloatingOverlays()` placement.

## 1.0.224 - Compare Adjust, Python examples, first controller extraction (2026-11-26)

- **View > Compare &Adjust (`G`)**: exposure / gamma / auto-stretch / 3D LUT applied
  to both compare panes through the workspace's own colour context, so a dark pair
  can be lifted together while split/swipe/overlay stays in sync.
- `examples/python/`: hot_pixels, noise_estimate and roi_metrics scripts plus a
  pure-stdlib PNG reader and protocol notes.
- **`CompareAdjustController`** owns the panel, its anchor and the signal wiring;
  MainWindow holds one unique_ptr. First subsystem-controller extraction from the
  God-class headers (the incremental approach used for CvOverlay).
- `compare_adjust_tests` pins that the adjust reaches the workspace colour context.

## 1.0.223 - DisplayAdjust divide-by-zero, script-thread QPointer, release-link binding (2026-11-26)

- `applyDisplayAdjust()` could compute a zero stretch range on a flat channel and
  then call `std::lround(NaN)` (undefined behaviour). Flat channels now keep the
  full 0..255 range.
- `PythonScriptRunner::runAsync()` no longer dereferences a QPointer on a worker
  thread; delivery goes through `mvcore::postToMainThread()`.
- Tile uploads drain stale OpenGL errors before judging their own result.
- The update-check release link must name the requested owner/repo and tag.
- `loadWorkspace()` accepts a cancellation probe; `UsmSharpen` uses the row
  accumulator; `PluginLibrary` separates read failure from EOF; `Fft2D` skips
  all-zero padded rows/columns and reuses its twiddle buffer.
- **`workflow_ux_tests` is green again and no longer timing-dependent**: it waits
  for Browse's status instead of assuming one fixed pump suffices.
- Dr.Memory is no longer dead config: manual-only in `drmemory.yml`, removed from
  `release.yml`.

## 1.0.222 - Lanczos-3 resampling, USM tap hoist (2026-11-26)

- `lanczosQ` used 5 taps of a kernel with support 3 and never widened its window
  when minifying. It now uses the full ±3 support and scales it by the downsample
  ratio (radius capped at 8). New tests pin the invariant that a constant field
  resamples to itself at every ratio tested.
- `combineLumaSharpen` computes its clamped tap rows once per row instead of once
  per pixel per tap.

## 1.0.221 - render cache, HiDPI tile units, preload cancellation (2026-11-26)

- The compare-canvas GPU composite is cached by the base-surface key: a repaint
  with no state change no longer re-rasterises both panes and reads the framebuffer
  back.
- Tile magnification compares logical screen pixels against texture texels in one
  unit; HiDPI no longer mis-classifies an enlarged tile as minified.
- Linux ICC selection prefers a profile named after the display and reports a
  multi-profile directory instead of taking the alphabetically first.
- ICC version parsing no longer reads reserved bytes 10-11 as a bugfix field.
- `CompareWorkspace` submits loads outside `handlesMutex`; a discarded analysis
  preload is cancelled rather than dropped; `restoreWorkspaceState` disconnects its
  ready handler explicitly instead of being consumed by an unrelated event.
- `trimMipsToBudget` re-reads each chain under the lock before dropping it.
- Greyscale PFM decodes at one float per sample; JpegExif bounds use subtraction;
  NSIS uninstall refreshes the shell like the install path does.
- Perf baseline B11 recalibrated to a measured 130 ms seed (p50 104.975 ms);
  B20 registered as provisional/report-only.

## 1.0.220 - command-line open rejects the device namespace (2026-11-26)

- `\\.\` and non-drive `\\?\` targets are refused before anything is opened;
  UNC shares stay allowed (network browsing is supported). Covered by
  `test_commandline`.

## 1.0.219 - plugin hash->map race closed, Disable actually disables (2026-11-26)

- `loadPluginLibraryVerified()` hashes the file through a handle opened with
  write/delete sharing denied and keeps it open across `LoadLibraryExW`; both
  trust paths use it. The old sequence let the file be replaced between check and
  map, and DllMain runs at map time.
- `pinPluginModule()` takes an address inside the mapped module instead of
  reopening the DLL by path on Windows.
- `plugins/disabled` stores canonical paths; Startup normalizes them and
  `loadTrusted` skips disabled files before hashing. Disabled plugins are listed
  as `[Disabled] Not loaded` so they can be re-enabled.

## 1.0.218 - thumbnail pipeline scheduling, monitor latch, plugin trust UI (2026-11-26)

- `ThumbnailPipeline` submits outside its mutex; `removeConsumer()` clears that
  consumer's pending entries and its in-flight work is treated as stale;
  `m_pathRevisions` is bounded.
- `DirectoryMonitor::startReconcile()` can no longer wedge `m_scanInFlight`.
- `ThumbnailPipeline` / `ImageRepository` are leaked singletons (no destructor to
  lose; a worker at teardown must not see a destroyed instance).
- Export installs a cancel probe around the long writers and re-checks before
  committing.
- JpegTurbo: 256 MiB input cap + the shared 268M-sample raster budget.
- Plugin Manager: "Revoke Trust" forgets recorded hashes.
- Nightly fuzz runs `fuzz_session_package` and `fuzz_hdr_decoder` too.

## 1.0.217 - screen-change adjustments, package extraction checks (2026-11-26)

- `onDisplayScreenChanged()` keeps `adjust` when it rebuilds the colour context,
  so crossing a monitor boundary no longer resets exposure / gamma / auto-stretch.
- Session-package extraction reports a failed folder creation and a short write
  instead of leaving a truncated file behind.

## 1.0.216 - Python isolation and atomic export commit (2026-11-26)

- `python -I` + a scrubbed `PYTHON*` environment; the script response read is
  capped at 8 MiB.
- Export commit is a single atomic `MoveFileExW` replace (no attributes probe
  between check and rename).
- `UpdateChecker` fails the request when it cannot enforce TLS 1.2+ or the
  no-downgrade redirect policy.

## 1.0.215 - review P0/P1 batch (2026-11-26)

- **Display adjustment was a complete no-op.** The panel mutated the live colour
  context and `setDisplayColorContext()` compared it with itself, so exposure,
  gamma and auto-stretch never reached any raster. Now compares against the key
  actually applied.
- Grey images skipped the exposure/gamma/auto-stretch tables when a 3D LUT was
  loaded.
- Batch cancel lost-wakeup deadlock fixed; `TaskScheduler::depDone()` no longer
  treats "progress 100" / cancelled as finished.
- `HdrDecoder::toneMapToRgb24` size check is 64-bit (was an int overflow).
- Exit joins are bounded (global pool, directory monitor, thumbnail scan pools).
- `ExporterRegistry` is locked; preload/analysis failure paths now deliver their
  terminal callback instead of leaving consumers pending.
- Decoder input budgets: shared 268M-sample cap for PNG/TIFF/BMP, TIFF IFD entry
  and duplicate-key-tag limits, OpenEXR overlapping scanline rejection.
- GPU: tile uploads neutralise `GL_UNPACK_ROW_LENGTH`; failed texture allocation
  falls back to CPU instead of black tiles; grey expansion booked at 3 channels;
  compare upload restores the caller's texture binding and drops a full-image
  copy; `BlitStateGuard` tolerates null `extraFunctions()`.
- CI: `ci-gate` now depends on `thread-sanitizer` and `complexity-ratchet`.
- Known environment issue (pre-existing on v1.0.214, not introduced here):
  `workflow_ux_tests` fails its fullscreen-workflow status-message assertion in
  this workspace, standalone and in the full gate.

## 1.0.214 - partial review checkpoint (2026-11-25)

- Partial release: completed, CI-verified work only (44 verified + 1 source
  no-change + 169 pending of the 214 acceptance criteria). No Dr.Memory verdict.

## 1.0.213 - compare canvas composites on the GPU (PR-09) (2026-11-24)

- `MVIEWER_COMPARE_GL=1` moves split / swipe / overlay compositing into a
  fragment shader over two pane textures; the panes are still rasterised by the
  same CPU calls, so only the placement decision moved. ADR 038.
- New report-only benchmark B20: two-pane composite = 149 ms/frame (the number
  this work beats).
- Verified bit-exact against tests/gl_baseline at scale factors 1.0 and 2.0:
  0 differing pixels for split (x2), swipe (x2) and overlay.
- Checkerboard stays on the CPU path: QPainter anti-aliases its QRegion clip at
  block edges and a hard shader switch cannot reproduce that.

## 1.0.212 - fragment-shader tile blitter behind an A/B switch (2026-11-24)

- `gpu/ImageBlitterShader` + `MVIEWER_BLITTER=shader`: samples the tile, applies
  exposure / inverse-gamma, alpha-blends an overlay texture (ADR 037). Qt's
  blitter stays the default; a link failure falls back to it.
- `blitshader_flag_tests` pins the switch contract.
- Not done: the shader is not pixel-verified here - QOpenGLContext::create()
  faults offscreen, so it needs a hardware framebuffer harness before it can be
  made the default. Exposure/gamma are already baked CPU-side, so they are not
  fed to the shader yet.

## 1.0.211 - overlay stays on the GPU tile path; PR-09 baseline (2026-11-24)

- `drawGpuTiles()` no longer requires `overlayMode == None`: the derived overlay
  tiles are ordinary ImageData, so FocusPeaking / FalseColor / Zebra stop forcing
  the whole visible tile layer through QPainter.
- New report-only benchmark B20 (`compare_composite_frame_ms`): two-pane
  split/overlay composite of 4096x3072 sources at 1920x1080 = 149 ms/frame on the
  CPU canvas - the number PR-09 has to beat.
- Not done: the fragment-shader blitter and the GL compare compositor. They
  touch the default render path of every image and need their own round.

## 1.0.210 - libjpeg-turbo JPEG fast path (2026-11-23)

- New third-party dependency: libjpeg-turbo 3.1.0, fetched/built like LibRaw,
  SHA256-pinned, static. Writes RGB24/Greyscale8 straight into ImageData with a
  SIMD IDCT and native 1/2, 1/4, 1/8 scaling. ADR 036.
- Measured B11 (3840x2160 JPEG, p50): 170 ms -> 104 ms (-39%), accurate SIMD IDCT.
- `jpeg_turbo_tests` pins correctness against Qt's decode (mean abs diff 1.15,
  threshold 2), the greyscale case, the scale-factor choice and the fallback
  contract.
- ICC-bearing and EXIF-rotated JPEGs keep Qt's decode.
- `QtDecoder.cpp` split (complexity gate): metadata helpers moved to
  `QtDecoder_meta.cpp` behind a Qt-free internal header.

## 1.0.209 - global thread pool consolidation (2026-11-23)

- The two `QThreadPool::globalInstance()` fire-and-forget tasks (Python script
  bridge, internal-trash prune) now use `TaskScheduler`'s bounded, instrumented
  background pool.
- Not done: PR-05 JPEG fast path (needs libjpeg-turbo or a WIC JPEG entry point
  plus its own measurement pass), PR-08/09 GPU shader pipeline.

## 1.0.208 - P0 safety + P1 hot loops (2026-11-23)

- PR-01 PythonScriptRunner: QPointer guard (no UAF on a dialog closed mid-run),
  interpreter mutex, dangling `responsePath` removed, regression test added.
- PR-02 `CompareWorkspace` uses its `AsyncLifetimeToken` instead of a cross-thread
  `QPointer`; `TaskScheduler` task state moved into a `shared_ptr`; the blanket
  `race:run` suppression replaced by the exact `race:runTask` frame.
- PR-03 `ImageData` content version + `GpuTileUploader::ensure()` O(1) hit (ADR 035).
- PR-04 DisplayAdjust 8-bit 1D LUT (768 pow instead of 36 M) + float tetrahedral
  3D LUT.
- PR-06 LibRaw OpenMP demosaic. PR-07 USM row accumulation + FFT stage twiddles.
- PR-10 `tests/CMakeLists.txt` split into core / ui / workflow.
- Not done: LTO (breaks the test links under /LTCG), PR-05 JPEG fast path,
  PR-08/09 GPU shader pipeline, PR-10 thread-pool consolidation.

## 1.0.207 - toolbar un-collapsed, favorites removed, changelog English (2026-11-23)

- Browse toolbar: every action is back on the strip; the v1.0.206 "More"
  overflow is gone.
- Favorites removed end to end (action + Ctrl+D, File-menu submenu, sidebar
  bar, AppState persistence, DirectoryModel API). Recent folders/files remain.
- `CHANGELOG.md` 1.0.199 - 1.0.206 rewritten in English.

## 1.0.206 - toolbar decluttered; TSan leg required (2026-11-22)

- Occasional actions moved into a More overflow menu at the end of the strip
  (it used to hold 15+ buttons in 52px).
- The TSan leg no longer sets `continue-on-error`; `ci/tsan.supp` records the
  third-party races plus one stress-test recycling false positive marked
  TODO(investigate).
- Not done: collapsing the location box by default (it would hide the current
  path, the core affordance - a product call).

## 1.0.205 - UI finish + test target move (2026-11-22)

- Plain toolbar icon buttons unified to kControlHeight (they mixed 26px and
  34px before).
- History and Pinned merged into one tab strip.
- Thumbnail card shadow rendered at devicePixelRatio (fixes 150%/200% softness).
- Hover fade switched to easeOutCubic.
- Empty-state key caps became rounded chips with a monospaced face.
- 178 test targets moved to `tests/CMakeLists.txt`; `src/CMakeLists.txt`
  3711 -> 718 lines.
- LibRaw 16-bit gained `ditherRows16ToRgb888()`, dropping the 8-bytes-per-pixel
  RGBX64 intermediate (at most 4 full-image buffers -> 3).
- ImageRepository / ThumbnailPipeline / EventBus / Logger now state their
  ADR-017 lifetime policy at the definition, per rule 5.

## 1.0.204 - build governance wrap-up (2026-11-22)

- The `lizard` complexity ratchet became its own required job
  `complexity-ratchet`: it used to live in `continue-on-error` `build-health`,
  so breaching the limits never blocked anything.
- YUV extension column right-aligned.
- TSan leg: runs a real report (`halt_on_error=1`) but is still advisory. Once
  hardened it reported two races attributed to our own frames
  (`TaskScheduler.cpp:51` and the `CompareWorkspace` `QPointer` guard), both
  marked TODO(investigate); until they are fixed, making it required would only
  produce a gate held up by an ignore list. Known third-party races are listed in
  `ci/tsan.supp`.
- Not done, with reasons: moving `runtime_storage.h` into `core/` was rejected by
  R5 (core headers must not include Qt) and reverted; the `tests/CMakeLists.txt`
  split was not done that round (test targets interleaved with the Format /
  clang-tidy / benchmark sections).

## 1.0.203 - UI finish and Win11 feel, second review (2026-11-22)

- Breadcrumb separator `>` -> `›`, last segment bold.
- ComparePaneHeader file name 15pt -> 10.5pt.
- Thumbnail cards switched to `ElideMiddle`, so long prefixed names still show
  the trailing frame number.
- Analysis sidebar split into two rows; the combo box is no longer squeezed away
  at 300px.
- Python output uses a monospaced face.
- Mica now writes `DWMSBT_NONE` back when disabled (it used to stay translucent).
- Python Run / blind-test Reveal & Export / Export gained `primary="true"`.

## 1.0.202 - remaining P0/P1 product defects (2026-11-22)

- Blind test: the default score is no longer recorded as 0 (the slider's 3 IS
  the first side's score), `Space` is actually bound, and switching sources
  resets the reveal state.
- Annotation deletion is undoable with `Ctrl+Shift+Z`.
- USM dialog duplicate labels `Amount (Amount):` / `Radius (Radius):` removed.
- PluginLoader's `createAnalyzer` / `name()` wrapped in exception isolation.
- The WIC TIFF 1x1 probe is gated on file size to avoid a full-raster allocation.
- Logger: recursive mutex (a Qt warning raised while the handler held the lock
  used to re-enter the same non-recursive mutex) plus 8 MiB rotation.
- TaskScheduler: `m_backpressure` written under the lock, the back-pressure
  decision moved inside `enqueueTask()` under the same lock, and `shutdown()`
  clears `m_deferred`.
- DiskCache: the read-hit `ts` write is throttled per key (5 s), thread_local, so
  the throttle adds no shared lock.

## 1.0.201 - concurrency and build wrap-up, second review (2026-11-22)

- `ThumbnailCache` ABBA deadlock: `get()`/`put()` schedule the bootstrap outside
  `m_mutex` (they used to hold it while joining the worker that needs it).
- `setDisplayColorContext()` wraps `m_gpu.clear()` in a GL context, fixing the
  texture handle leak.
- Added `shutdownMainThreadDispatcher()`: after shutdown a late closure is dropped
  instead of running inline on a worker; regression test added.
- The Python bridge gained `runAsync()`, so a script can no longer freeze the UI.
- `mviewer_core` dropped the redundant `Qt6::Widgets` and declares
  `Qt6::Gui`/`Qt6::Core` explicitly.
- The TSan leg became hard: `halt_on_error=1`, `continue-on-error` and `|| true`
  removed.
- Not done, with reasons: the `tests/CMakeLists.txt` split, the leftover P1s
  (TaskScheduler / DiskCache / PluginLoader / Logger), non-leaking singleton /
  ADR-017 consistency, annotation undo and the blind-test interactions.

## 1.0.200 - UI finish and Win11 feel, second review (2026-11-22)

- Added `ImageViewer::layoutFloatingOverlays()`: corner slots for the scope /
  spectrum / adjustment panels, disjoint, clear of the canvas-drawn chips and
  re-laid out on every resize; `viewer_overlay_layout_tests` added.
- The scope widget got a rounded card shell and a reserved bottom readout band.
- The display adjustment panel became a `canvasFloatCard` with a monospaced
  EV/gamma readout.
- Light-theme slider handles became accent-filled with a bg2 ring.
- Focused inputs switched to the Win11 Fluent neutral surface + 2px accent bar.
- Dialog buttons went to a 6px radius; popups lost the second outline stacked on
  DWM's rounded corners.
- ExportDialog scrolls its sections with the button row pinned; PreferencesDialog
  scrolls per page and defaults to 600x520.

## 1.0.199 - new-feature P0 triage, second review (2026-11-22)

- Session package: Zip Slip rejected on both write and read (only
  `session.json` and `images/<plain file name>`), the temp directory is owned by
  MainWindow, the real extension is kept, 128 MiB / 256 MiB caps added.
- DisplayAdjust: `Grayscale8` auto-stretch branches on channel count and uses
  `stride`; the `.cube` axis order corrected to spec.
- `ScopeWidget` dereferences `QRgb*` (R/G used to be 0).
- The FFT spectrum writes Grayscale8 bytes directly, and `hasOffAxisPeak` masks
  the four DC corners.
- A bare `L` is no longer swallowed by `handleZoomKey`, so the line profile
  works.
- OpenEXR: `dataWindow` length check, `chlist` stride 18 -> 16, `offset + 8`
  wrap-around guarded, size caps; grayscale PFM writes all three channels.
- `ClipboardPasteCommand` goes through `pathFromUtf8()`, with a Chinese-path
  regression case.
- Annotation text draws from the anchor instead of being clipped by the 1x1 rect.
- Not done and recorded: annotation undo, the blind-test default score / `Space` /
  switch reset, the async Python bridge, and several missing `tr()` strings.

## 1.0.198 — Item 2 governance, part 2 (2026-11-21)

- 247 test sources (~73k lines) moved out of `src/` into `tests/`; `src/` is now
  product code only. CMake paths updated, including the generated per-module
  loop.
- The `__FILE__`-derived repo-root fallback in eight core suites matched
  `/src/core/`, so it silently degraded to `"."` after the move — CI caught it.
  Now accepts both `src/` and `tests/`, and `test_m3m4m5` gets the
  `MVIEWER_SOURCE_DIR` define every other target had.
- Authenticode signing is wired and opt-in (secret-based PFX); this repo has no
  certificate, so artifacts are unsigned until one is procured.
- **Correction:** the v1.0.197 artifacts carried internal version 1.0.196 — the
  `project(VERSION)` SSOT in the root `CMakeLists.txt` was not bumped for that
  tag. Fixed here; `version_info.txt` now reads 1.0.198.

## 1.0.197 — Linux monitor ICC (2026-11-21)

- `loadLinuxIccProfile()` reads the display profile from `$MVIEWER_ICC`, then
  `$XDG_CONFIG_HOME/color/icc`, `~/.config/color/icc`, `/usr/share/color/icc`
  and `/var/lib/colord/icc`; unreadable means the sRGB fallback. macOS ColorSync
  remains the fallback (no macOS runner here to verify it).

## 1.0.196 — Python script bridge (2026-11-21)

- Tools → "Run Python Script…": a user `.py` runs as a subprocess on the
  current image via `<interpreter> <script> <request.json> <response.json>`.
  No shell, unchanged environment, 30 s timeout that kills the child,
  interpreter lookup `$MVIEWER_PYTHON` → `python3` → `python` → `py`.
- Deliberately a subprocess protocol rather than an embedded interpreter: an
  in-process runtime would put arbitrary code in the image-loading process.

## 1.0.195 — Undo for a gallery paste (item 5) (2026-11-21)

- A gallery paste is recorded on the shared undo stack, so the existing Ctrl+Z
  deletes the copies MViewer wrote. The command is undo-only (the sources are not
  retained, so Redo would be a no-op) and is folder-scoped: a stale entry cannot
  delete anything in a folder you navigated to later.

## 1.0.194 — Item 2 governance, part 1 (2026-11-21)

- `scripts/architecture_gate.ps1` no longer exempts `*_p.h`. Removing the
  blanket exemption surfaced 4 real layering edges, all fixed by routing through
  `ImageLoadingService::load()/loadWorkspace()`, `MemoryTracker` and
  `ThumbnailProvider`. Gate back to 0 violations.
- CI gains an advisory ThreadSanitizer leg (Linux, core suites offscreen).
- `CvOverlay` extracted from `ImageViewer` (first God Class extraction), with
  its own test.
- Not done and called out: the test migration out of `src/` (churn, no user
  effect) and code signing (needs a certificate this repo does not have).

## 1.0.193 — Shortcut help, preferences fit, language hot-switch (2026-11-21)

- F1 cheat sheet and in-app guide document the review tools (`L`, `W`, `A`/`T`,
  `G`) and the new context-menu entries; Compare's `,` / `.` documents the
  frame-step behaviour.
- Preferences fits itself to the available work area on show.
- Switching language re-sends `LanguageChange` to every top-level widget so
  sub-panels follow.

## 1.0.192 — Large-folder stall (item 3) (2026-11-21)

- The progressive gallery scan published the whole browse sequence on every
  128-file batch, and each publication made the consumers walk the whole
  growing list — O(N²) over the scan. Publication is now coalesced to one
  event-loop turn, at most once per 100 ms; the first screen still appears
  mid-scan. 10,000 images: 8 publications instead of ~79, scan ~0.8 s.

## 1.0.191 — Portable session package (2026-11-21)

- Tools → Export/Import Session Package: a store-only ZIP carrying the compare
  session manifest plus the referenced images. Import extracts to a temp
  directory and reopens the session, leaving the originals untouched.

## 1.0.190 — 2D FFT spectrum + CV box overlay (2026-11-21)

- Viewer context menu → "FFT Spectrum": radix-2 2D FFT of the frame luma, DC
  removed, centred and log-scaled; `hasOffAxisPeak` exposes a moire tripwire.
- "Load CV Boxes…" reads boxes from JSON in image coordinates and draws them on
  the canvas.

## 1.0.189 — A/B blind test with MOS export (2026-11-21)

- Viewer context menu → "A/B Blind Test…": shuffled presentation order from a
  recorded seed, one side visible at a time, identity hidden until both 1–5 MOS
  scores are in, then revealed and exported as JSON.

## 1.0.188 — Live display adjustment + CIEDE2000 / ColorChecker (2026-11-21)

- Viewer `G`: exposure (±3 EV), gamma, Auto-Stretch and an Adobe `.cube` 3D
  LUT (trilinear). Presentation-only; carried in the display colour context so
  rasters and tiles re-materialize through the normal path.
- CIEDE2000 ΔE00, verified against all 34 Sharma/Wu/Dalal (2005) reference
  pairs.
- "Color Checker dE00…" samples a 4×6 grid inside the current selection (or the
  whole frame) and reports per-patch ΔE00 + mean/max against the 24-patch D65
  reference.
- Features 5 and 6 shipped together: both were completed in the same
  verification window and share viewer/context-menu wiring.

## 1.0.187 — HDR: OpenEXR and PFM (2026-11-21)

- `HdrDecoder` is registered ahead of the Qt decoders and handles `.exr`
  (uncompressed single-part scanline, HALF/FLOAT) and `.pfm` (`PF`/`Pf`).
- Float working image is tone-mapped to 8-bit (Reinhard + gamma 2.2) so
  highlights compress instead of clipping; metadata carries `format`,
  `bitDepth`, `colorSpace = linear` and `MViewer.Hdr`.
- Tiled / compressed / multi-part EXR is reported as unsupported instead of
  being mis-decoded.
- `hdr_decoder_tests` writes its own PFM and EXR fixtures and checks the
  container parsing, the row order, the tone map and the extension contract.

## 1.0.186 — Review markup and review-image export (2026-11-20)

- Viewer markup tool (`A`): drag a box, `Shift`+drag an arrow, `T` a text label,
  right-click deletes. Markup lives in image coordinates and is persisted as
  `<stem>.mviewer-annotations.json` next to the source.
- `Export Review Image…` writes a `<stem>-review.png` with the markup burned in
  at source resolution plus the JSON sidecar.
- The markup and line-profile tools are mutually exclusive.

## 1.0.185 — Raw sequence playback and synchronized frame scrubbing (2026-11-20)

- A multi-frame `.yuv` / `.y4m` capture is now a playable sequence: Space
  play/pause, `,` / `.` step frames, Play/Pause/Restart in the context menu, and
  `Frame n/N · Playing` in the status pill. Multi-page TIFF documents keep their
  page wording.
- Compare: `,` / `.` step every multi-frame cell together, so two raw captures
  stay on the same frame while scrubbing. Without a multi-frame cell the keys
  keep their previous overlay-opacity meaning.

## 1.0.184 — Line profile and video scopes (2026-11-20)

- The viewer gains an ISP/CV review readout pair: `L` arms a draggable line
  profile (R/G/B/luma along the line, with sample count and mean luma) and `W`
  opens a corner scope panel that cycles luma waveform → RGB parade →
  vectorscope. Both are also in the viewer context menu.
- The scopes read the frame already on screen (the LOD raster in LOD mode) and
  never re-decode, so enabling them costs nothing until they are shown.
- `viewer_scope_tests` covers the toggle, the mode cycle and the committed
  profile line.

## 1.0.183 — Review-driven fixes (2026-11-20)

- The gallery re-probes the dimensions of files rewritten by a rotate/flip, so
  a 90°/270° rotation no longer leaves the card footer / Details column
  advertising the pre-rotation resolution; the on-image info overlay re-reads
  its metadata as well.
- The Image Info overlay dedupes camera/lens by field identity instead of the
  rendered English label, fixing duplicate lines in Chinese and any other
  non-English UI.
- The Windows crash filter formats its report paths in static storage instead
  of 24 KB of locals on the faulting thread (a stack-overflow crash used to
  double-fault and lose the dump).
- The GitHub release link from the update response must now be a github.com
  release-tag URL before it reaches the browser, and `checkGitHub` validates
  the `owner/repo` argument.
- Linux plugin loading uses `RTLD_NOW | RTLD_LOCAL`; the macOS "reveal" actions
  use `open -R` instead of the non-existent `xdg-open`.
- Compare mode keeps `Tab`/`Shift+Tab` for keyboard focus traversal, the
  floating Metadata panel title/column-width behaviour is corrected, and a
  `.yuv` / `.raw` parameter edit refreshes the gallery thumbnail.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.182 — Image Info panel legibility (2026-11-20)

- The on-image Image Info panel (View → Image &info, `I` / `M`) now sizes itself
  from its content (300–520 px, capped by the viewer) and wraps long values
  instead of clipping them; the previous fixed 380 px box cut off full paths,
  colour-space names and long file names at the window edge.
- The panel fill is essentially opaque, so the photo no longer bleeds through
  and muddies the text; the histogram and the "Press I / ESC to close" hint
  share one layout and no longer overlap, and on a short viewer the histogram
  yields its height before the metadata is pushed off screen.
- The floating Metadata panel is resizable (480×600, minimum 340×400), opaque,
  and shows a wider value column instead of clipping long metadata values.
- Theme fix: item-view selection rules no longer repaint the view's `Base` role.
  Previously the selection colour turned alternating rows and the empty
  viewport accent blue (floating Metadata panel, plugin list) and the folder
  tree lost its sidebar fill.
- The sidebar preview caption elides long file names and the sample-luminance
  line instead of running past the panel edge.
- CI tooling: Dr.Memory advisory validation distinguishes real complete
  zero-error target reports from findings, unsupported infrastructure, timeout
  and no-report. A real Windows run launched bin64/drmemory.exe and retained
  target reports, but hit the 180 s deadline with partial findings; this is not
  a clean memory result. Release publication stays best-effort.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.181 — Browse publication and paint costs (2026-11-17)

- Cached ICC contexts avoid per-resize profile I/O/parsing with screen/WCS/CMS
  invalidation retained (PR #389). Sharp transition identity blits and safe
  local loading-bar paints preserve display quality (PR #390).
- Scan publication converts/inserts new runs, including middle insertions.
  Name/Date/Size/Type ascending/descending and suffix type filters stream
  sorted rows with current/selection identity and needed preloads retained.
  Stale scans check generation before exists (PR #392).
- Interior thumbnail insertion keeps viewport demand bounded (PR #393).
- Real concurrent preview/current-LOD/thumbnail DPR2 measurements retain full
  evidence; no scheduler, priority, pool or thread changes (PRs #391/#393).
- Resolution/metadata-dependent queries retain full-query barriers. ICC misses,
  Background metadata fallback, Compare rebuilds and foreground yield still
  have costs; offscreen CI does not establish physical-GPU smoothness.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.180 — Browse and preview reliability (2026-11-17)

- Promoted display rasters must match the current image and serial before
  they can drop the in-flight request (PR #382).
- Cached previews keep post-orientation source size (PR #383); a refused
  preview submit is retried and then shows a visible failure (PR #384).
- The first zoomed-out LOD is Sharp-resampled to the paint device-pixel dest
  (PR #385). Append-only folder scans keep still-needed neighbor preloads and
  warm rasters (PR #386).
- Queue wait vs decode is recorded for the current display LOD and neighbor
  preloads; scheduling is unchanged (PR #387).
- Known issue: the reported >=10-second UI stall when entering a folder
  remains unresolved. This release does not claim to fix it and does not
  treat it as PNG-specific.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.179 — Bounded decodes and load readiness (2026-11-16)

- Supported BMP/PNG and non-WIC strip TIFF have bounded downsampled decode
  (PRs #375-#377); top-down BMP opening and fallback labels are corrected
  (PR #378).
- Some above-LOD images no longer stay blank after neighbor-preload cancellation
  or folder-sequence churn (PR #380).
- sRGB-table shutdown lifetime and fixed-width EXIF signature ASan findings
  are corrected (PR #379).
- Known issue: the separately reported >=10-second single-PNG folder UI stall
  remains unresolved; this release does not claim to fix it.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.178 — Browse freeze hotfix (2026-11-12)

- Fixes the 1.0.177 freeze while browsing or switching folders: the first
  viewport preview now releases the gallery thumbnail stand-in, so the
  loading indicator stops repainting (PR #373, regression test V10).
- Report-cancellation workflow test drains every scheduler pool before its
  handle check (PR #372).
- Still open: bounded PNG / BMP / non-WIC TIFF downsampled decode (perf
  review P2-4), Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.177 — Image pipeline quality and performance (2026-11-12)

- Pipeline review 2026-11-12 (all 13 findings) and performance review
  (P1-1, P2-1, P2-2, P2-3, P2-5): linear-light Sharp fit to exact device
  pixels in the viewer, 16-bit dither, single-channel grayscale, YUV bilinear
  chroma / box luma / BT.2020 tag / source-matrix luma, optional LibRaw high
  quality (`raw/highQuality`), ICC-correct EXIF thumbnails.
- Faster opening: lazy full-frame analysis load (≤60 MP), viewport LOD for
  images above 4 MP (16-bit/RAW above 1 MP), viewport-sized neighbor warm,
  no Sharp overscan, strided `.raw` previews.
- Still open: bounded PNG / non-WIC TIFF downsampled decode (perf review
  P2-4), Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.176 — Headerless grayscale `.gray` / `.grey` (2026-11-12)

- ADR 034: `.gray` / `.grey` (any letter case) are headerless single-channel
  images (never LibRaw, no longer YUV). Defaults: 8-bit, 1× gain, sRGB gamma
  off; also 16-bit LSB/MSB containers (8–16 significant bits) and 1-bit
  (MSB-first, rows padded to whole bytes, 1 = white). Size from `WxH` in the
  file name, checked against the file size. Tools → Preferences → Gray
  (`grayRaw/defaults`) and a per-file Gray Parameters dialog. Browse group
  **Gray images (.gray/.grey)** is hidden by default (existing saved filters
  are migrated once).
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.175 — Plain `.raw` follow-up and RAW review (2026-11-11)

- Browse filter: `.raw` sensor dumps get their own **Sensor RAW (.raw)** group,
  visible by default (camera RAW group unchanged, hidden by default).
- 2026-11-11 RAW review (4 P1 + 11 P2) fixed: no silent bit-depth wrap (depth
  auto-detected on defaults, clipped + noted on overrides), defaults refresh
  every pane, USM save suffix, real load errors, multi-`WxH` names, no made-up
  dialog size, overrides follow rename/move/copy, async pixel readout, cheaper
  thumbnails, in-place banded render, source-image probe, neutral clipped
  highlights, every bit depth 8–16.
- Defaults unchanged: 16-bit LSB container, 10-bit, 1× gain, gray-world WB.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.174 — Plain `.raw` sensor-dump viewing (2026-11-11)

- ADR 033: `.raw` files are headerless Bayer/mono sensor dumps (never LibRaw).
  Size from `WxH` in the file name, checked against the file size with
  storage-format guessing; Preferences → RAW defaults (16-bit LSB container,
  10-bit, little endian, RGGB, gray-world WB, 1× gain, sRGB gamma) and a
  per-file RAW Parameters dialog; bilinear demosaic; sensor values in the pixel
  readout; viewer rotate/flip disabled for `.raw`.
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.173 — Project-review pass (2026-11-11)

- All findings of the 2026-11-11 project review except the Qt patch upgrade
  (P1-7): full zh_CN, Linux tests in the PR gate, release PDBs/symbols zip,
  RAW/TIFF parser 64-bit and byte-order fixes with nightly fuzzing, rating/tag
  backend removed (recently viewed in `recents.txt`), async batch rotate/flip,
  cache limits and Clear caches in Preferences, plugin DLL hardening, CI
  supply-chain hardening (SHA-pinned actions, provenance attestations).
- Still open: Authenticode signing (no certificate) and the Qt 6.8.x upgrade.

## 1.0.172 — Details zebra restore and rating/tag UI removal (2026-11-09)

- Details browse rows again use the v1.0.168 Base/AlternateBase zebra on every
  column, including single-column. Tertiary gallery QSS keeps the third column
  on the same stripe colors; selection stays Highlight. Transparent `::item`
  rules stay gone so gutters do not go black.
- Rating, color-label, pick/reject mark, and tag feature surface is removed:
  shortcuts, metadata-panel editors, browse filters, Details columns, thumbnail
  badges and tooltips, sort-by-rating, and the F1 cheat-sheet section. Folder
  Favorites and the recently-viewed filter remain. RatingStore/TagStore stay as
  inert persistence only. Coverage is in browse acceptance and theme/UI checks.

## 1.0.171 — Browse gallery content background (2026-11-09)

- Browse mode no longer paints a black field under gallery thumbs after the
  tertiary row-chrome fix: drop the over-broad transparent `::item` QSS (Qt
  paints `rgba(0,0,0,0)` into the opaque viewport as `#000000`) and stop pinning
  gallery `alternate-background-color` to content `bg0`. Tertiary pane/panel
  selectors and the flat Details unselected fill remain. Coverage is in the
  theme/UI model checks.

## 1.0.170 — Tertiary browse row backgrounds (2026-11-09)

- Three-column browse no longer paints blue/gray alternating unselected row
  backgrounds that look like selection: gallery QSS includes the tertiary pane,
  and Details uses a flat Base fill for unselected rows (same as List). Real
  selection highlighting is unchanged. Coverage is in the theme/UI model checks
  and browse acceptance suites.

## 1.0.169 — Triple-browse selection and layout restore (2026-11-09)

- Enabling three-column browse clears parked ThumbnailPanel pending/filter
  selection state when a pane is mirrored or hidden, so the rightmost pane no
  longer reselects stale files.
- Turning three-column browse off restores the interactive prior 1- or 2-column
  layout (session-restored triple still falls back to two columns). Coverage is
  in the existing browse acceptance suites; native display/DPI review remains
  separate.

## 1.0.168 — Three-column directory browsing (2026-11-08)

- Browse supports one, two or three independent directory panes, with a separate
  three-column toolbar/menu action, per-pane navigation and sorting, shared
  selection/Compare, transfer destination choice and saved session state.
- Single/two-column behavior and Ctrl+3 Details are preserved. Automated
  regression coverage is in the existing browse acceptance and workflow suites;
  native display/DPI and long-session visual review remain separate.

## 1.0.55 — Format conversion SIMD vectorization & search scalability (2026-09-18)

- **QtConvert SIMD Vectorization & Zero-Copy Fast-Paths:**
  - RGBA32 to ARGB32 format conversion accelerated via 256-bit AVX2 (`_mm256_shuffle_epi8`, 8 pixels/iter) and 128-bit SSSE3 (`_mm_shuffle_epi8`, 4 pixels/iter) vector channel swizzling with runtime CPU feature dispatch.
  - Native little-endian memory layout equivalence between BGRA32 and Qt's `Format_ARGB32` utilized for direct full-buffer or row-wise `std::memcpy`, replacing per-pixel scalar unpacking loops.
  - Contiguous full-buffer bulk `std::memcpy` fast-paths added for Grayscale8 and RGB24 when stride equals row width.
  - Direct RGB channel extraction from `Format_ARGB32` and `Format_RGB32` in `fromQImage`, bypassing temporary `QImage` allocations and Qt generic format conversion.
- **Search Scalability & Hash Indexing:**
  - `SearchIndex` upgraded with `std::unordered_map<std::string, size_t>` for $O(1)$ index lookup and $O(1)$ swap-and-pop removal, reducing large-gallery indexing from $O(N^2)$ to $O(N)$.
  - Added `reserve(size_t)` to pre-allocate index capacity during bulk directory scans.
  - Case-insensitive search optimized by hoisting search term lowercasing outside candidate iteration and scanning pre-lowercased blobs directly.
- **BatchProcessor Modular Refactoring:**
  - Decomposed `BatchProcessor::processFile` into single-responsibility static step helpers (`applyAnalyzeOp`, `applyCropOp`, `applyResizeOp`, `applyWatermarkOp`, `applyExportOp`), reducing function span to 46 lines and retiring tracked debt from `complexity_gate.ps1` and ADR-014.
- **Unit Tests:**
  - Added new `test_qtconvert` test suite with 8 test groups covering null guards, Grayscale8/RGB24/BGRA32 fast paths, RGBA32 SIMD swizzling across aligned and unaligned odd dimensions, non-owning `toQImageRef`, and round-trip fidelity.
  - Expanded `test_search` with 5,000-entry indexing performance tests, $O(1)$ in-place update validation, case-folding search, and snippet integrity tests.

## 1.0.30 — Review hardening (2026-09-13)

- **Bounded everything a file can drive:** EXIF/ICC offsets are range-checked
  (a crafted 8-byte `.tif` used to dereference a wild pointer), an unterminated
  JSON array no longer spins the parser, workspace/settings/session/preset/ICC
  reads have size caps, animation probes and contact sheets are bounded, and a
  rename pattern can no longer write outside the export folder.
- **No unbounded waits or silent losses:** repository cancellation and disk-cache
  shutdown are bounded, per-thread SQLite connections are released, a throwing
  analyzer is reported instead of producing nothing, and the clipboard keeps its
  own pixels after an export.
- **Delivery is deterministic:** core callbacks go through an explicitly
  installed main-thread dispatcher instead of sniffing `QCoreApplication`, and
  batch runs report each file as it finishes.
- Compare's dead asynchronous diff transport (no production subscriber) removed;
  `docs/coding_style.md` now describes the code that exists.
- Evidence: ADR 016 (Qt boundary in core headers, enforced by the architecture
  gate), ADR 017 (process-lifetime services and teardown policy), and the new
  `teardown_stress_tests` in `.\build.ps1 Test`.

## 1.0.29 — Larger Compare filenames (2026-09-12)

- Compare pane captions and filename overlays use a larger bold font.

## 1.0.28 — User guide and P to Compare (2026-09-12)

- Selecting 2–8 images and pressing P (or C) opens Compare. Help → User Guide
  and `docs/USER_GUIDE.md` cover Compare and shortcuts.

## 1.0.27 — Compare contextual chrome (2026-09-12)

- Overlay/checker/threshold sliders and custom column count show only when
  relevant. Analyze opens the Compare inspector. ? help no longer replaces
  the window title.

## 1.0.26 — Compare chrome and ROI A/B (2026-09-12)

- Fullscreen Compare insets content away from the taskbar and screen edges.
- ROI boxes are tagged A/B; the measurement HUD stays fully visible.
- PSNR/SSIM uses a dark high-contrast strip; sync-zoom is no longer clipped.

## 1.0.25 — Console launch (2026-09-11)

- MViewer.exe is a console-subsystem binary again (CMD window on Explorer
  launch), matching 1.0.22, to reduce unsigned-GUI antivirus quarantine.

## 1.0.24 — Larger histograms and Compare fullscreen (2026-09-11)

- Compare overlay histograms are large enough to read; analysis, inspection,
  and metadata histograms grew with them.
- Compare opens as a true top-level fullscreen window with no dialog inset.

## 1.0.23 — Compare chrome and silent launch (2026-09-11)

- PSNR/SSIM HUD, auto-align, enabled A/B swap, and an exit button sit on the
  Compare status strip. Diff metrics/export work for 2–8 images vs reference.
- Browse analysis is Alt+H; Compare crosshair is R. Analysis panel is Chinese.
- Browse toolbar uses the application icon family. Packaged MViewer.exe no
  longer opens a console window.

## 1.0.22 — Faster Compare histograms (2026-09-11)

- Overlay and full-image inspection histograms subsample to a 256-pixel long
  edge. ROI analysis histograms remain exact pixel counts.
- The application, window, and installer ship a dedicated MViewer icon.

## 1.0.21 — Compare overlay inspection (2026-09-11)

- Compare adds toolbar checkboxes for a transparent top-left histogram overlay
  and a wrapping filename overlay on each pane, including canvas modes.

## 1.0.20 — Professional inspection (2026-09-11)

- Viewer and Compare isolate R/G/B/Y as grayscale presentation overlays.
  Analysis, Pixel Inspector, and ROI measurement stay on source RGB.
- A source-pixel grid appears at 800% zoom and above in Viewer and Compare,
  including Split/Overlay/Swipe/Checkerboard canvas modes.
- Compare visual diffs and PSNR/SSIM honor `autoAlignBeforeDiff`, and the
  metrics label reports the detected integer translation.

## 1.0.19 — Browse/Compare/ROI correctness patch (2026-09-05)

- Viewer metadata overlay is `I` / `M` / Image Info only. Left-click and hover no
  longer cover the image and steal pan or Fit↔100% double-click.
- Compare toolbar/menu/`C` require a 2–8 image selection, matching Compare Selected.
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
  and invalid (never the display LOD) for infeasible ones; diff/PSNR/SSIM on an
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
  Trash**, a per-user reversible staging directory. Native Windows Recycle Bin
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
- Gate: CTest `version_consistency` fails when the generated
  `build_msvc/version_info.txt` or `CMakeLists.txt` `project(VERSION)`
  disagrees with the expected version, when `src/` hard-codes a `1.0.x`
  version literal, when `scripts/package_release.ps1` /
  `scripts/package_portable.ps1` stop reading `version_info.txt`, or when
  STATUS.md does not mention the current version. It never reads the `.nsi`.
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
- Final Release build and the full registered CTest suite are green, including
  golden, benchmark-enforcement, workflow, Browse/Compare acceptance, and async
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
  source-backed core tests are green. Canonical CTest run #1 passed the full
  registered CTest suite; runs #2 and #3 also passed; three-run evidence is
  maintained in the M43 review report.
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
- The full registered CTest suite passed in three consecutive runs, including
  golden-image, benchmark-enforcement, lifecycle, workflow, Browse, and Compare
  acceptance gates. Full evidence is recorded in
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
  `Show Diff` state controls visualization.
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
- Thumbnail schema 4, Preview scaled metadata, ImageViewer CPU/GPU tile
  materialization and Compare use one ICC display-copy contract. Analysis
  pixels remain unchanged. `m36_display_tests` covers sRGB, AdobeRGB,
  Display-P3 and no-profile samples.
- Compare has one host per MainWindow with host-bound queued loads. Sync
  persistence retains Off/Zoom/Drag/All and toggles no longer Fit/reset the
  viewport. Batch analysis runs sequentially in a cancellable bounded worker.
- Generated CTest registration is **132 tests**. The local focused gate
  passes the M36 display, Compare, lifetime and RatingStore suites. The full
  baseline remains environment-blocked by restricted temporary/cache file
  writes in existing workflow/cache tests; see the M36 evidence report.

- **Decode**: `DecoderRegistry` dispatches to `QtDecoder` (JPEG/PNG/BMP/TIFF/…),
  `RawDecoder` (embedded-JPEG preview for CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF/…,
  graceful fallthrough), and `QtFallbackDecoder`. RAW display prefers the
  embedded JPEG; LibRaw 0.21.4 (LGPL-2.1 / CDDL-1.0) supplies half-size demosaic
  when the preview is short, and full demosaic only when no preview exists.
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
  dumps in `%APPDATA%/MViewer/crash-reports/`); one-time crash-report dialog on
  next launch; `--selftest` headless decode→metadata gate (CTest `selftest`).
- **Update check**: `UpdateChecker` (WinHTTP, GitHub Releases), Help menu
  "Check for Updates" + silent check 8 s after startup; current version from
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
  `release.yml` (perf report / UI regression / package / release; Dr.Memory
  execution disabled/skipped, no memory verdict).
- **Packaging**: portable ZIP + NSIS installer; SHA256SUMS + auto release
  notes (`scripts/release_manifest.ps1`); both artifacts launch on a clean
  Windows (PATH stripped) — verified 2026-08-05.

## Deferred / future (not shipping now)

- GPU Stage B (`scaleRegion` on the GPU) and Stage C/D: custom shaders,
  multi-pass, D3D11/Vulkan direct compositing
  (UI boundary frozen).
- Linux/macOS native installers (Linux CI artifacts build; only Windows ships
  an installer).
- AI workflows (captioning, embeddings, similarity search) — M18, planned.

## Known gaps (honest, not hidden)

- RAW without a usable preview and without a LibRaw open (or a build with
  `MVIEWER_WITH_LIBRAW=OFF`) still falls through. LibRaw here has no external
  libjpeg/zlib, so some DNG deflate/lossy files still fail open.
- GPU Stage A is opt-in (`MVIEWER_GPU=1` or the `gpuAcceleration` setting);
  default is the CPU tile path. Stage B is not implemented.
- 100 MP first-viewport fill measured 1480 ms on a 2-core Xeon VM (vs 392.8 ms
  on the 2026-07-24 baseline box) — B10 is report-only by design
  (`performance_budget.json`); regression axis is `--regression`.
- C4819 warnings in a few sources (UTF-8 without BOM) and C4530 in some
  test/bench TUs are tracked in Phase 6 of M24.
- Installer/portable ship no `plugins/` directory; on first run the app now
  creates an empty plugin home next to the executable (third-party
  plugins can be dropped in later; startup log stays informative).

## Product-force progress (2026-08-05 state)

| Work item | Status | Notes |
|-----------|--------|-------|
| A-1~A-10 reconciliation | ✅ | `docs/review/A_ITEMS_COMPLETION_AUDIT_2026-07-24.md` (~91%) |
| M16 Compare wrap-up | ✅ | Pixel Link / Overlay opacity / Diff highlight / 1–8 layout presets / edit↔metrics linkage / per-pane histogram overlay |
| Browse gate | ✅ | Unified Selection + progressive fetch for large directories + predictive thumbnail window at 10k+ scale |
| M19 UI Model convergence | ✅ | `DirectoryModel` / `ImageListModel` / `WorkspaceModel` / `AnalyzerModel`; Metadata Overlay `I`/`ESC` + Lens/ICC |
| M20 Compare keyboard flow | ✅ | Ctrl+2/4/8 layout presets; B/S/W/O/H/Z/D/C/L mode keys; continuous navigation keeps mode/ROI; `?` shortcut help |
| M21 Analysis+Export | ✅ | AnalysisPanel↔AnalyzerModel History/Pin; unified `ExportJob` path; Memory Timeline; Dashboard |
| Workspace restore | ✅ | Layout/zoom/Compare/crash recovery (A-6 landed) |
| M17 high-value sub-items | ✅ | Batch analysis export / plugin rescan / rating-filtered export |
| M16 analysis persistence | ✅ | `AnalyzerModel` history/pin/result serialization retained across restarts |
| M17 auto-update | ✅ | `UpdateChecker` (GitHub Releases, WinHTTP); version from CMake SSOT (M24); offline `updatechecker_tests` cover JSON, dotted versions, transport errors, and safe download URL policy |
| M23 quality automation | ✅ | ADR/Bug gates, auto dashboard, test matrix, benchmark trend (ADR-015) |
| M24 stability convergence | ✅ | Done 2026-08-05: 70/70 CTest, clean-env packaging, async lifetime / workflow acceptance / test credibility / performance / RC verification (verdict: READY WITH NON-BLOCKING LIMITATIONS) |
| M25 RC convergence | ✅ Done (2026-08-09) | Compare top bar three-row overflow fixed; `thumbnailpanel.cpp` 682 lines (within ADR-014); S1–S9 + T1–T4 Release stress PASS on both configs; 73/73 local gates (added `browse_convergence_tests`) |
| M26 RC reliability closure | ✅ Automated done | Done 2026-08-09: **78/78 CTest green**. TaskScheduler exactly-once finalize (deadline/cancel/deferred/reject paths), reverse-dep cancelTree (transitive) |
| M27 async lifetime wrap-up | ✅ Automated done | Done 2026-08-10: **84/84 CTest green**. TaskScheduler fault injection (work/onProgress/done exceptions contained, empty work rejected, cancelTree suppresses done, drain wall-clock, graphMetrics) |

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
2. `.\build.ps1 Test` must be green (currently 132 registered CTest tests incl.
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
