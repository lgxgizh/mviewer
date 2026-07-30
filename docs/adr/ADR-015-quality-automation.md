# ADR-015: Quality Automation (M23)

- **Status:** Accepted
- **Date:** 2026-07-30
- **Deciders:** Hermes (commander), OpenCode (writer)
- **Supersedes / relates:** ADR-014 (TU-size guardrails); QUALITY.md; M22 review

## Context

After M22 the project had *some* engineering scaffolding (CI tiers, benchmark
baseline, clang-tidy/format hooks, a health dashboard), but the review made the
direction explicit:

> From this stage, reduce new feature work. Invest in engineering
> infrastructure. Every quality metric should be produced **automatically by CI**,
> not hand-maintained.

The concrete gaps called out were:

1. Dashboard must be **auto-updated and committed by CI**, not edited by hand.
2. A **Failure Database** (known issues) that is auto-linked to regression tests.
3. A **Test Matrix** that is generated, not written.
4. **Benchmark trend** (history + trajectory), not just a baseline.
5. A **Bug Gate**: every issue must carry a regression test.
6. A real **Coverage report** (HTML + per-module) uploaded as an artifact.
7. A **Code Complexity Gate** with cyclomatic + length thresholds.
8. An **ADR Gate**: architectural changes must add/update an ADR.

## Decision

Adopt **M23: Quality Automation** as a milestone whose only deliverable is that
all quality signals are machine-produced and self-updating:

- `scripts/health_score.ps1` aggregates every gate into one score + dashboard.
- `scripts/complexity_gate.ps1` enforces file / function-length / cyclomatic /
  class-size limits (PR = advisory; nightly = `-Strict` so debt surfaces).
- `scripts/architecture_gate.ps1` enforces layer rules (advisory).
- `scripts/known_issues_gate.ps1` (Bug Gate) requires open issues to link an
  existing regression test.
- `scripts/adr_gate.ps1` requires architectural PRs to add/update an ADR.
- `scripts/test_matrix.ps1` regenerates `docs/test_matrix.md` from the tree.
- `scripts/benchmark_trend.ps1` appends each nightly run to a rolling history and
  renders `benchmark/report/index.html` sparklines.
- The nightly `publish-health` job runs all of the above and **commits** the
  generated `docs/quality/*`, `docs/test_matrix.md`, `docs/known_issues/*`, and
  `benchmark/report/*` back to `master`.

## Rationale

A quality system that depends on humans editing Markdown rots the moment the
human is busy. By making every artifact a CI output, the project's health is
always current and every PR can prove: features did not regress, performance did
not drop, architecture did not rot, and quality keeps rising.

## Consequences

- Authors must not hand-edit `docs/quality/dashboard.md`, `docs/test_matrix.md`,
  or `benchmark/report/index.html` — they are regenerated.
- Architectural PRs (Repository / Cache / Scheduler / Compare) that omit an ADR
  are blocked by `adr-gate`.
- Open issues without a regression test are blocked by `known-issues` gate.
- The nightly run is the enforcement tier (non-blocking for PRs); complexity CC>25
  / fn>120 / file>800 / uncovered open issues FAIL the nightly alert.

## Alternatives considered

- **Manual dashboards**: rejected — proven to rot (the exact problem the review
  raised).
- **Hard-fail complexity/coverage in PR**: deferred — would block the large
  pre-existing debt; enforced in nightly alert tier instead, to be tightened once
  debt is paid down.
