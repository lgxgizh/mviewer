# ADR-M22.1: Centralized Preferences Dialog

## Status

Proposed (DRAFT, companion to `docs/rfc/M22_PRODUCT_POLISH.md` §F1)

## Context

Configurable options are persisted via `QSettings` but scattered across
`mainwindow`, `imageviewer`, `exportcommand`, `pluginsettings`, reachable only
through inline menus. There is no single Preferences surface, and the new
toggles from F2/F3/F4 have nowhere consistent to live.

## Decision

Add a tabbed `PreferencesDialog` (Qt Widgets) that reads/writes the *existing*
`QSettings` keys plus a small number of genuinely new keys. No new persistence
layer is introduced; `QSettings` remains the single source of truth.

## Rationale

- Centralizes discovery of options for professional users.
- Gives F3 (`autoAlignBeforeDiff`) and F4 (`defaultAnalysisOverlay`) a natural home.
- Reuses the already-working persistence path; near-zero risk.

## Consequences

- ✅ One place to configure the app; new feature toggles get a home.
- ✅ Live-apply where feasible (view/compare/analysis re-read QSettings).
- ❌ Some legacy inline controls become redundant (leave them; non-breaking).

## Related

- RFC M22 §F1
- `src/mainwindow.cpp` (QSettings usage), `src/core/SettingsIO.*` (import/export)
