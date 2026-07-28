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
- `thumbnailpanel.cpp` (view core, 817 lines) +
  `thumbnailpanel_filters.cpp` (filters, sorting, metadata index) ·
  `thumbnailpanel_fileops.cpp` (rename / trash / copy / move / batch export) ·
  `thumbnailpanel_delegates.cpp` (thumb / details / list delegates).

No public header changed; no behavior changed. The private headers are an
implementation detail and may only be included by their class's TUs.

Phase 2 (future, separate ADRs): promote cohesive TUs into real controller
classes (e.g. `CompareController`, `SessionController`) once their state
boundaries have stabilized — extracting state before behavior has settled
would churn signals/ownership for no product gain.

## Consequences

- All three review targets are met (585 / 788 / 817 lines).
- Each responsibility is now independently reviewable and diffable; merge
  conflicts across unrelated features disappear.
- The include cost of `*_p.h` is paid by every TU of that class; acceptable
  because member TUs previously included the same superset anyway.
- Adding a new MainWindow/CompareWorkspace/ThumbnailPanel method requires
  choosing the right TU — the banner comment in each `*_p.h` is the map.
