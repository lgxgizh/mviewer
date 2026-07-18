# ADR-010: Why UI Widgets are Lightweight

## Status

Accepted

## Context

UI widgets (CompareWorkspace, AnalysisPanel, etc.) easily accumulate business logic: they compute diffs, manage state, call decoders directly. This violates layered architecture.

## Decision

Widgets are **strict display components**: they render domain state and emit signals. No business logic, no direct cache access, no engine calls beyond signal emission.

## Rationale

- **Testability** — logic is in UI-free domain/core layers, testable headless
- **Portability** — replace Qt with another UI without touching business logic
- **Clarity** — each layer has a clear role
- **Parallelism** — UI thread never blocked by compute-heavy operations

## Widget responsibilities

- Render current state (pixmaps, images)
- Emit signals on user input
- Receive updates via model/slot

## NOT widget responsibilities

- Compute statistics → AnalysisEngine
- Manage cache → CacheManager
- Decode images → ImageRepository
- Compare images → CompareEngine

## Consequences

- ✅ Pure presentation, no logic
- ✅ Easy to refactor UI independently
- ✅ Signals/slots keep coupling loose
- ❌ Requires clear model/view separation
- ❌ Slots must always call back to core, never replicate logic

## Related

- RFC-009 (UI lightweight)
- RFC-010 (Domain First)
