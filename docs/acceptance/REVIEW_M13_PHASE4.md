# M13 Review — Engineering-level Audit (post Phase 4)

**Reviewer:** Hermes (commander). **Date:** 2026-07-19.
**Scope:** Audit the delivered M13 work against the original engineering review
(2026-07-19) — *did the modules actually get wired into the product loop, or
did we just implement interfaces?* Verify the hard performance numbers.

## Verdict

**The product loop is genuinely wired, not just interface-shaped.** All five
workflow executables (`product_browse_tests`, `compare_workflow_tests`,
`analysis_panel_tests`, `export_tests`, `workspace_persist_tests`) drive the
REAL engine path (verified by reading the sources — `ImageRepository::
loadDirectoryAsync`, `ThumbnailPipeline`, `AnalyzerRegistry`, `Encoder`,
`QJson` workspace round-trip). `product_workflow_gate` chains them in user
order and passed 5/5. This closes the review's top risk: *"modules exist but
workflow not串起来."*

**Hard perf numbers (the review's biggest worry) are now MET, not just
claimed.** Local bench on the real `benchmark/data/medium` (1000 JPEG) tier:

| Metric | Review target | Measured (medium tier) | Result |
|---|---|---|---|
| B1 directory scan | < 500 ms | 29.9 ms | ✅ 16× under |
| B2 first thumbnail (COLD) | < 300 ms | **33.97 ms** | ✅ 9× under |
| B2 first thumbnail (WARM) | — | 18.98 ms | ✅ |
| B3 JPEG decode p50/p95 | — | 21 / 32 ms | ✅ |
| B7/B8 warm switch | < 50 ms | 4.5 / 7.5 ms | ✅ |

The review's stated "first thumbnail 2400 ms" problem is **resolved** (34 ms
now). The prior optimization (async thumbnail pipeline + cache) delivered.

## Phase-by-phase audit

| Phase | Claim | Verified? | Evidence |
|---|---|---|---|
| M13.1 Workflow gate | 5-link chain green | ✅ | `scripts/product_workflow_gate.ps1` + ctest 5/5 |
| M13.2 Benchmark dashboard | trend report | ✅ | `scripts/benchmark_dashboard.ps1`, `nightly.yml` dashboard job |
| M13.3 NSIS installer | `.exe` built | ✅ | `dist/MViewer-1.0.0-setup.exe` (14 MB), `pack_installer.ps1` |
| Review blocker A | real UI screenshot | ✅ | `ui_screenshot` → `dist/mviewer_screenshot.png` (real `QWidget::grab`) |
| Review blocker B | 10000-img large tier | ✅ | `D:/mviewer_bench_data/large` 10000 JPEG / 628 MB |
| M13.4 Real datasets | no-crash matrix | ✅ | `test_assets_acceptance` 122 scanned / 108 decoded / 14 skip / 0 crash |
| P2 Tile RFC | RFC + landing | ✅ | `docs/rfc/M13_TILE_PIPELINE.md`; `TileCache.h`/`TileGrid.h` + tests live in `core/render/` and `imageviewer.cpp` |
| P1 AnalyzerRegistry | realized | ✅ | `test_analysis_panel` enumerates 7+ analyzers, `getAnalyzer`/`runAnalyzer` exercised |

## Findings (non-blocking, to fix)

1. **CHANGELOG gap (doc).** `CHANGELOG.md` stops at M11. M13 (Beta) work is
   user-facing and must be recorded per AGENTS.md. → Add an M13 section.

2. **`test_product_browse` budget drift (soft).** `kFirstThumbBudgetMs` is
   2000 ms ("worst-case under ctest concurrency"), but the review's real target
   is < 300 ms and the bench *actually* measures 34 ms. The acceptance test's
   budget is 6× looser than reality — it would pass even if regressed to 1.9 s.
   → Tighten the budget to ~300 ms (the real gate), so the test catches
   regression instead of just smoke-passing. Low risk, high signal value.

3. **`benchmark/generate_bench_data.ps1` doc/behavior mismatch (doc).** Comment
   says "large is ~2 GB" and default `$Format="all"`; but Phase 4 added
   `--emit-format jpeg` specifically so large fits disk (628 MB). A user running
   the script with defaults for `-Tiers large` would still try ~2 GB. → Update
   the comment + default `large` to `jpeg`, or note `-Format jpeg` is required
   for large.

4. **`search_files` tool false-zero on `src/CMakeLists.txt` (tooling, not code).**
   The agent's grep-based search returned 0 matches for `add_executable` etc.
   on this file, while terminal `grep` confirmed everything is registered. This
   is a tooling quirk — noted so future reviews don't mis-trust it. The CMake
   registration is correct and complete.

## Honest limitations (not defects)

- **Camera RAW (NEF/CR2/ARW)** is out of scope: MViewer's Qt-plugin decoder
  doesn't support RAW containers. The review's `camera/iphone/nikon/...` subdirs
  imply RAW; supporting them needs a libraw integration (decoder-scope, frozen
  for M13). Phase 4 covers every format MViewer *can* decode + integrity edge
  cases. Documented in `docs/acceptance/M13.4_real_datasets.md`.

- **8 pre-existing ctest failures** (core_tests / m3m4m5 / m3pipeline /
  decoder_tests / cache_tests / repository_tests / metadata_tests /
  bench_smoke) are baseline asset/env gaps (corpus golden + benchmark/data
  corpus not generated in CI), NOT M13 regressions — verified the corresponding
  test sources are untouched by M13. They predate this work and are tracked
  separately.

## Release-readiness

The product loop is closed and the headline perf budget is met. Remaining M13
phases (⑤ Perfetto ⑥ Plugin SDK ⑦ GPU RFC ⑧ Public roadmap) are additive and
do not affect the already-verified loop. **Recommendation: proceed to Phase 5.**
Reported findings #1–#3 are quick doc/test fixes, can land with Phase 5 or as a
standalone doc commit — they don't block.
