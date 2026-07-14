# Contributing to MViewer

Thank you for your interest in contributing! MViewer is an open-source image algorithm validation tool built with modern C++20 and Qt 6.

## Development Setup

### Prerequisites

- **C++20 compiler**: MSVC 2022 (primary), or Clang/GCC (tested on Linux/macOS)
- **Qt 6.11+** (`msvc2022_64` on Windows, or system Qt on Linux/macOS)
- **CMake 3.22+**
- **Ninja** (recommended) or your platform generator

### Building

```bash
# Configure
cmake -B build -G Ninja \
  -DCMAKE_PREFIX_PATH="<path-to-Qt>/6.11.1/msvc2022_64"

# Build
cmake --build build

# Run tests
cd build && QT_QPA_PLATFORM=offscreen ./bin/core_tests
QT_QPA_PLATFORM=offscreen ./bin/test_m3m4m5
QT_QPA_PLATFORM=offscreen ./bin/mviewer_unit_tests
```

On Windows, ensure the MSVC `bin` directory is first on `PATH` (or use the Visual Studio developer prompt) so `cl.exe` resolves correctly.

## Architecture

MViewer follows a strict layered architecture:

```
UI → Application → Domain → Core → Infrastructure
```

- **Domain**: Pure `std` types, zero Qt dependency
- **Core**: Qt-free headers; `.cpp` internals may use Qt
- **UI**: Qt 6 Widgets boundary only

When adding a feature, place it in the correct layer. Do not let UI logic leak into Core, and do not let Qt types appear in Domain headers.

## Quality Gates

Every change must pass:

1. **Build** succeeds (clean-first recommended)
2. **Tests** pass (`core_tests`, `test_m3m4m5`, `mviewer_unit_tests`)
3. **Benchmark** regression check (`benchmark_scenario` vs baseline)
4. **Documentation** updated (spec / ADR / RFC as applicable)
5. **Formatting** (`clang-format` — LLVM style, see `.clang-format`)

## Adding a Feature

1. Write or update the **RFC** in `docs/rfc/` (for non-trivial changes)
2. Update the **spec** in `docs/spec/` (Module/Purpose/API/.../Future template)
3. Implement behind the correct layer
4. Add **unit tests** (extend `test_m3m4m5` or add a focused suite)
5. Add a **benchmark scenario** if performance-relevant
6. Run `clang-format` on changed files
7. Open a PR with a clear description

## Code Style

- C++20, 4-space indent, 100-column limit (see `.clang-format`)
- Prefer `const` and value semantics
- Headers must compile standalone (include what you use)
- No Qt types in `domain/` or `core/` headers

## Reporting Issues

Please use GitHub Issues. Include:
- OS / compiler / Qt version
- Steps to reproduce
- Expected vs actual behavior
- Sample images if relevant (avoid large binaries in issues)

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
