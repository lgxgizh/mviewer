# ADR 004: Clean architecture layering

## Status
Accepted

## Context
The project must scale toward ~50K lines without becoming a tangle of
UI-coupled logic. Dependencies must point inward: UI → application → core → domain.

## Decision
Four layers:
- `domain/` — pure business value types, no Qt, no implementation.
- `core/` — engines, interfaces, caches, scheduler, event bus.
- `application/` — use cases orchestrating domain + core.
- `ui/` — Qt Widgets boundary; adapts core types to Qt.

Dependency rule: outer layers may depend on inner layers, never the reverse.
Domain headers never include Qt.

## Consequences
- Core is unit-testable without a display.
- Clear ownership; new features slot into a known layer.
