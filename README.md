# MViewer

**Image algorithm validation tool** — FastStone Pro + algorithm analysis, rebuilt with modern C++20 clean architecture.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6.11_Widgets-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey.svg)]()
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

---

## Overview

MViewer is **not an image browser** — it's an **image algorithm validation tool** designed for comparing and analyzing image processing algorithms. The core workflow is **comparison** and **analysis**; browsing is just the entry point.

- **Compare**: Multi-image side-by-side with synchronized zoom/pan/selection, blink comparison, difference maps
- **Analyze**: Histogram, RGB mean, PSNR, SSIM, noise estimation, ROI statistics
- **Export**: PNG/JPEG/BMP/WebP with quality control, batch conversion
- **Plugin**: Extensible analyzer system via `AnalyzerRegistry`

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
|-----------|--------|
| Language | C++20 |
| GUI | Qt 6.11 Widgets |
| Build | CMake + Ninja |
| Compiler | MSVC 2022 (primary), Clang/GCC (future) |
| Cache | SQLite (disk) + LRU (memory) |
| Tests | Custom lightweight framework |

---

## Build

### Prerequisites

- Visual Studio 2022 (MSVC 14.44+)
- Qt 6.11.x (`msvc2022_64`)
- CMake 3.20+
- Ninja

### Build Commands

```bash
# Configure
cmake -B build_msvc -G Ninja \
  -DCMAKE_PREFIX_PATH="D:/QT/6.11.1/msvc2022_64" \
  -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl

# Build
cmake --build build_msvc

# Run tests
cd build_msvc && QT_QPA_PLATFORM=offscreen ./bin/core_tests.exe

# Run benchmark
./bin/benchmark.exe
```

### Targets

| Target | Description |
|--------|-------------|
| `MViewer` | Main application |
| `core_tests` | Core engine tests (compare, layout, sync) |
| `mviewer_unit_tests` | Unit tests (decode, cache, scheduler) |
| `visual_test` | Visual/manual verification |
| `analyze_main` | CLI analysis tool |
| `benchmark` | Performance benchmark suite |

---

## Milestones

| Milestone | Status | Description |
|-----------|--------|-------------|
| M0/M1 | ✅ | Working browser: 3-panel, directory tree, gallery, preview, thumbnails |
| M2 | ✅ | Architecture solidification: Image Core, Task Scheduler, domain layer, EventBus, Command system |
| M3 | ✅ | Analysis panel: Histogram, PSNR/SSIM/Noise, ROI stats, plugin registry |
| M4 | ✅ | Image export: Encoder (PNG/JPEG/BMP/WebP), batch conversion |
| M5 | ✅ | CacheManager integration + Benchmark suite |
| M6 | 📋 | SQLite disk cache deepening |
| M7 | 📋 | CommandPalette / AnalysisPanel integration |

---

## Project Structure

```
mviewer/
├── src/
│   ├── core/
│   │   ├── analysis/      # AnalysisEngine (PSNR, SSIM, noise, histogram)
│   │   ├── analyzer/      # Analyzer plugin interface + HistogramAnalyzer
│   │   ├── benchmark/     # Benchmark framework
│   │   ├── cache/         # CacheManager (unified memory + disk)
│   │   ├── command/       # Command pattern (ICommand + Registry)
│   │   ├── compare/       # CompareEngine (sync zoom/pan, blink, diff)
│   │   ├── filesystem/    # FileSystem (image enumeration)
│   │   ├── image/         # Decoder, Encoder, ImageCache, DiskCache, ImageRepository
│   │   ├── render/        # RenderEngine
│   │   └── scheduler/     # TaskScheduler (4 independent pools)
│   ├── domain/            # Pure business objects (Image, Histogram, Selection, CompareSession)
│   ├── application/       # UseCases (OpenDirectory, Compare, Rename, Delete)
│   └── ui/                # Qt Widgets (MainWindow, ImageViewer, CompareWorkspace, ...)
├── docs/
│   ├── adr/               # Architecture Decision Records (001-010)
│   ├── vision.md          # Product vision
│   ├── architecture.md    # Target architecture
│   ├── roadmap.md         # Development roadmap
│   ├── coding_style.md    # C++20 coding standards
│   ├── performance.md     # Performance targets
│   ├── image_pipeline.md  # Image processing pipeline
│   ├── cache.md           # Cache hierarchy design
│   ├── ui.md              # UI specification
│   └── plugin.md          # Plugin system specification
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
Unified cache orchestration over memory (3 LRU pools) and disk (SQLite).

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
// Register a custom analyzer
AnalyzerRegistry::instance().registerAnalyzer("my_analyzer", []() {
    return std::make_unique<MyAnalyzer>();
});
```

### Command System
All user actions implement `ICommand` and register in `CommandRegistry`:

```cpp
reg.registerCommand(std::make_unique<ExportCommand>(this));
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [vision.md](docs/vision.md) | Product vision and scope |
| [architecture.md](docs/architecture.md) | Target architecture overview |
| [roadmap.md](docs/roadmap.md) | Milestone plan |
| [coding_style.md](docs/coding_style.md) | C++20/Qt coding standards |
| [performance.md](docs/performance.md) | Performance targets and profiling |
| [image_pipeline.md](docs/image_pipeline.md) | Decode → Color → Cache → Render pipeline |
| [cache.md](docs/cache.md) | Multi-level cache hierarchy |
| [ui.md](docs/ui.md) | UI specification |
| [plugin.md](docs/plugin.md) | Plugin system design |
| [adr/](docs/adr/) | Architecture Decision Records |

---

## Performance Targets

| Operation | Target |
|-----------|--------|
| Cold start | < 300ms |
| Warm start | < 100ms |
| Image switching (preloaded) | < 16ms |
| JPEG decode (24MP) | < 50ms |
| Zoom/pan | 60fps sustained |
| Cache hit ratio (memory) | > 90% |

---

## License

MIT License — see [LICENSE](LICENSE) for details.

---

## Contributing

This is an open-source project. Contributions welcome:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

Please read [coding_style.md](docs/coding_style.md) and [architecture.md](docs/architecture.md) before contributing.
