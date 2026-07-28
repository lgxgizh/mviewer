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

- `mainwindow.cpp` (core wiring, 585 lines) +
  `mainwindow_ui.cpp` (setupUi) · `mainwindow_commands.cpp` (commands +
  keyboard dispatch) · `mainwindow_navigation.cpp` (history / recent /
  favorites) · `mainwindow_session.cpp` (workspace / project / autosave /
  recovery / update check) · `mainwindow_export.cpp` (report + image export) ·
  `mainwindow_view.cpp` (drag&drop, overlays, fullscreen, slideshow, status).
- `compareworkspace.cpp` (cells / layout / loading, 788 lines) +
  `compareworkspace_render.cpp` (paint modes, diff overlay, blink, histograms) ·
  `compareworkspace_editpanel.cpp` (adjustments, metrics, presets) ·
  `compareworkspace_interact.cpp` (keyboard / mouse / pixel link) ·
  `compareworkspace_nav.cpp` (pair navigation, layout presets, session apply).
- `thumbnailpanel.cpp` (view core, 670 lines) +
  `thumbnailpanel_filters.cpp` (filters, sorting, metadata index) ·
  `thumbnailpanel_fileops.cpp` (rename / trash / copy / move / batch export) ·
  `thumbnailpanel_delegates.cpp` (thumb / details / list delegates) ·
  `thumbnailpanel_viewmode.cpp` (the 150-line `setViewMode`, split out on the
  M23 re-check when the core TU had crept back to 826 and breached the 800
  guard).

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

- All three review targets are met (584 / 787 / 670 lines); the core
  `thumbnailpanel.cpp` is kept strictly under the 800 guard by isolating
  `setViewMode` in `thumbnailpanel_viewmode.cpp`.
- Phase 2 has begun incrementally (per the reviewer's "abc 都需要" directive):
  `ThumbnailProvider` is the first real class extracted from a god object, not
  just a TU split. It removes the thumbnail-production knowledge (decode,
  square-fit, on-disk cache) from `ThumbnailPanel`, leaving the panel to own only
  where finished thumbnails land and the widget lifecycle. The core TU dropped
  from 680 → 670 lines as a side effect; `ThumbnailProvider` adds 34 (header) +
  48 (impl) lines of genuinely reusable, testable logic.
- Each responsibility is now independently reviewable and diffable; merge
  conflicts across unrelated features disappear.
- The include cost of `*_p.h` is paid by every TU of that class; acceptable
  because member TUs previously included the same superset anyway.
- Adding a new MainWindow/CompareWorkspace/ThumbnailPanel method requires
  choosing the right TU — the banner comment in each `*_p.h` is the map.
