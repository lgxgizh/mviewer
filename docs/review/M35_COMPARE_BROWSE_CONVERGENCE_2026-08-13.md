# M35 Compare / Browse Convergence Evidence

Date: 2026-08-13

## Root causes closed

- Difference analysis and visualization were coupled; a zero-difference
  heatmap is blue and was applied at 0.5 opacity by default.
- Sync Zoom reused Uniform Pixel Scale's absolute-scale behavior, shrinking
  smaller/different-resolution images instead of sharing a relative Fit ratio.
- newly rebuilt panes were fitted before the layout assigned final geometry.
- MainWindow opened Compare at 1000x700 instead of fullscreen.
- PreviewPanel cleared every presentation before scheduling its next scaled
  preview, and warm cache access still waited behind Thumbnail work.
- QImage profiles were summarized then discarded before display materialization.
- `build.ps1 Test` warned on CTest failure but returned success.

## Resulting contracts

- Ordinary Compare has no diff overlay; explicit visualization and background
  metrics are independent. Latest-wins generation remains in force.
- Each pane fits independently. Sync Zoom applies a shared relative ratio;
  Uniform Pixel Scale is the only absolute-equality mode.
- A single coalesced post-layout Fit follows pane rebuilds; real Compare opens
  fullscreen and retains Escape close behavior.
- Preview immediately reuses a warm gallery thumbnail, retains a prior frame
  on cold miss, and atomically upgrades through the existing cancellable task.
- Analysis pixels are unchanged by color management. The shared Compare display
  materializer consumes embedded ICC from a metadata sidecar and emits sRGB.
- The local canonical test command propagates CTest's exit code.

## Verification evidence

- Canonical local gate (`build.ps1 Test`): 86/86 PASS, 0 failures, 418 s.
- ICC/display contract: 19/19 PASS, including AdobeRGB reference conversion,
  no-profile fallback and analysis-byte immutability.
- M27 lifetime/Preview latency: 30/30 PASS. 24 MP preview worst UI event call
  measured 9.8 ms; viewer materialization 20.5 ms (budgets: 250/500 ms).
- Compare acceptance/session/build-exit focused tests: PASS.
- Workflow M35 checks pass: default overlay absent; explicit overlay and
  threshold latest-wins; warm thumbnail usable synchronously; async upgrade;
  mixed-size per-pane Fit, relative zoom and Uniform opt-in contracts; all pane
  rebuild routes use a coalesced post-layout Fit.
- Standalone M24 soak: PASS. 10k first entries 743 ms; directory-switch worst
  527 ms; 50 Compare rounds completed with a 335 ms worst round; 24 MP Preview
  UI gap 5.01 ms. Destroy-mid-decode, close-under-load, rejection recovery and
  scheduler/thumbnail convergence all passed; final pending/active/handles = 0.
- The soak's close-under-load assertion now samples only after stack-owned
  MainWindow destruction and uses a bounded cooperative drain. Its former
  `close()`-then-immediate-sample ordering could intermittently fail before the
  lifecycle boundary despite the same run subsequently converging.
- UX Review Agent verdict: PASS for the automated M35 Compare scope. Native
  fullscreen transitions, display/DPI combinations and perceived zoom/flicker
  remain explicit human-on-hardware checks below.

## Before / after

| Flow | Before | After |
| --- | --- | --- |
| Compare display | Difference heatmap tinted non-base panes by default | Original display is default; overlay is explicit and reversible |
| Compare Fit | Sync Zoom forced one absolute scale and early Fit could see provisional geometry | Per-pane final-geometry Fit multiplied by one shared relative ratio |
| Compare host | Fixed 1000x700 dialog | Fullscreen on first presentation; Escape still closes |
| Warm Preview | Synchronously cleared, then waited in the Thumbnail queue | Existing QPixmap is usable before `setImage` returns (zero event-loop hops) |
| Cold Preview | Blank frame until decode delivery | Previous valid presentation remains until the latest generation upgrades |
| Color | Profile summary survived but display pixels were untagged | Compare display copy converts embedded ICC to sRGB; analysis bytes stay identical |
| Test gate | CTest failure emitted a warning and could exit 0 | Native nonzero exit is captured and propagated unchanged |

## Changed-file map

- Compare behavior/tests: `src/compareworkspace*.{h,cpp}`, `src/mainwindow.cpp`,
  `src/test_workflow_ux.cpp`.
- Preview behavior/tests: `src/previewpanel.{h,cpp}`, `src/mainwindow.cpp`,
  `src/test_m27_lifetime.cpp`, `src/test_workflow_ux.cpp`.
- Display color/tests: `src/core/image/{QtConvert,MetadataReader,ImageRepository}.cpp`,
  the Qt decoders, ICC/repository tests and the metadata presentation filter.
- Gates/stress: `build.ps1`, `scripts/build_test_exit_gate.ps1`,
  `src/CMakeLists.txt`, `.github/workflows/ci.yml`, and
  `benchmarks/m24_soak_main.cpp`.
- SSOT: `STATUS.md`, `CHANGELOG.md`, the Compare/Preview specs, image pipeline,
  beta/release checklists and this evidence report.

## Remaining release risks

- Color-managed display is currently wired to Compare's pane materialization.
  The full tile-based ImageViewer and scaled Preview render paths still require
  the same helper to claim application-wide display parity.
- Disk-cache pixels intentionally do not persist ICC bytes. A bounded metadata
  header refresh reconstructs the profile sidecar on disk hits, preserving the
  frozen cache schema and cross-process display fidelity.
- Human UX review on a real display remains required for perceived flicker,
  zoom feel and long-session behavior.
