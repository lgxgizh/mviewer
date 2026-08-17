# M47 — Source-Backed Display: Phases 1–6 Evidence

Date: 2026-08-17 · Milestone: M47 · Status: complete (Phases 1–6 of M47)

## 1. What Phases 1–6 delivered

The Phase 0 contract (`docs/rfc/M47_SOURCE_BACKED_DISPLAY.md`) fixed that
**display representation != analysis source** and defined an additive
capability model. Phases 1–6 implemented and verified it end to end:

| Phase | Deliverable | Gate |
|---|---|---|
| 1 | Qt-free `ISourceImageCapabilities` + `SourceImage` (probe w/o decode, native-LOD/region classification counters); `QtDecoder` implements it (JPEG native LOD, TIFF honest no) | `m47_source_tests` 8 groups |
| 2 | Viewer LOD-first display: probe → viewport LOD / bounded region rasters, latest-intent/cancellation/lifetime-safe; 100 MP JPEG displays with bounded RSS | `m47_viewer_lod_tests` 6 groups |
| 3 | Compare source-backed panes: infeasible sources skip the full load, keep their pane via a metadata-only placeholder, display through per-pane viewport LOD rasters; skips never count as failures (duplicates per-request); zoom re-materializes at a denser edge | `m47_compare_lod_tests` 6 groups |
| 4 | Exact-source consumers verified with LOD panes present: Inspector samples the full-res frame (invalid, never the LOD, for infeasible panes); diff/PSNR/SSIM degrade with zero decodes; report export records placeholder pairs non-comparable, requested order, zero full decodes | `m47_exact_source_tests` 3 groups |
| 5 | Transactional async Workspace/Project restore: worker-side read+deserialize, atomic UI apply for the current generation; supersede/failure/destroy safe | `m47_restore_tests` 5 groups |
| 6 | Deterministic large-image soak + benchmark evidence (below) | `m47_large_soak`; recorder sections B2/C |

## 2. Measured evidence (recorder: `benchmarks/m47_large_image_baseline_main.cpp`)

Phase 0 baseline (2026-08-17, before any change) vs current tree:

| Path | Phase 0 | Now (Phase 6 measurement) |
|---|---|---|
| 100 MP JPEG, viewer open | **cannot open at all** (Qt 256 MB allocation limit rejects `decodeFull`) | opens to display in ~0.5 s; `displayReady` carries 12000×8333; LOD mode; **no full ImageFrame**; +53 MB RSS |
| 100 MP JPEG, viewer repaint | every fit/100%/zoom repaint stalls the UI thread 126–164 ms (full-frame scaling) | bounded LOD raster paints; zoom re-materializes denser rasters on the Thumbnail pool |
| 100 MP pair, Compare | cannot open (each pane needs a full decode) | both panes display via source-backed LOD in ~0.9 s; pane raster 1277×886, source geometry 12000×8333; **+82 MB RSS; zero full decodes** |
| 24 MP JPEG, viewer | full frame retained, +244 MB RSS while held | unchanged (feasible sources keep the full-frame analysis path) |

The counting-decoder instrumentation used by the recorder masks the Qt
capability interface, so the recorded classification there is the honest
bounded fallback (`FullDecodeScaled`, JPEG DCT-scaled, memory-bounded); the
enforced contract is the memory bound — `fullDecode == 0`, no full
`ImageFrame`, bounded RSS — which holds. The capability-native
classifications (`NativeLod` etc.) are asserted by the un-instrumented
`m47_*` test suites.

## 3. Soak evidence (`m47_large_soak`)

Six rounds of 100 MP-class display churn through the Viewer LOD path and
Compare source-backed panes: open → displayReady → zoom churn → A→B→A
supersede, destroy mid-request on alternating rounds. Per-round verdicts:
pools drain; `fullDecodeScaled/fullDecodeCrop/fullDecode == 0`; `nativeLod
>= 1`. Final verdicts: scheduler dependency graph converges to zero (no
leaked handles/deferred entries); peak RSS growth < 350 MB across all six
rounds; the RSS plateau is stable from round 3 (drift < 50 MB — no
per-round leak; Windows WorkingSet keeps the heap high-water mark, so
absolute convergence is not asserted).

## 4. Gate progression

| Gate | Result |
|---|---|
| M46 closure | 96/96 (3 consecutive runs) |
| + Phase 1 | 99/99 |
| + Phase 2 | 100/100 |
| + Phase 3 | 101/101 |
| + Phase 4 | 102/102 |
| + Phase 5 | 103/103 |
| + Phase 6 | **104/104** (incl. golden, bench_enforce, workflow, soak, architecture gate 0 violations) |

## 5. Honest limitations (not faked)

- **TIFF has no native LOD** with the Qt decoder: a 100 MP TIFF pane displays
  blank (the bounded attempt is recorded and rejected inside Qt's allocation
  limit) rather than pretending a capability exists. Full TIFF LOD/tile decode
  is deferred beyond M47.
- **Exact-source analysis on infeasible panes is unavailable by design**: the
  Inspector shows 无效, diff metrics stay "—", and report pairs are recorded
  non-comparable until an explicit full materialization exists (Phase 4
  contract: analysis consumers never trigger implicit materialization).
- **Native hardware qualification** (physical ICC, mixed-DPI, long-session GUI)
  remains **MANUAL / BLOCKED** on this offscreen machine — see
  `docs/review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md`; nothing here
  claims those rows.

## 6. Verification commands

```
.\build.ps1 Test          # full gate (104 tests)
python testdata/generate_large_fixtures.py --ensure
build_msvc\bin\m47_large_image_baseline.exe --out benchmark\report\m47.json
build_msvc\bin\m47_large_soak.exe
```
