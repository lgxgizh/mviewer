# Engineering Review — closure report (2026-07-19)

**Review source:** the "Product Beta" engineering review (P0–P5 + M11.1–M11.3)
pasted by the user, plus the approved 8-phase M13 plan.
**Verdict:** review is **closed** except two items that are *blocked by external
constraints* (not forgotten, not faked).

## Closed this pass

| Item | What closed it | Evidence |
|------|----------------|----------|
| P0 Product Workflow | `docs/workflow/*.md` + `product_workflow_gate` ctest (5/5) | M13.1 commit `f3f8d1c` |
| P1 AnalyzerRegistry | `getAnalyzer()`/`runAnalyzer()` added + `analysis_panel_tests` | commit `5f58cfe` |
| P2 Tile Pipeline | `docs/rfc/M13_TILE_PIPELINE.md` (RFC the review required) + verified `TileCache/TileGrid/Viewport/RenderEngine` are **wired** into `ImageViewer::paintEvent` | `docs/rfc/M13_TILE_PIPELINE.md` |
| P3 Benchmark | `scripts/benchmark_dashboard.ps1` → `benchmark/report/{history.csv,index.html}` trend; wired into `nightly.yml` `dashboard` job | this commit |
| P4 CI | `nightly.yml` (clang-tidy/ASan/benchmark, non-gating) | present |
| P5 Code quality | `scripts/audit_qt_boundary.ps1` (0 forbidden) + `M12.5_quality_audit.md` | present |
| M11.1 Workflow | — | done earlier |
| M11.2 Performance | `M12.2_results.md` | done earlier |
| M11.3 Release | **NSIS installer `.exe` built** + G1 guard in `pack_installer.ps1` + portable zip + README | `dist/MViewer-1.0.0-setup.exe` (14 MB) |

## Honestly blocked (external constraints — NOT faked)

1. **M11.3 screenshot / demo gif** — this is a headless build box with no
   interactive desktop; `PrintWindow` would capture a blank frame. A real
   screenshot requires a human (run MViewer, capture) or a CI runner with a
   desktop session. **Manual step; will not fake.**
2. **P3 large benchmark tier (10000 imgs)** — needs ~15 GB; D: has only ~5.6 GB
   free. Deferred to a bigger disk (ties into M13 Phase 4 real datasets).

## Verification

- `build.ps1 Release` OK (during installer build).
- `product_workflow_gate` ctest: **5/5 PASS**.
- Full ctest: 20/28 — the 8 failures are pre-existing baseline gaps (corpus /
  golden assets), unrelated to this work.

## Subtraction check

All closures are docs / additive scripts / installer config. No new `core/`
module, no cache/scheduler/render rewrite, no Rust, no GPU code. Architecture
freeze (M11) holds.
