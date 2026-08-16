# M44 Release Readiness & Structural Convergence

Date: 2026-08-15  
Status: automated release-candidate closure complete; native UX sign-off pending

## Final verdict

**AUTOMATED RC READY — MANUAL ITEMS PENDING**

The M44 implementation closes the reproducible data-safety, export-path,
async-persistence, architecture-boundary, and complexity risks in the local
Windows test environment. The final Release build and complete CTest gate are
green. This verdict does not claim native GUI, physical ICC, mixed-DPI, or
real multi-volume validation that was not available in this offscreen run.

## Baseline and final evidence

The baseline was captured before M44 source changes in
`M44_RELEASE_CLOSURE_BASELINE_2026-08-15.md`.

| Evidence | M44 baseline | Final run |
| --- | ---: | ---: |
| Release build | not yet run for M44 | **PASS** (`.\build.ps1 Release`) |
| Full CTest | 88/88 passed | **88/88 passed**, 499.20 s (`.\build.ps1 Test`) |
| `golden_image` | passed | **passed** |
| `bench_smoke` | passed, 122.90 s | **passed, 123.57 s** |
| `bench_enforce` | passed, 324.14 s | **passed, 265.08 s** |
| Architecture advisory warnings | 4 | **0** |
| Complexity hard file failures | 58 | **0** |
| Cyclomatic failures | 6 | **0** |
| Functions over 120 lines | 44 | **0** |
| Complexity warnings | 122 | **92** |
| Health score | 78.2 / C | **92.7 / A** |

The final CTest run also passed `workflow_ux_tests`, Browse/Compare/export
acceptance, `async_lifetime_tests`, `m27_lifetime_tests`, `m27_lifecycle_torture`,
the golden-image gate, and both performance gates. No performance budget
regression was reported.

## Reproduced risks and root causes

- File commands treated `std::filesystem::rename` as sufficient for all moves,
  did not expose rollback uncertainty, and lacked a deterministic cross-volume
  seam.
- Several UI-triggered persistence/export paths had direct or synchronous
  write/serialization responsibilities instead of a common cancellable,
  atomic worker path.
- Preview and viewer widgets crossed the Core boundary directly for cache and
  repository policy.
- Mature implementation and test translation units had accumulated oversized
  functions and high cyclomatic complexity.
- The first structural split of `MetadataModel::rebuild()` left section helper
  bodies empty. `metadatamodel_tests` caught this immediately; the original
  section logic was restored before the final gate.

## Changes delivered

### P0 data safety

- Added the injectable `FileSystemAdapter` seam for deterministic filesystem
  fault injection and UTF-8 path conversion.
- File rename/move/delete commands now preflight collisions, protect existing
  destinations, use verified copy/remove fallback for cross-volume moves, and
  retain unresolved command state when rollback cannot be completed.
- CommandStack no longer discards commands whose disk state is unresolved.
- Added coverage for normal rename/move/delete, collisions, partial batches,
  rollback success and failure, cross-volume fallback, cancellation/error
  reporting, and Chinese/emoji paths.

### Export and persistence convergence

- ExportJob remains the single cancellable execution path with bounded staging,
  temporary output, atomic commit, cleanup, and structured results.
- MainWindow report/session/project persistence now captures value snapshots,
  serializes and writes in worker-side jobs, and delivers results through
  generation and `QPointer` lifetime guards.
- Thumbnail batch analyzer export uses the shared atomic text writer; the
  obsolete synchronous legacy implementation was removed.
- ExportJob report/contact runners now return their result on every control
  path, eliminating the compiler's missing-return warnings.

### Architecture closure

- Added thin ImageLoading facade/service boundaries for viewer and preview
  loading policy.
- PreviewPanel and ImageViewer no longer expose direct CacheManager or
  ImageRepository dependencies in the guarded UI paths.
- `architecture_gate.ps1` reports zero advisory violations.

### Structural convergence

- Split oversized production responsibilities across ImageViewer, MainWindow,
  CompareWorkspace, ThumbnailPanel, AnalysisPanel, ExportJob, ImageRepository,
  MetadataReader/Model, PluginManager/Loader, WorkspaceSerializer, and related
  dialogs without changing their public product contracts.
- Split large acceptance fixtures into `.inc` feature suites while retaining
  their registrations and assertions.
- `complexity_gate.ps1 -Strict` exits 0 without threshold, ignore-list, or gate
  changes.

## Architecture before / after

Before: four advisory drifts (PreviewPanel→CacheManager and
ImageViewer→ImageRepository).  
After: **0** advisory violations; the enforced direction remains
`UI → Application → Core → Domain`, with Qt-free Core/Domain headers retained.

## Performance and behavior evidence

The final run preserved the existing Browse → Compare → Analyze → Export
workflow gates, golden-image output, and performance budgets. The benchmark
numbers are local wall-clock evidence on the same four-logical-core host; the
enforcement profile passed in both baseline and final runs, so the apparent
time reduction is not treated as a product-performance claim beyond “no gate
regression.”

The final run additionally verified the async lifetime and persistence paths,
including close/destruction safety and latest-generation result delivery.

## Native UX evidence and manual pending items

The following were not observable or executable in the available offscreen
environment and are explicitly **MANUAL PENDING**:

- Native Release GUI Browse → fullscreen viewer → zoom/pan → return workflow.
- Native Compare 2/4/8-image split/overlay/blink/diff/ROI/Inspector workflow.
- 1000+ image long-session feel, rapid A→B→A navigation, repeated
  fullscreen/Compare open-close, and repeated export/cancel perception.
- Physical embedded ICC behavior, Windows 100/125/150/200% DPI, mixed-DPI
  display movement, and real two-volume integration evidence.
- Native Unicode file-operation UX and user-facing aggregation dialogs.

Offscreen `workflow_ux_tests` and acceptance tests are evidence for their
automated contracts only; they are not substituted for this manual review.

## Remaining risks

1. The native UX checklist still needs a Windows GUI reviewer sign-off on a
   Release build, especially mixed-DPI, ICC, long-session feel, and real
   multi-volume behavior.
2. The complexity warning population is materially lower but remains advisory
   at 92; further reduction should be incremental and product-driven.
3. Compiler/toolchain code-page warnings in pre-existing source/header text
   remain environment noise; the M44-specific missing-return warnings are
   closed.

## Verification commands

```powershell
.\build.ps1 Release
.\build.ps1 Test
.\scripts\architecture_gate.ps1
.\scripts\complexity_gate.ps1 -Strict
.\scripts\health_score.ps1
```

All commands above completed successfully for this M44 closure, with the
manual limitations recorded rather than inferred away.
