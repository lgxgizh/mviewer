# ADR-011: Why Viewer Core scope is bounded (no Tile Pipeline / Plugin ABI / Perfetto in M4)

## Status

Accepted (2026-07-17)

## Context

The `M4_PROFESSIONAL_VIEWER_CORE` RFC matures the existing Compare Engine and
AnalyzerRegistry into a professional viewer core. There is pressure to also pull
in adjacent large subsystems (Render Tile Pipeline, Plugin ABI rework, Perfetto
tracing). Each of those is independently valuable but carries high complexity
and would dilute M4's focus on **maturity of what already works**.

## Decision

M4 is explicitly bounded:

- **Render Tile Pipeline — OUT (deferred).** A `core/render/Viewport` +
  `TileGrid` + `TileCache`(LOD) foundation already exists (seeded M7) and the
  `ImageViewer` paints per visible tile. Extending it into a full tile pipeline
  (100 MP / RAW streaming) is a separate, later milestone, not M4. Doing it now
  would introduce large complexity before the viewer-core maturity work is done.
- **Plugin ABI — OUT (frozen).** The E2E plugin path (`example_analyzer` loaded
  through a shared `mviewer_core`, registered/queried at runtime, verified by a
  subprocess CTest) is the contract. M4 stabilizes the `Analyzer` interface but
  does not change the ABI/loading mechanism.
- **Perfetto — OUT (already opt-in).** An opt-in trace shim exists (M7). M4 does
  not depend on it and does not make it mandatory.

## Rationale

- **Focus:** M4's value is hardening synchronization, pixel inspector,
  selection, analyzer-stability, and command unification — not new engine
  architecture.
- **Risk control:** Tile Pipeline + RAW + ABI changes are each multi-week efforts
  that would blow M4's budget and re-introduce the "see need → add class → fix
  CI" churn the project is moving away from.
- **Sequencing:** the roadmap re-prioritization (2026-07-16) already placed
  Render Pipeline after Compare maturity. M4 = Compare; Render Pipeline = later.

## Consequences

- ✅ M4 stays a tight, verifiable maturity milestone.
- ✅ Existing foundations (render seed, plugin E2E, Perfetto shim) are preserved
  and extended in their own later milestones, not abandoned.
- ❌ Tile-based 100 MP / RAW rendering is NOT delivered in M4 (by design).
- ❌ No plugin ABI/loading changes in M4 (by design).

## Related

- RFC: `docs/rfc/M4_PROFESSIONAL_VIEWER_CORE.md`
- Roadmap re-prioritization note (2026-07-16) — `docs/roadmap.md`
