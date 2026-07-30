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

## Build Health (M23)

Beyond the per-PR Feature Gate, the project tracks **long-term health** via an
automated scoring system. Single entry point:

```powershell
pwsh scripts/health_score.ps1        # -> docs/quality/dashboard.md + build_health.json
```

| Dimension | Source | Gate level |
| --- | --- | --- |
| Build / Tests | ctest JUnit XML (`build_msvc/`) | **Hard** (ci-gate) |
| Code Quality | cppcheck + clang-tidy CI artifacts | **Hard** (ci-gate) |
| Complexity | `scripts/complexity_gate.ps1` — file >800 lines FAIL cap, >600 warn, function >80 warn; ADR-014 TUs use their documented caps | Advisory (`-Strict` planned once debt cleared) |
| Architecture | `scripts/architecture_gate.ps1` — R1: UI must not include Cache; R2: Widget must not access Repository; R3: Compare must not depend on Thumbnail; plus `audit_qt_boundary.ps1` (Core/Domain Qt-free) | Advisory Warning (Reviewer arbitrates per ADR) |
| Performance | `benchmark/perf_baseline.json` ±10% regression gate (`bench_enforce`) | **Hard** |
| Stability | Benchmark B9 (cache soak) + B17 (workflow soak: RSS / handle growth across browse→compare→exit cycles) | Report-only → dashboard |
| Coverage | Nightly OpenCppCoverage per-module summary; target: **Core ≥ 85%** | Advisory (nightly) |

- Dashboard: `docs/quality/dashboard.md` (auto-generated — do not hand-edit)
- CI: `build-health` job on every PR (advisory, artifact + step summary);
  `coverage` job in nightly
- Score weights and formula live in `scripts/health_score.ps1`
- Architecture rule ↔ ADR mapping: `docs/adr/README.md`

## Git

- Branch from `master`, submit PR
- CI must pass (Build, Test, Benchmark, Golden before merge)
- Descriptive commit message referencing RFCs touched
