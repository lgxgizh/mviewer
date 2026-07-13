# RFC-011: Performance Budget

## Status
Draft → Accepted

## Priority
P0

## Goal
Define and enforce performance budgets. All implementations must respect these targets.

## Budgets

| Operation | Target |
|-----------|--------|
| Cold start | < 300ms |
| Warm start | < 100ms |
| Open folder (1st thumbnail) | < 100ms |
| Switch image (preloaded) | < 30ms |
| Decode display (24MP JPEG) | < 50ms |
| Thumbnail generation | background only |
| UI response | always < 16ms |
| Memory (normal workload) | < 500 MB |

## Enforcement
- Every PR must pass performance regression tests
- Violations must have architectural justification
- Budgets documented in code comments

## Consequences
- Predictable user experience
- Prevents performance regression
## Related
- docs/quality/performance_budget.md
