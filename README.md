# MViewer

**Visual analysis platform for image algorithm engineers** — compare, validate, and analyze image processing algorithm outputs.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.8%2B_Widgets-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![CI](https://github.com/lgxgizh/mviewer/actions/workflows/ci.yml/badge.svg)](https://github.com/lgxgizh/mviewer/actions)

---

## Overview

MViewer is **not a general-purpose image viewer** — it is a **visual analysis platform** for engineers who compare and validate the outputs of different image processing algorithms (camera ISP, CV pipelines, SDK versions). The core workflow is **comparison** and **analysis**; browsing is just the entry point.

- **Compare**: Multi-image (2–8) side-by-side with synchronized zoom/pan/selection, blink comparison, difference maps
- **Analyze**: Histogram, RGB mean, PSNR, SSIM, noise estimation, entropy, sharpness, MTF (MTF50), dead-pixel detection, ColorChecker Delta-E, ROI statistics
- **Performance**: Background async decode, 5-level cache, predictive preloading, CPU tile pipeline (100 MP visible-region decode), capability-gated GPU tile upload, UI never blocks
- **Plugin**: Extensible analyzer system via `AnalyzerRegistry`
- **Browse & Manage**: File-name search with recursive subfolder scan, live
  gallery filter; per-image Metadata panel (format, dimensions, bit depth,
  channels, color space, DPI, EXIF orientation, ICC profile, embedded
  EXIF/XMP text); file actions — rename, move to recycle bin, copy/move to…,
  reveal in Explorer (shortcuts: F2 / Delete / Ctrl+C / Ctrl+M / Ctrl+E)

---

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    UI (Qt Widgets)                   │
│  MainWindow │ ImageViewer │ CompareWorkspace │ ...   │
├─────────────────────────────────────────────────────┤
│                  Application Layer                   │
│  UseCases: OpenDirectory │ Compare │ Rename │ Delete │
├─────────────────────────────────────────────────────┤
│                    Core Layer                        │
│  CompareEngine │ AnalysisEngine │ RenderEngine       │
│  ImageRepository │ CacheManager │ TaskScheduler       │
│  EventBus │ CommandRegistry │ Encoder │ Decoder       │
├─────────────────────────────────────────────────────┤
│                   Domain Layer                       │
│  Image │ Histogram │ Selection │ CompareSession      │
└─────────────────────────────────────────────────────┘
```

**Dependency direction**: UI → Application → Core → Domain (strictly inward).

- **Domain**: Zero dependencies (pure `std` types, no Qt)
- **Core**: Qt-free headers; `.cpp` internals may use Qt
- **UI**: Qt 6 Widgets boundary

---

## Tech Stack

| Component | Choice |
| ----------- | -------- |
| Language | C++20 |
| GUI | Qt 6 Widgets (see the [Qt version matrix](#qt-version-matrix)) |
| Build | CMake + Ninja |
| Compiler | MSVC 2022 (primary), Clang/GCC (CI / Linux) |
| Cache | SQLite (disk) + LRU (memory) |
| Tests | Custom lightweight framework + golden/vision regression |

---

## Build

### Prerequisites

- A C++20 compiler (MSVC 2022, or recent Clang/GCC)
- Qt 6 (`msvc2022_64` on Windows, or system Qt on Linux/macOS) — see below
- CMake 3.22+
- Ninja (recommended)

### Qt version matrix (M46, single source of truth)

| Role | Version | Where |
| ----- | ------- | ----- |
| **Minimum supported** | Qt 6.8 | the CI PR gate builds and tests with 6.8.0 (`ci.yml`) |
| **CI tested** | 6.8.0 | `.github/workflows/ci.yml` (windows-2022, MSVC 2022) |
| **Recommended / dev-verified** | 6.10.x | the dev machine builds and runs the full gate with 6.10.3 |

Newer 6.x releases are expected to work (no 6.8-specific API usage), but only
6.8.0 (CI) and 6.10.3 (dev) are continuously verified. `build.ps1` detects Qt
in this order: `QT_ROOT_DIR` → `Qt6_DIR` → `QT_ROOT` → `D:\QT\6.11.1` (legacy
fallback) → user/Program Files installs; point it at your install via
`QT_ROOT_DIR` or `Qt6_DIR` rather than relying on the legacy fallback path.

### Build Commands

```bash
# Configure — point CMAKE_PREFIX_PATH at your Qt installation
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="<path-to-Qt>/msvc2022_64"

# Build
cmake --build build

# Run tests (headless)
cd build && QT_QPA_PLATFORM=offscreen ./bin/core_tests
QT_QPA_PLATFORM=offscreen ./bin/test_m3m4m5
QT_QPA_PLATFORM=offscreen ./bin/mviewer_unit_tests
```

> **Windows note**: Put the MSVC `bin` directory first on `PATH` (or use the Visual Studio developer prompt) so `cl.exe` resolves correctly.

### Targets

| Target | Description |
| -------- | ------------- |
| `MViewer` | Main application |
| `core_tests` | Core engine tests (compare, layout, sync) |
| `mviewer_unit_tests` | Unit tests (decode, cache, scheduler) |
| `test_m3m4m5` | M3/M4/M5 integration tests |
| `test_plugin_loader` / `test_plugin_manager` | Plugin framework tests |
| `golden_main` | Golden image regression (generate / `--compare`) |
| `vision_regression` | Vision regression (generate / `--compare`) |
| `ui_fixture` | UI screenshot regression (generate / `--compare`) |
| `benchmark_scenario` | Per-scenario performance benchmark (CSV + baseline diff) |

---

## Project Structure

```
mviewer/
├── src/
│   ├── core/
│   │   ├── analysis/      # AnalysisEngine (PSNR, SSIM, noise, histogram)
│   │   ├── analyzer/      # Analyzer plugin interface + built-in analyzers
│   │   ├── benchmark/     # Benchmark framework
│   │   ├── cache/         # CacheManager (unified memory + disk)
│   │   ├── command/       # Command pattern (ICommand + Registry)
│   │   ├── compare/       # CompareEngine (sync zoom/pan, blink, diff)
│   │   ├── filesystem/    # FileSystem (image enumeration)
│   │   ├── image/         # Decoder, Encoder, ImageCache, DiskCache, ImageRepository
│   │   ├── render/        # RenderEngine
│   │   ├── scheduler/     # TaskScheduler (5 independent pools)
│   │   └── plugin/        # PluginLoader + PluginManager
│   ├── domain/            # Pure business objects (Image, Histogram, Selection, CompareSession)
│   ├── application/       # UseCases (OpenDirectory, Compare, Rename, Delete)
│   └── ui/                # Qt Widgets (MainWindow, ImageViewer, CompareWorkspace, ...)
├── docs/
│   ├── adr/               # Architecture Decision Records
│   ├── rfc/               # Request For Comments (design proposals)
│   ├── spec/              # Module specifications
│   ├── contracts/         # Per-module API contracts
│   ├── workflow/          # Development workflows
│   ├── design/            # Design notes (data flow, etc.)
│   └── quality/           # Performance / threading / memory budgets
├── golden/                # Golden reference images for regression tests
├── tests/                 # Regression test suites (vision, ui_fixture, plugin)
├── benchmarks/            # Benchmark suite
├── CMakeLists.txt
└── README.md
```

---

## Key Components

### ImageRepository

Abstraction over the image lifecycle — hides FileSystem + Decoder + Cache behind a single interface.

```cpp
auto result = ImageRepository::instance().load("image.png");
if (result.success()) {
    auto frame = result.frame;  // ImageFrame with pixels + histogram + state
}
```

### CacheManager

Unified cache orchestration over memory (LRU pools) and disk (SQLite).

```cpp
// Memory first, then disk
ImageData img;
if (CacheManager::instance().get(CacheLevel::FullImage, key, img)) {
    // Cache hit
}
```

### Analyzer Plugin System

Extensible analysis via `AnalyzerRegistry`:

```cpp
AnalyzerRegistry::instance().registerAnalyzer("my_analyzer", []() {
    return std::make_unique<MyAnalyzer>();
});
```

### Plugin Loading

Load external analyzer plugins at runtime via `PluginManager`:

```cpp
PluginManager::instance().loadDirectory("<path-to-plugins>");
```

---

## Documentation

| Document | Description |
| ---------- | ------------- |
| [docs/vision.md](docs/vision.md) | Product vision and scope |
| [docs/architecture.md](docs/architecture.md) | Target architecture overview |
| [docs/roadmap.md](docs/roadmap.md) | Milestone plan |
| [docs/coding_style.md](docs/coding_style.md) | C++20/Qt coding standards |
| [docs/performance.md](docs/performance.md) | Performance targets and profiling |
| [docs/image_pipeline.md](docs/image_pipeline.md) | Decode → Color → Cache → Render pipeline |
| [docs/cache.md](docs/cache.md) | Multi-level cache hierarchy |
| [docs/ui.md](docs/ui.md) | UI specification |
| [docs/plugin.md](docs/plugin.md) | Plugin system design |
| [docs/adr/](docs/adr/) | Architecture Decision Records |
| [docs/rfc/](docs/rfc/) | Design proposals |
| [docs/spec/](docs/spec/) | Module specifications |
| [QUALITY.md](QUALITY.md) | Quality gates and engineering workflow |

---

## Performance Targets

> **Three different numbers, deliberately kept separate (M46 reconciliation):**
>
> | Level | What it is | Where |
> | ----- | ---------- | ----- |
> | **Automated gate (CI/Release)** | generous, cross-machine-stable absolute caps enforced by `mviewer_bench --enforce` against `benchmark/performance_budget.json` (`bench_enforce` is a hard CTest gate) | `benchmark/performance_budget.json` |
> | **Regression axis** | ±10% vs `benchmark/perf_baseline.json`, run on the nightly regression profile — separate from the absolute caps | `benchmark/perf_baseline.json`, `nightly.yml` |
> | **Manual product targets** | aspirational, developer-machine expectations for interactive feel — NOT enforced by any gate | the table below |
>
> The table below is the *product* (manual) target list, measured on the
> developer machine; the automated gate's numbers are deliberately looser so
> the mandatory CI gate stays green across heterogeneous runners (see the
> calibration note in `performance_budget.json`).

| Operation | Manual product target (dev machine) |
| ----------- | -------- |
| Cold start | < 300ms |
| Warm start | < 100ms |
| Image switching (preloaded) | < 16ms |
| JPEG decode (24MP) | < 50ms |
| Zoom/pan | 60fps sustained |
| Cache hit ratio (memory) | > 90% |
| First thumbnails, 1000-image folder | < 2 s |

Measured M24-era results (2-core VM vs dev box) and the budget-vs-target gaps
are documented in `docs/performance.md` and the M24/M46 review reports.

---

## Distribution

Two self-contained packages are produced from a Release build (no external
Qt / Visual C++ install required on the target machine):

| Artifact | Command | Contents |
| -------- | ------- | -------- |
| `dist/MViewer-<ver>-portable.zip` | `scripts/package_portable.ps1` | `MViewer.exe` + Qt6 runtime + platform/imageformat plugins + bundled VC runtime + README/CHANGELOG |
| `dist/MViewer-<ver>-Setup.exe` | `scripts/package_release.ps1` | NSIS installer (start-menu + desktop shortcuts, uninstaller) |

Build everything in one step:

```powershell
# Release build + portable zip + installer
# (version comes from CMakeLists.txt project(VERSION) automatically)
powershell -ExecutionPolicy Bypass -File scripts/package_release.ps1 -Build
```

`scripts/package_portable.ps1` uses Qt's official `windeployqt` to gather exactly the
DLLs/plugins `MViewer.exe` imports, then bundles the matching MSVC C++ runtime
so the archive runs on a clean Windows install. Prereqs: Qt 6.10.x
(`msvc2022_64`), `windeployqt`, and `makensis` (NSIS) on `PATH`. The package
version is read from `build_msvc/version_info.txt` (generated from the CMake
`project(VERSION)` — the single version source).

> **Screenshot / demo GIF**: Generated from the real UI (no mock) via
> `scripts/record_demo.ps1`, which renders the actual `MainWindow` offscreen
> through Qt (`mviewer_demo_render`) across three workflow states — directory
> open, image selected with the Metadata panel populated, and a live filename
> search — then stitches them into `dist/mviewer_demo.gif` (and a static
> `dist/mviewer_screenshot.png`) with `ffmpeg`. This runs headlessly because the
> build/terminal session cannot reach the interactive display session; the
> rendered frames are genuine Qt output of the shipping UI code. Prereq: `ffmpeg`
> on `PATH` (`choco install ffmpeg`). The app is also fully exercisable headless
> via the benchmark/acceptance executables (see `scripts/product_workflow_gate.ps1`).

---

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) before opening a pull request. See
[CHANGELOG.md](CHANGELOG.md) for the release history.

## License

MIT License — see [LICENSE](LICENSE) for details.
