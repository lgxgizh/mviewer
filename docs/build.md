# Build & Environment Guide

Single source of truth for how MViewer builds locally and in CI. If a build
breaks, read this file first.

> **Rule of thumb:** there is exactly ONE way to build — `build.ps1`. CI
> delegates to the same script. There is no second, hand-written build path.

---

## 1. Build flow

```
build.sh / build.ps1
   ├─ Import-MSVCEnvironment()      locate VS + vcvars64.bat, import env
   ├─ Resolve Qt path               env vars > default (> throw)
   ├─ cmake -G Ninja -DCMAKE_PREFIX_PATH=<qt> ..   (always; idempotent)
   └─ Task:
        Release / Debug  → cmake --build . -j
        Test             → build + ctest --output-on-failure
        Clean            → remove build_msvc/
```

`build.ps1` is the canonical entry point. `build.sh` is a thin POSIX wrapper
that forwards to PowerShell (so the same command works from a bash shell).

```powershell
# Windows (PowerShell)
powershell -ExecutionPolicy Bypass -File .\build.ps1 [Release|Debug|Test|Clean]

# Any shell (bash wrapper)
bash build.sh [Release|Debug|Test|Clean]
```

| Task     | What it does                              |
|----------|-------------------------------------------|
| (none)   | Release build                             |
| `Release`| Release build                             |
| `Debug`  | Debug build                               |
| `Test`   | Build (Release) + run `ctest` offscreen   |
| `Clean`  | Delete `build_msvc/`                       |

---

## 2. Qt installation

Qt is **not** vendored and **not** hard-coded. The build resolves it at
runtime (see §4). You only need to install it once and (optionally) export an
env var.

**Local (Windows, developer machine):**

- Install Qt 6 (current dev machine: **6.10.3**) for `msvc2022_64` via the Qt
  online installer.
- Default expected location: `D:\QT\6.11.1\msvc2022_64`
  (only used as a last-resort fallback; prefer env vars).

**CI (GitHub Runner):**

- Qt is installed by the workflow via `jurplel/install-qt-action@v4`
  (currently **6.8.0**, `win64_msvc2022_64`). The runner sets `Qt6_DIR`
  automatically, so no hard-coded path is needed.

> **Version note (M46 reconciliation):** CI builds/tests with **6.8.0**
> (minimum supported), the developer machine verifies the full gate with
> **6.10.3** (recommended/dev-verified). The README carries the same matrix —
> there is no "6.11" claim anywhere. Newer 6.x releases are expected to work
> but are not continuously verified. The `D:\QT\6.11.1` fallback path in
> `build.ps1` is a legacy default; export `QT_ROOT_DIR`/`Qt6_DIR` to point the
> build at your actual install.

To point the build at a custom Qt without moving it:

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.11.1\msvc2022_64'        # highest priority: the msvc2022_64 root
# or
$env:Qt6_DIR  = 'C:\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6'   # walked up to the Qt root
# or
$env:QT_ROOT  = 'C:\Qt\6.11.1'                       # → $QT_ROOT/msvc2022_64
```

Resolution order inside `build.ps1` (every step guard-checked with `Test-Path`):
`QT_ROOT_DIR` (already the `msvc2022_64` root — what `install-qt-action@v4`
exports, i.e. the CI path) → `Qt6_DIR` → `QT_ROOT/msvc2022_64` →
`D:\QT\6.11.1\msvc2022_64` (legacy default) →
`%USERPROFILE%\Qt\6.11.1\msvc2022_64` → `%ProgramFiles%\Qt\6.11.1\msvc2022_64`
→ **throw** with setup hints.

---

## 3. Build tools (prerequisites)

| Tool            | Why                                  | Notes |
|-----------------|--------------------------------------|-------|
| Visual Studio 2022 Build Tools | MSVC toolchain + `vcvars64.bat` | Located via `vswhere.exe` |
| Ninja           | CMake generator                      | Any Ninja on PATH |
| CMake ≥ 3.22    | Build system                         | Invoked only by `build.ps1` |
| Qt 6 (see §2)   | UI / image / sql components          | `Widgets`, `Gui`, `Sql` |
| PowerShell      | The build script itself              | `-ExecutionPolicy Bypass` |

The MSVC environment is imported programmatically by `Import-MSVCEnvironment`
(`vswhere` → `vcvars64.bat` → parse `set` output). You do **not** need to open
a "Developer Command Prompt" yourself.

---

## 4. `build.ps1` internals (quick reference)

1. **`Import-MSVCEnvironment`** — finds VS via `vswhere.exe`, runs
   `vcvars64.bat x64`, and imports every `KEY=VALUE` line into the process
   environment. This is the only sanctioned way to set up the MSVC toolchain.
2. **Qt resolution** — env-var priority (§2). Throws with clear guidance if
   nothing is found.
3. **Configure** — `cmake -G Ninja -DCMAKE_PREFIX_PATH=<qt> ..` inside
   `build_msvc/`. **Always** runs (no cache-existence gate), so switching
   Debug/Release re-configures correctly.
4. **Task dispatch** — build / test / clean as above. `Test` sets
   `QT_QPA_PLATFORM=offscreen` so headless CTest works.

---

## 5. CI

`.github/workflows/ci.yml` is **Tier 1 — the PR gate** (`PR Gate (Tier 1)`), and
it is **nine jobs, not one `build` job**:

| Job | Runner | What it does | Blocks the PR? |
|-----|--------|--------------|----------------|
| `format` | ubuntu | clang-format 22.1.8 on changed C++ lines + markdownlint | `continue-on-error: true` (still listed in `ci-gate`'s `needs`) |
| `cppcheck` | ubuntu | supplemental static analysis with `--error-exitcode=1` | yes (required) |
| `clang-tidy` | ubuntu | bugprone/performance/clang-analyzer findings on PR-changed lines | yes (required) |
| `build` | windows-2022 | `build.ps1 Release` (compile + Qt/CRT deploy) + zero-compiler-warning grep on `build.log` | yes |
| `test` | windows-2022 | `build.ps1 Release` + `testdata/generate_fixtures.py`, then raw `ctest` (unit tests, `bench_enforce` perf hard-gate, `golden_image`); `needs: build` | yes |
| `build-health` | ubuntu | complexity gate + architecture gate + health dashboard | no (advisory, `continue-on-error: true`) |
| `adr-gate` | ubuntu | architectural PRs must touch `docs/adr/` | yes (required) |
| `known-issues` | ubuntu | every OPEN issue must link a regression test | yes (required) |
| `ci-gate` | ubuntu | aggregator: every required job must report `success` | this is the status branch protection depends on |

The important split: **the Tier-1 PR path never calls `build.ps1 Test`.**
`build` and `test` each run `build.ps1 Release` (the `test` job recompiles on its
own fresh runner because jobs do not share artifacts), and the `test` job then
owns CTest itself (`ctest --output-on-failure --output-junit test-results.xml
-j$testJobs`) so the suite executes once per PR and the job can publish its own
JUnit artifact. `build.ps1 Test` — build + CTest in one command — is what the
nightly `quality` job (`.github/workflows/nightly.yml`) and two Tier-3
`release.yml` jobs (performance report, full golden-image regression) run, and
what you run locally. That is a deliberate one-step difference, not drift: the
CTest invocation itself is kept identical (same `-j` rule, same
`QT_QPA_PLATFORM=offscreen`). The only workflows that hand-write `cmake`
configure/build are the ones that need a different toolchain or extra flags —
nightly `asan`, `ubsan`, `clazy`, `perfetto` (clang-cl sanitizer builds,
`MVIEWER_ENABLE_PERFETTO=ON`) — while every job that builds or gates the shipped
product goes through `build.ps1`.

---

## 6. `CMakePresets.json`

Two configure presets share `build_msvc/` as the binary dir:

- `windows-msvc-release` (`CMAKE_BUILD_TYPE=Release`)
- `windows-msvc-debug` (`CMAKE_BUILD_TYPE=Debug`)

Both read `CMAKE_PREFIX_PATH=$env{Qt6_DIR}` — no hard-coded paths, no
`CMAKE_CXX_COMPILER=cl` (the MSVC generator resolves the compiler itself).
Build presets and a `windows-msvc` test preset mirror them.

> These presets are provided for IDE/editor integration. The day-to-day build
> still goes through `build.ps1`, which calls CMake directly.

---

## 7. ctest

`CMakeLists.txt` calls `enable_testing()`, and at HEAD the suite registers
**132** tests via `add_test(NAME ...)` across `CMakeLists.txt` +
`src/CMakeLists.txt` (the block below shows three of them; `bench_enforce`,
registered in `src/CMakeLists.txt`, is the performance hard gate):

```cmake
enable_testing()
add_test(NAME core_tests   COMMAND core_tests)
add_test(NAME m3m4m5_tests COMMAND test_m3m4m5)
add_test(NAME unit_tests   COMMAND mviewer_unit_tests)
```

Run them with:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 Test
```

which builds Release, deploys the Qt/CRT runtime, sets
`QT_QPA_PLATFORM=offscreen` and then runs `ctest --output-on-failure
--output-junit test-results.xml -j$testJobs` — **not a hardcoded `-j4`**.
`build.ps1` computes `$testJobs = max(1, min(4, [Environment]::ProcessorCount -
1))`: one logical core is left for the OS/Qt helper threads and the parallelism
is capped at four. The Tier-1 `test` job in `ci.yml` uses the same computation
(`[Math]::Max(1, [Math]::Min(4, $logicalCores - 1))`), so local and CI choose the
same `-j` on the same host.

---

## 8. Common errors

| Symptom | Cause | Fix |
| --------- | ------- | ----- |
| `Qt 6 not found` | No Qt on `Qt6_DIR`/`QT_ROOT` and default missing | Set `$env:Qt6_DIR`, or install to `D:\QT\6.11.1\msvc2022_64` |
| `Qt cmake config not found at ...` | Path points at Qt root, not the msvc dir | Use `.../msvc2022_64` (or `QT_ROOT` = the `6.11.1` dir) |
| `vswhere.exe not found` / `vcvars64.bat not found` | VS Build Tools absent | Install VS2022 Build Tools + "Desktop development with C++" |
| `cl` / linker errors after switching Debug↔Release | Stale `build_msvc/` | `build.ps1 Clean` then rebuild (configure is always re-run anyway) |
| Tests fail to start (GUI crash) | Qt needs a display | `build.ps1 Test` already sets `QT_QPA_PLATFORM=offscreen` |
| CI green but local red (or vice-versa) | Diverged build logic | Ensure CI calls `build.ps1`; never hand-write CMake in CI |

---

## 9. Do NOT

- Manually edit `build.ps1`, `CMakePresets.json`, or `.github/workflows/ci.yml`
  unless explicitly asked. Agents especially tend to "improve" CI and break the
  local/CI parity — don't.
- Call `cmake`, `ninja`, `cl.exe`, or `vcvars64.bat` directly. Go through
  `build.ps1`. (Exception: debugging the build system itself, then revert.)
- Hard-code a Qt path anywhere.
