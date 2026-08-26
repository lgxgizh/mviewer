# M49 — Windows UX / Unicode / Compare Reliability Closure

Date: 2026-08-27
Version line: 1.0.10
Scope: Windows filesystem identity, Browse selection ownership, Compare layout
semantics, and momentary Compare presentation.

## Root causes

- `std::filesystem::path(std::string)` and native `.string()` conversions were
  used at several UTF-8/Windows boundaries. This could corrupt non-ASCII names
  or throw while a Qt callback was still on the stack.
- Sidecar import ran synchronously from directory navigation, and Browse had
  more than one path capable of publishing gallery selection into the shared
  `SelectionModel`.
- Compare pane captions participated in layout sizing, and the UI exposed an
  editable row value even though the engine derives rows from image count and
  column count.
- The old Space behavior reused continuous Blink state. It could not provide a
  display-only, reversible A←B presentation without mutating Compare state.

## Final implementation

- `core/filesystem/Utf8Path.{h,cpp}` is the single Qt-free UTF-8/native path
  adapter. Core persistence, image discovery, metadata, batch/export, plugin,
  startup, and command filesystem paths use it explicitly.
- Atomic file, sidecar, rating, and tag operations use non-throwing iterator /
  filesystem overloads where available and contain expected failures at the
  boundary. Sidecar directory import is cancellable background work owned by
  MainWindow's existing scheduler/lifetime pattern; no worker callback captures
  MainWindow.
- `MViewerApplication::notify()` is a last-resort `std::exception` / unknown
  exception firewall. It logs the Qt receiver/event boundary and returns false;
  it does not replace the lower-level filesystem error handling.
- `ThumbnailPanel` is the sole gallery-to-`SelectionModel` publisher. Real
  plain/Ctrl/Shift mouse gestures use the panel's path anchor for stable range
  behavior across Windows IconMode styles; programmatic restore publishes once.
- Default two-pane Compare uses expanding pane containers and an elided,
  tooltip-backed caption. The layout status is `R rows × C cols`; custom mode
  persists only the column count and derives rows.
- `RawImageView` supports a display-only transient raster override. The
  `temporaryCompareButton` and Space key share `beginTemporaryCompare()` /
  `endTemporaryCompare()`. Cleanup covers release, key release, focus/window
  deactivation, mode changes, image replacement, disable, and destruction.
  Engine image order, selection, ROI, analysis, session, and continuous Blink
  state are not changed.

## Regression coverage

- Unicode nested directory with Chinese/spaces/emoji path components, a
  non-ASCII image filename, RatingStore round-trip, sidecar write/read/remove,
  missing-sidecar behavior, and identity preservation.
- MainWindow-backed plain, Ctrl-toggle, Shift-range, Ctrl-off selection checks
  against both `ThumbnailPanel::selectedPaths()` and `SelectionModel::selection()`.
- Compare acceptance checks for long Unicode captions, exact equal pane widths,
  full available-area coverage, columns-driven row text, button availability,
  A←B pressed/released raster behavior, Space reuse, and state immutability.
- Existing workflow UX, Compare, persistence, lifecycle, golden-image,
  architecture, complexity, and performance gates remain enabled.

## Verification

Targeted post-clean check:

```text
ctest --test-dir build_msvc -R ^workflow_ux_tests$ --output-on-failure
PASS — 1/1, 37.19 s
```

The final release evidence below is updated only from the project-mandated
`powershell -ExecutionPolicy Bypass -File .\build.ps1 Test` runs after the
version/documentation changes. No test is skipped or weakened.

Final full gate A:

```text
powershell -ExecutionPolicy Bypass -File .\build.ps1 Test
PASS — 105/105, 630.61 s
```

Final full gate B:

```text
powershell -ExecutionPolicy Bypass -File .\build.ps1 Test
PASS — 105/105, 629.13 s
```

Both final runs included the benchmark, workflow UX, golden-image,
architecture, complexity, and performance gates.

## Native Windows qualification

Automated offscreen tests are not a substitute for native desktop review.
Physical-DPI, mixed-DPI, native dialog, long-session visual smoothness, and
real hardware ICC checks are **MANUAL / BLOCKED** until executed on the target
Windows desktop. This review does not claim those rows passed.

## Remaining risks

- Native Windows visual qualification remains outstanding as stated above.
- The UTF-8 audit covers user-content path boundaries in the current product
  flow; ordinary text serialization and non-path `std::string` conversions are
  intentionally not mechanically rewritten.
- Existing benchmark and long-session gates remain the guard against regressions
  in memory growth and scheduler convergence.
