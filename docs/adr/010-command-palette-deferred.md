# ADR 010: Defer Command Palette UI to Milestone 7

## Status
Accepted

## Context
ADR-003 specifies a `CommandPalette` widget that lists all registered commands.
The command infrastructure (`ICommand` + `CommandRegistry`) is frozen in M2
(Milestone 2 — architecture solidification). Per the architect's directive,
M2 must stop adding features and solidify infrastructure only. `roadmap.md`
already schedules CommandPalette for **M7** (together with AnalysisPanel
integration), not M2.

## Decision
Build only the command **infrastructure** in M2:
`ICommand`, `CommandRegistry`, and the five seed commands (OpenDirectory,
Compare, Rename, Delete, ToggleHistogram). These are frozen and constitute
M2's deliverable for this feature area.

Defer the `CommandPalette` **UI widget** (a `QWidget` listing registered
commands, filterable by name, invoked by id) to M7. When built, it will
consume `CommandRegistry::allCommands()` — no interface change is needed.

## Consequences
- M2 closes with all *infrastructure* frozen and no outstanding feature debt.
- The `CommandRegistry` surface in M2 is forward-compatible: Palette is purely
  a read-only consumer and cannot break the frozen interface.
- ADR-003's "CommandPalette (UI) lists all registered commands" is interpreted
  as *intended future behavior*; ADR-010 records the milestone split.
- M7 deliverable is well-scoped: one new widget, zero interface churn.
