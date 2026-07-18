# RFC — Nightly CI Workflow (M11 Product Beta, P4)

**Status:** DRAFT for review.
**Scope:** Add `.github/workflows/nightly.yml` — a scheduled, non-blocking run of
the heavy static-analysis + sanitizer + benchmark jobs that are currently
advisory in `ci.yml`. Does **not** modify `ci.yml` (per review: keep PR gate =
format/build/test/package).

## Problem
`ci.yml` runs clang-tidy / clazy / ASan as `continue-on-error` jobs on **every**
push/PR. They still consume runner minutes on every PR and add noise. The review
(P4) wants these moved to a **daily scheduled** job that never blocks a PR but
still surfaces regressions.

## Proposal
Add `nightly.yml`:
- Trigger: `schedule: cron '0 3 * * *'` (03:00 UTC daily) + `workflow_dispatch`.
- Jobs (all `continue-on-error: true`):
  - `clang-tidy` (reuse the exact invocation from `ci.yml` static-analysis).
  - `clazy` (reuse `ci.yml` clazy).
  - `asan` (reuse `ci.yml` asan — MSVC `/fsanitize=address`, offscreen ctest).
  - `benchmark` — a Linux runner that builds `mviewer_bench`, generates the
    medium dataset (1000 imgs), runs `mviewer_bench --enforce`, and uploads the
    verdict + `performance_report.csv` as an artifact. **Fails the nightly (but
    not PRs)** so a Slack/email notify can catch regressions.
- Upload all findings as artifacts; no branch-protection dependency.

## Reuse, don't reinvent
The three SA jobs already exist verbatim in `ci.yml`. `nightly.yml` duplicates
their step blocks (GitHub Actions has no cross-workflow `uses` for inline jobs).
The benchmark job reuses `mviewer_bench --enforce` (B2/B8/B9 gates) already proven
locally.

## Acceptance
- [ ] `nightly.yml` present; runs on schedule + manual dispatch.
- [ ] clang-tidy / clazy / ASan / benchmark all execute; none block PRs.
- [ ] Benchmark artifact (`performance_report.csv`) produced nightly.
- [ ] `ci.yml` unchanged (PR gate stays Phase-1).

## Non-goals
- No PR-blocking clang-tidy/ASan (explicitly rejected by review + Architect directive).
- No new build-system changes.
