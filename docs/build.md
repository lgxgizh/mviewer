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

- Install Qt 6.11.1 for `msvc2022_64` via the Qt online installer.
- Default expected location: `D:\QT\6.11.1\msvc2022_64`
  (only used as a last-resort fallback; prefer env vars).

**CI (GitHub Runner):**

- Qt is installed by the workflow via `jurplel/install-qt-action@v4`
  (currently **6.8.0**, `win64_msvc2022_64`). The runner sets `Qt6_DIR`
  automatically, so no hard-coded path is needed.

> **Version note:** local uses 6.11.x, CI uses 6.8.x. This is intentional
> (different install mechanisms) and is fine — both expose the same CMake
> package layout that `CMAKE_PREFIX_PATH` consumes. Do **not** try to force a
> single version across both; keep the env-var lookup and they stay decoupled.

To point the build at a custom Qt without moving it:

```powershell
$env:Qt6_DIR  = 'C:\Qt\6.11.1\msvc2022_64'          # highest priority
# or
$env:QT_ROOT  = 'C:\Qt\6.11.1'                       # → $QT_ROOT/msvc2022_64
```

Resolution order inside `build.ps1`: `Qt6_DIR` → `QT_ROOT/msvc2022_64` →
`D:\QT\6.11.1\msvc2022_64` (guard-checked) → **throw** with setup hints.

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

`.github/workflows/ci.yml` (single `build` job, `windows-2022`):

```
checkout → ilammy/msvc-dev-cmd → jurplel/install-qt-action (Qt 6.8.0)
   → powershell build.ps1 Test
```

CI does **not** re-implement configure/build/test. It installs toolchains and
then calls `build.ps1 Test`, so local and CI cannot drift.

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

`CMakeLists.txt` calls `enable_testing()` and registers targets:

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

which builds and then runs `ctest --output-on-failure --output-junit
test-results.xml -j4` under the offscreen Qt platform.

---

## 8. Common errors

| Symptom | Cause | Fix |
|---------|-------|-----|
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
