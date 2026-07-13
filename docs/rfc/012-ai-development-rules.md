# RFC-012: AI Development Rules

## Status
Draft → Accepted

## Priority
P0 (most important)

## Goal
This repository is AI-first. Every implementation task must follow a strict workflow to ensure architectural coherence and correctness.

## Workflow (MANDATORY)
```
1. Read Specification (docs/spec/)
       ↓
2. Read ADR (docs/adr/)
       ↓
3. Read RFC (docs/rfc/)
       ↓
4. Plan (concrete steps, file list, API surface)
       ↓
5. Implement (per plan, per RFC, per spec)
       ↓
6. Build (cmake --build . — must succeed)
       ↓
7. Run Tests (all executables green)
       ↓
8. Benchmark (per RFC-011 budgets)
       ↓
9. Self Review (architecture compliance, no drift)
       ↓
10. Update Documentation (ADR/RFC/spec if changed)
       ↓
11. Commit (atomic, verifiable)
```

## Rules

### Never skip verification
Every commit must pass build + tests. No exceptions.

### Never introduce architectural drift
If implementation would violate RFC/ADR, update RFC/ADR *first*, then implement.

### Architecture > speed
Correct architecture is more important than fast implementation.

### Specs are contracts
`docs/spec/` documents are binding. Implementation must match spec exactly.

### ADRs are decisions
`docs/adr/` records *why*. Never silently violate an ADR.

### RFCs are roadmaps
`docs/rfc/` records *what's next*. Implement them in priority order.

### Quality is non-negotiable
`docs/quality/` defines budgets. Always check. Always report.

### Every commit must be buildable
No commit leaves the build red.

### Self-review is mandatory
After implementation, re-read the code against RFC/ADR/spec before committing.

## Consequences
- Predictable, verifiable progress
- No "it works on my machine" — it builds in CI
- Architecture remains coherent as project grows
- New features don't break old invariants

## Related
- RFC-011 (Performance budget)
- RFC-010 (Domain First)
- docs/quality/*, docs/spec/*
