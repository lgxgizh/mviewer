# ADR 014: Split god-object UI translation units by responsibility

Date: 2026-07-28
Status: Accepted

## Context

After the M14–M22 product milestones, the three biggest UI files had absorbed
most of the application's workflow logic and were turning into god objects:

| File                  | Before (lines) | Roles it had absorbed                                            |
| --------------------- | -------------- | ---------------------------------------------------------------- |
| `mainwindow.cpp`      | 3770           | UI construction, commands, shortcuts, session, recent, export    |
| `compareworkspace.cpp`| 2598           | rendering, blink/diff, edit panel, presets, navigation, input    |
| `thumbnailpanel.cpp`  | 1893           | view, filtering/sorting, metadata index, file ops, delegates     |

External review (2026-07) flagged this as the project's largest technical debt
and set hard targets: MainWindow < 1000, CompareWorkspace < 800,
ThumbnailPanel < 800 lines.

## Decision

Phase 1 of the convergence plan is a **behavior-preserving split of each class
implementation into responsibility-scoped translation units**, sharing one
private header per class (`*_p.h`) that owns the include set and the few
file-local helpers that must be visible to more than one TU (now `inline`).

- `mainwindow.cpp` (core wiring, 860 lines) +
  `mainwindow_ui.cpp` (setupUi) · `mainwindow_commands.cpp` (commands +
  keyboard dispatch) · `mainwindow_navigation.cpp` (history / recent /
  favorites) · `mainwindow_session.cpp` (workspace / project / autosave /
  recovery / update check) · `mainwindow_session_notifications.cpp` (update /
  crash notifications) · `mainwindow_export.cpp` (report + image export) ·
  `mainwindow_ui_layout.cpp` (widget / menu / dock / status layout) ·
  `mainwindow_ui_connections.cpp` (signal wiring) ·
  `mainwindow_view.cpp` (drag&drop, overlays, fullscreen, slideshow, status).
- `compareworkspace.cpp` (cells / layout / loading, 770 lines) +
  `compareworkspace_analysis.cpp` (histograms / metrics panels) ·
  `compareworkspace_controls.cpp` (toolbar + mode controls) ·
  `compareworkspace_display_planner.cpp` (fit/LOD planning, free functions) ·
  `compareworkspace_editpanel.cpp` (adjustments, metrics, presets) ·
  `compareworkspace_interact.cpp` (keyboard / mouse / pixel link) ·
  `compareworkspace_nav.cpp` (pair navigation, layout presets, session apply) ·
  `compareworkspace_render.cpp` (paint modes, canvas host) ·
  `compareworkspace_render_canvas.cpp` (blink controller, canvas paint) ·
  `compareworkspace_render_diff.cpp` (diff batch + metrics) ·
  `compareworkspace_render_materialization.cpp` (cell raster materialization) ·
  `compareworkspace_roi.cpp` (ROI box, HUD, measurement export).
- `thumbnailpanel.cpp` (view core, 637 lines) +
  `thumbnailpanel_async.cpp` (scan + dimension-probe workers) ·
  `thumbnailpanel_selection.cpp` (selection / focus / scrolling) ·
  `thumbnailpanel_pipeline.cpp` (visible-range scheduling / thumbnail delivery) ·
  `thumbnailpanel_filters.cpp` (filters, sorting, metadata index) ·
  `thumbnailpanel_fileops.cpp` (rename / trash / copy / move / batch export) ·
  `thumbnailpanel_delegates.cpp` (thumb / details / list delegates) ·
  `thumbnailpanel_live.cpp` (incremental live-folder deltas) ·
  `thumbnailpanel_viewmode.cpp` (view-mode configuration, split out on the
  M23 re-check when the core TU had crept back to 826 and breached the 800
  guard).

`*_p.h` carries the authoritative TU map for each class, with the current line
count of every sibling TU, so "which file owns this?" is answered in one place.

## Budget enforcement

The line budgets are not prose: `scripts/complexity_gate.ps1` (a CTest entry,
`complexity_gate_regression`) counts physical lines and fails the build on:

- `mainwindow.cpp` > 1000, `compareworkspace.cpp` > 800, `thumbnailpanel.cpp` > 800;
- any `mainwindow_*.cpp` / `compareworkspace_*.cpp` / `thumbnailpanel_*.cpp` > 1000
  (warned from 800);
- any function > 120 lines or cyclomatic complexity > 25, and any class > 1000 lines
  (warn).

The sibling cap is what keeps this ADR honest: the caps on the *seed* files alone
were satisfied by relocating code. Three siblings are currently between the 800
warning and the 1000 failure and are tracked debt, not accepted state:
`mainwindow_session.cpp` (990), `compareworkspace_interact.cpp` (947),
`mainwindow_ui_layout.cpp` (897).

No public header changed; no behavior changed. The private headers are an
implementation detail and may only be included by their class's TUs.

The first **Phase 2 class extraction** is `ThumbnailProvider`
(`thumbnailprovider.{h,cpp}`): the decode → square-fit → on-disk-cache policy
that previously lived inside `ThumbnailPanel`'s `ThumbnailPipeline` decode/result
lambdas. It is a stateless, worker-thread-safe class; `ThumbnailPanel` now only
routes the finished pixmap into its per-panel ready map and owns lifecycle. This
is a true class (state/behavior boundary), not a TU split — see the Consequences
note on how it differs from the Phase-1 work.

Remaining Phase 2 (future, separate ADRs): promote cohesive TUs into real
controller classes (e.g. `CompareController`, `SessionController`) once their
state boundaries have stabilized — extracting state before behavior has settled
would churn signals/ownership for no product gain.

## Consequences

- All three review targets are met (860 / 770 / 637 physical lines).
  The core `thumbnailpanel.cpp` returned below the 800 guard by keeping
  `setViewMode` in `thumbnailpanel_viewmode.cpp` and selection/navigation in
  `thumbnailpanel_selection.cpp`.
- Phase 2 has begun incrementally (per the reviewer's "abc 都需要" directive):
  `ThumbnailProvider` is the first real class extracted from a god object, not
  just a TU split. It removes the thumbnail-production knowledge (decode,
  square-fit, on-disk cache) from `ThumbnailPanel`, leaving the panel to own only
  where finished thumbnails land and the widget lifecycle. The core TU now stands
  at 637 lines; `ThumbnailProvider` adds 31 (header) + 55 (impl) lines of
  genuinely reusable, testable logic.
- Each responsibility is now independently reviewable and diffable; merge
  conflicts across unrelated features disappear.
- The include cost of `*_p.h` is paid by every TU of that class; acceptable
  because member TUs previously included the same superset anyway.
- Adding a new MainWindow/CompareWorkspace/ThumbnailPanel method requires
  choosing the right TU — the banner comment in each `*_p.h` is the map.
