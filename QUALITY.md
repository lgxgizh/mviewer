# Quality Gate

## Feature Gate

Every new feature must satisfy **all** of the following before merging:

| Gate | Definition |
| --- | --- |
| **Build** | `cmake --build build_msvc --clean-first` succeeds with 0 errors, 0 warnings (warnings-as-errors where applicable) |
| **Test** | All existing + new tests pass: `core_tests.exe`, `test_m3m4m5.exe`, `mviewer_unit_tests.exe` |
| **Benchmark** | No regression >10% on any established scenario from `benchmark_results.csv` |
| **Golden** | All golden image comparisons pass (if visual output changed) |
| **Spec Update** | Relevant `docs/spec/*.md` updated to reflect new API / behavior |
| **RFC Update** | Relevant `docs/rfc/*.md` Status updated to `Implemented` |
| **Documentation** | `docs/contracts/` + `docs/workflow/` entries updated if applicable |
| **Self Review** | Author confirms: no Qt leak, thread-safe, error-handled, documented |
| **No Temp Files** | No scratch scripts (e.g., `_check.bat`, `_v.sh`) left in repo |

## Architecture

Architecture is **frozen**. No large refactoring unless an ADR explicitly requires it.

See `docs/adr/001`–`010` for canonical decisions.

### Frozen Names (do NOT rename)

| Module | Role |
| --- | --- |
| `ImageRepository` | Sole image lifecycle owner |
| `CacheManager` | 5-level hierarchical cache owner |
| `TaskScheduler` | Priority multi-queue scheduler |
| `CompareEngine` | Compare facade |
| `AnalysisEngine` | Analysis routing |
| `RenderEngine` | Render backend facade |

## Code Style

- C++20 conformant
- Core layer: **no Qt in headers**
- Domain layer: **zero dependencies**
- Headers **must include-guard** and **self-compiling**
- Public API documentation (file-level or function-level comments)

## Build Health (M23 — Quality Automation)

Beyond the per-PR Feature Gate, the project tracks **long-term health** via an
automated, self-updating system. All quality artifacts are produced by CI — they
are **never hand-edited** (see ADR-015). Single entry point:

```powershell
pwsh scripts/health_score.ps1        # -> docs/quality/dashboard.md + build_health.json
```

| Dimension | Source | Gate level |
| --- | --- | --- |
| Build / Tests | ctest JUnit XML (`build_msvc/`) | **Hard** (ci-gate) |
| Code Quality | cppcheck + clang-tidy CI artifacts | **Hard** (ci-gate) |
| Complexity | `scripts/complexity_gate.ps1` — file >800 FAIL · function >120 lines FAIL · cyclomatic >25 FAIL · function >80 / cyclo >15 WARN · class >1000 WARN; ADR-014 TUs use documented caps | Advisory on PR (nightly `-Strict`) |
| Architecture | `scripts/architecture_gate.ps1` R1–R4 (UI∌Cache · Widget∌Repository · Compare∌Thumbnail · Domain 0-dep) | Advisory Warning |
| ADR | `scripts/adr_gate.ps1` — architectural PRs (Repository / Cache / Scheduler / Compare) MUST add/update an ADR | **Required** (ci-gate) |
| Known Issues (Bug Gate) | `scripts/known_issues_gate.ps1` — every OPEN issue must link an existing regression test | **Required** (ci-gate) |
| Test Matrix | `scripts/test_matrix.ps1` — auto-generated `docs/test_matrix.md` (Feature × Unit/Integration/Benchmark/UI/Vision) | auto (nightly) |
| Benchmark Trend | `scripts/benchmark_trend.ps1` — rolling `benchmark/report/index.html` SVG sparklines over past runs | auto (nightly) |
| Performance | `benchmark/perf_baseline.json` ±10% regression gate (`bench_enforce`) | **Hard** |
| Stability | B9 (cache soak) + B17 (workflow soak: RSS / handle growth across browse→compare→exit) | Report-only → dashboard |
| Coverage | Nightly OpenCppCoverage (cobertura **+ HTML**) per-module summary; target **Core ≥ 85%** | Advisory (nightly) |

- Dashboard: `docs/quality/dashboard.md` (auto-generated, **committed nightly** by `publish-health` — do not hand-edit)
- Failure DB: `docs/known_issues/` (`Issue-NNN.md` + `known_issues.json`, auto-linked to regression tests; see `README.md`)
- CI: `build-health` (PR, advisory) · `adr-gate` + `known-issues` (**PR, required**) · nightly `coverage` (HTML) + `publish-health` (auto-commits dashboard / trend / matrix)
- Score weights and formula: `scripts/health_score.ps1`
- Architecture rule ↔ ADR mapping: `docs/adr/README.md`

## Git

- Branch from `master`, submit PR
- CI must pass (Build, Test, Benchmark, Golden before merge)
- Descriptive commit message referencing RFCs touched
