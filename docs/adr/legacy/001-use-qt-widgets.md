# ADR 001: Use Qt Widgets for the UI layer

## Status
Accepted

## Context
MViewer is a desktop image viewer/comparison tool. We need a mature,
cross-platform widget toolkit with native file dialogs, layout managers,
and painting primitives.

## Decision
The `ui/` layer is built exclusively on **Qt 6 Widgets** (`QWidget`,
`QGridLayout`, `QPainter`, etc.). Qt is confined to `ui/` and selected
core `.cpp` implementation files; it never leaks into domain headers.

## Consequences
- Fast native UI with minimal custom code.
- Core business logic stays Qt-free and unit-testable headless
  (`QT_QPA_PLATFORM=offscreen`).
- UI ↔ core boundary converts explicitly (e.g. `mvcore::toQImage`).
