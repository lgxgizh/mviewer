# RFC-010: Domain First Development

## Status
Draft → Accepted

## Priority
P0

## Goal
Future development follows Domain First direction.

## Principle
```
UI (Qt Widgets, only in src/ui/)
    ↓
Application (UseCases, only in src/application/)
    ↓
Domain (business objects, in src/domain/)
    ↓
Core (Qt-free algorithms, in src/core/)
    ↓
Infrastructure (Qt-allowed, file/sqlite)
```

## Rules
- Business rules belong to Domain
- Qt should remain inside UI
- Core should not depend on Qt (headers expose std types only)
- Application orchestrates use cases
- UI renders domain state, never owns logic

## Module Placement
- `domain/`: Pure structs/enums, zero Qt
- `core/`: Algorithms, engines, managers — Qt in .cpp only
- `application/`: Use cases (OpenDirectory, Compare, Export)
- `ui/`: Qt widgets, dialogs, views

## Consequences
- Testable (domain/core are UI-free)
- Portable (replace Qt without touching domain)
- Long-lived unlike UI frameworks

## Related
- RFC-009 (UI lightweight)
- ADR-010 (Why UI widgets lightweight)
