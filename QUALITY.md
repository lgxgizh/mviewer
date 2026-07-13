# Quality Gate

## Feature Gate

Every new feature must satisfy **all** of the following before merging:

| Gate | Definition |
|---|---|
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
|---|---|
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

## Git

- Branch from `master`, submit PR
- CI must pass (Build, Test, Benchmark, Golden before merge)
- Descriptive commit message referencing RFCs touched
