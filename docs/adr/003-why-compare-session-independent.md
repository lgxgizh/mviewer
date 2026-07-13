# ADR-003: Why CompareSession is Independent

## Status
Accepted

## Context
Comparison state (which images, layout, sync transform, blink state) could live inside CompareEngine or be separate. Encapsulating it in an independent value type makes the architecture cleaner.

## Decision
**`CompareSession`** is an independent domain value type (in `domain/CompareSession.h`) that owns all comparison state. `CompareEngine` operates on it; `CompareWorkspace` only renders it.

## Rationale
- **Separation of concerns** — state vs. behavior. Session = data, Engine = logic, Workspace = rendering
- **Testability** — session state can be created and verified without an engine or UI
- **Serialization** — a pure value type can later be saved/loaded (compare presets)
- **Thread safety** — immutable session snapshots can be passed to render threads
- **UI independence** — workspace reads session; never owns logic

## State owned by CompareSession
- Image list (paths/IDs)
- Compare layout (cols, rows)
- Per-cell transforms (scale, offset)
- Sync state (enabled/disabled, shared transform)
- Blink state (active index)

## Consequences
- ✅ Clear data/behavior separation
- ✅ Workspace is strictly a view
- ❌ Need to sync session changes back to engine

## Related
- RFC-006 (Compare engine controllers)
- RFC-009 (UI lightweight)
