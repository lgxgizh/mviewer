# ADR 003: Command pattern for user actions

## Status
Accepted

## Context
User actions (open directory, compare, rename, delete, toggle histogram)
should be uniform, scriptable, and extensible (command palette + shortcuts).

## Decision
Every action implements `core/command/ICommand` (`id()`, `shortcuts()`,
`execute()`). Commands are registered in `CommandRegistry` (a singleton)
and dispatched by id or shortcut. `CommandPalette` (UI) lists all registered
commands.

## Consequences
- Uniform invocation path for menu, keyboard, and palette.
- Easy to add new commands without touching call sites.
- Commands are decoupled from `QAction`/widget lifecycle.
