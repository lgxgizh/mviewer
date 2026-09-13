# MViewer Coding Style Specification

## General Principles

- Readability over cleverness
- Consistency over personal preference
- Modern C++20 idioms where appropriate
- Performance-conscious but not prematurely optimized

### Conformance Status

This document is normative, but parts of it are *target* rather than *current*
practice. Know which is which before "fixing" code to match a snippet:

| Area | Status |
| ------ | -------- |
| Formatting (indent, braces, alignment, include order) | **Enforced** — `.clang-format` is the source of truth, checked by the CI Format job |
| Layering + Qt boundary in core headers | **Enforced** — R1–R5 in `scripts/architecture_gate.ps1` |
| TU size / complexity budget | **Enforced** — `scripts/complexity_gate.ps1` (ADR 014) |
| clang-tidy warning classes | **Enforced** — CI clang-tidy job (`bugprone-*`, `performance-*`, `clang-analyzer-*` on changed lines) |
| Error handling, naming, comments, doc comments | **Reviewed** — conventions below describe what the code actually does; a snippet that contradicts the code is a doc bug, fix the doc |

**Where this document and a config file disagree, the config file wins** —
update the document in the same commit.

---

## Language Standard

- **C++20** minimum
- Compile with `/std:c++20` (MSVC) or `-std=c++20` (Clang/GCC)

---

## Naming Conventions

| Element | Convention | Example |
| --------- | ----------- | --------- |
| Types (classes, structs, enums) | `PascalCase` | `ImageDecoder`, `CacheEntry` |
| Functions / Methods | `camelCase` | `decodeImage()`, `nextFrame()` |
| Variables (local) | `camelCase` | `fileCount`, `pixelBuffer` |
| Member variables | `m_camelCase` (prefix `m_`) | `m_decoder`, `m_cacheSize` |
| Static variables | `s_camelCase` (prefix `s_`) | `s_instanceCount` |
| Global constants | `kCamelCase` (prefix `k_`) | `kMaxCacheSize` |
| Macros (avoid) | `SCREAMING_SNAKE_CASE` | `MVIEWER_ASSERT()` |
| Namespaces | `snake_case` | `mvcore` (core), `mviewer::domain`, UI types live in the global namespace |
| Enum values | `PascalCase`, no `k` prefix | `PixelFormat::RGB24`, `RenderCommandType::DrawImage` |
| Template parameters | `PascalCase`, single letter for simple | `typename T`, `typename Allocator` |
| Concepts | `PascalCase` (verb-ish) | `Decodable`, `Cacheable` |
| Files | `PascalCase`, matching the primary type | `ImageRepository.cpp`, `BatchProcessor.h` |

---

## File Organization

### Header Files (`.h`)

```cpp
#pragma once

// Related header (for .cpp)
#include "decoder_factory.h"

// C system headers
#include <cstdint>
#include <cstddef>

// C++ STL headers
#include <memory>
#include <vector>
#include <string>

// Third-party headers
#include <QtCore/QString>

// Project headers
#include "core/image/ImageBuffer.h"

namespace mvcore {

class ImageDecoder
{
  public:
    virtual ~ImageDecoder() = default;
    virtual bool canDecode(const std::string &path) const = 0;
    virtual ImageData decodeFull(const std::string &path) const = 0;
    virtual std::vector<std::string> extensions() const = 0;
};

} // namespace mvcore
```

### Include Order

1. Related header (for `.cpp` files)
2. C system headers (`<cstdint>`, `<cstdio>`)
3. C++ STL headers (`<vector>`, `<memory>`)
4. Third-party headers (`<QtCore/...>`, `<vips.h>`)
5. Project headers (`"core/..."`, `"domain/..."`)
6. Qt headers in a UI TU (`<QWidget>`) — core headers must not include Qt at all
   (R5 in `scripts/architecture_gate.ps1`); `core/image/QtConvert.h` and
   `core/image/QtMetadataSemantics.h` are the only adapter exceptions

Each group separated by blank line. Sorted case-sensitively within groups
(`SortIncludes: CaseSensitive`), so `"core/..."` sorts before `"domain/..."`.

---

## Class Design

### Rule of Zero / Rule of Five

- Prefer Rule of Zero (use smart pointers, STL containers)
- If you define any of destructor/copy/copy-move/move, define all five

### Access Order

```cpp
class MyClass
{
  public:
    // Constructors, destructor
    // Public methods

  protected:
    // Protected methods

  private:
    // Private methods
    // Member variables (m_ prefix)
};
```

(`AccessModifierOffset: -2` — access specifiers indent two spaces, as above.)

### Pimpl Idiom

Use Pimpl for classes with heavy third-party dependencies or unstable ABI:

```cpp
// header
class Renderer
{
  public:
    Renderer();
    ~Renderer();
    void draw();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

---

## Smart Pointer Policy

| Situation | Use |
| ----------- | ----- |
| Exclusive ownership | `std::unique_ptr<T>` |
| Shared ownership | `std::shared_ptr<T>` |
| Non-owning observer | `raw pointer` (`T*`) or `std::observer_ptr` (C++20) |
| Optional non-owning | `T*` (nullable) |
| Factory returns | `std::unique_ptr<T>` |
| Breaking cycles | `std::weak_ptr<T>` |

**Never** use `std::auto_ptr` (removed in C++17).
**Never** use raw `new`/`delete` in application code (only inside smart pointer construction).

---

## Error Handling

The codebase does **not** use exceptions or `std::expected` for recoverable
failures. `std::expected` is C++23 and is not available under `/std:c++20`;
there is no `tl::expected` dependency and no `mviewer/core/result.h`. Do not
introduce either without an ADR (see `docs/adr/`).

### Fallible Operations → `bool` + Out-Parameter or Result Struct

Callers must be able to see the failure and a reason. Pick the shape that fits
the call site:

```cpp
// 1. bool + error out-parameter — for thin IO / persistence helpers.
bool exportSettings(const std::string &path, std::string *errorOut = nullptr);
bool atomicWriteFile(const std::string &path, const std::string &content,
                     std::string *errorOut = nullptr);

// 2. Result struct — when success carries data as well as an error.
//    ImageRepository::Result: std::shared_ptr<ImageFrame> frame; std::string error;
//                             bool success() const;
ImageRepository::Result result = repo.load(path, opts);
if (!result.success()) {
    report(result.error);
}

// 3. bool + reference out-parameter — for cache/hot-path lookups where the
//    caller only needs "hit or miss" and a miss is not an error.
bool getMemory(CacheLevel level, const std::string &key, ImageData &out);
bool loadMemoryHit(const std::string &filePath, const std::string &key, ImageData &out);

// 4. Nullable domain value — for decode-style producers. A failed decode
//    returns a null ImageData (the registry turns that into a Result/error
//    string one level up, where the path is still known).
virtual ImageData decodeFull(const std::string &path) const = 0;
```

The out-parameter form is only acceptable when the function's name already says
what can fail (`load`, `read`, `save`, `decode`); never return a `bool` that
silently means "and here is no explanation for an expected user-visible error".

### Optional Values → `std::optional`

Used for lookups where absence is normal, not an error:

```cpp
std::optional<Entry> cached(const std::string &path) const;
std::optional<mviewer::domain::Workspace> deserializeWorkspace(const std::string &text);
```

### Exceptions → Only for Unrecoverable Programming/Environment Errors

Exceptions are confined to `core/filesystem/Utf8Path.cpp`, where a path that
cannot be represented as valid UTF-8 is a contract violation that no caller can
repair, and to test doubles that inject failures:

```cpp
// core/filesystem/Utf8Path.cpp
throw std::runtime_error("invalid UTF-8 filesystem path");
```

No custom exception types exist. Every thread/task entry point catches at the
boundary so a throw can never escape a pool worker:

```cpp
try {
    body();
} catch (const std::exception &error) {
    qWarning() << "task failed:" << error.what();
} catch (...) {
    qWarning() << "task failed: unknown error";
}
```

`catch (...)` without logging is not acceptable at a boundary — a swallowed
throw is a silent hang in the UI.

### No Raw Error Codes

Avoid `int` return codes and sentinel values such as `-1`. Use a typed enum
when a failure has distinguishable causes that callers act on:

```cpp
enum class DecodeError {
    FileNotFound,
    InvalidFormat,
    CorruptData,
    UnsupportedFormat,
    OutOfMemory,
};
```

---

## Const Correctness

- Everything that should not be modified **must** be `const`
- Member functions that don't modify state: `void draw() const;`
- Parameters that aren't modified: `void render(const Image& image);`
- Prefer `const` over non-const, always

---

## Modern C++20 Features

### Use Freely

- `auto` for obvious types (not for primitive numeric types where type matters)
- `std::unique_ptr`, `std::shared_ptr`, `std::make_unique`, `std::make_shared`
- `std::span` for non-owning array views
- `std::string_view` for read-only string parameters
- `std::optional` for nullable values
- `bool` + out-parameter / Result structs for fallible returns (see Error Handling) — **not** `std::expected`
- `std::format` for string formatting
- `consteval` / `constinit` for compile-time guarantees
- Designated initializers: `Config{.maxCacheSize = 512, .threadCount = 4}`
- Range-based for: `for (const auto& item : container)`
- Structured bindings: `auto [width, height] = image.size();`

### Use Judiciously

- Concepts: for public template APIs only
- Ranges: when they improve readability
- Coroutines: only for async pipeline stages
- Modules: when build system supports them (future)

### Avoid

- `std::endl` (use `'\n'` — `endl` flushes)
- `volatile` (not a threading primitive)
- C-style casts (use `static_cast`, `reinterpret_cast`)
- `std::move` on return values (prevents NRVO)
- Excessive `auto` that obscures types

---

## Formatting

### clang-format Configuration

`.clang-format` at the repository root is the source of truth and is checked by
the CI Format job (`git clang-format --diff`), so this listing is informative
only — change the file, not this section:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
UseCRLF: false          # LF everywhere: Windows and Linux clang-format must agree
AccessModifierOffset: -2
AllowShortFunctionsOnASingleLine: None
BreakBeforeBraces: Allman
PointerAlignment: Right
SortIncludes: CaseSensitive
IncludeBlocks: Preserve
```

Commit C++ files as **pure LF**. A file committed with CRLF or mixed line
endings makes `git clang-format` reformat the whole file, which fails the
Format gate. Stage with `git -c core.autocrlf=false add <path>` when in doubt.

### Line Length

- Hard maximum: 100 columns (enforced by clang-format)
- Break long lines at logical points

### Braces

Allman for both functions and control flow (`BreakBeforeBraces: Allman`):

```cpp
// Functions and control flow: brace on its own line
void processImage()
{
    // ...
    if (condition)
    {
        // ...
    }
    else
    {
        // ...
    }
}
```

---

## Documentation

### Public APIs

All public APIs must have doc comments:

```cpp
/// Loads one image and reports the outcome through the result value.
/// @param filePath Absolute path to the image file
/// @param opts Load options (cache use, histogram, resolution limit)
/// @return A frame on success; on failure `success()` is false and `error` explains why
/// @thread_safety Safe to call from any thread; the repository serialises disk access.
ImageRepository::Result result = ImageRepository::instance().load(filePath, opts);
if (!result.success()) {
    report(result.error);
}
```

### Complex Algorithms

Comment the **why**, not the **what**:

```cpp
// Use perceptual hash for similarity detection because
// exact pixel comparison fails on re-compressed images.
auto hash = computePHash(image);
```

---

## Commit Conventions

### Format

```
<Imperative summary> (72 chars or less)

<body> (optional, wrap at 72 chars: what changed and why)

<footer> (optional, references)
```

The subject is an imperative sentence with a capitalised first word — it
completes "This commit will …". Type prefixes (`fix:`, `feat:`, …) are **not**
used in this repository.

### Examples

```
Reduce JPEG decode latency via libjpeg-turbo SIMD

Switch from libjpeg to libjpeg-turbo with SSE2/NEON SIMD paths.
Benchmarks show 3x speedup on 24MP images.

Refs: #42
```

```
Correct GIF frame disposal method handling

The previous implementation ignored the disposal method field,
causing visual artifacts in animated GIFs with transparency.

Fixes: #87
```

---

## Pull Request Guidelines

- **Size:** < 400 lines changed (preferably < 200)
- **Scope:** One logical change per PR
- **Tests:** Include tests for new functionality
- **Benchmarks:** Include benchmarks for performance-critical changes
- **Description:** Explain what, why, and how to test
- **Review:** At least one approval before merge
