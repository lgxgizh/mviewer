# MViewer Coding Style Specification

## General Principles

- Readability over cleverness
- Consistency over personal preference
- Modern C++20 idioms where appropriate
- Performance-conscious but not prematurely optimized

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
| Namespaces | `snake_case` | `mviewer::image` |
| Enum values | `kCamelCase` | `PixelFormat::kRGBA8` |
| Template parameters | `PascalCase`, single letter for simple | `typename T`, `typename Allocator` |
| Concepts | `PascalCase` (verb-ish) | `Decodable`, `Cacheable` |
| Files | `snake_case` | `image_decoder.cpp`, `cache_manager.h` |

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
#include "mviewer/core/result.h"

namespace mviewer::image {

class ImageDecoder {
public:
    virtual ~ImageDecoder() = default;
    virtual std::expected<DecodeResult, Error> decode(const FilePath& path) = 0;
};

} // namespace mviewer::image
```

### Include Order

1. Related header (for `.cpp` files)
2. C system headers (`<cstdint>`, `<cstdio>`)
3. C++ STL headers (`<vector>`, `<memory>`)
4. Third-party headers (`<QtCore/...>`, `<vips.h>`)
5. Project headers (`"mviewer/..."`)

Each group separated by blank line. Alphabetically sorted within groups.

---

## Class Design

### Rule of Zero / Rule of Five

- Prefer Rule of Zero (use smart pointers, STL containers)
- If you define any of destructor/copy/copy-move/move, define all five

### Access Order

```cpp
class MyClass {
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

### Pimpl Idiom

Use Pimpl for classes with heavy third-party dependencies or unstable ABI:

```cpp
// header
class Renderer {
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

### Unrecoverable Errors → Exceptions

```cpp
if (!device) {
    throw RendererInitException("D3D11 device creation failed");
}
```

### Recoverable Errors → `std::expected`

```cpp
std::expected<DecodedImage, DecodeError> decode(const FilePath& path);
```

### Optional Values → `std::optional`

```cpp
std::optional<Metadata> readMetadata(const FilePath& path);
```

### No Raw Error Codes

Avoid `int` return codes. Use typed error types:

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
- `std::expected` (C++23, or use `tl::expected` until then) for fallible returns
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

Based on LLVM style with modifications:

```yaml
BasedOnStyle: LLVM
ColumnLimit: 100
IndentWidth: 4
ContinuationIndent: 8
AccessModifierOffset: -4
SortIncludes: true
IncludeBlocks: Regroup
PointerAlignment: Left
ReferenceAlignment: Left
SpaceBeforeParens: ControlStatements
SpacesInParentheses: false
MaxEmptyLinesToKeep: 2
```

### Line Length

- Target: 80 columns
- Maximum: 100 columns
- Break long lines at logical points

### Braces

Allman/BSD style for functions, K&R for control flow:

```cpp
// Functions: brace on new line
void processImage()
{
    // ...
}

// Control flow: brace on same line
if (condition) {
    // ...
} else {
    // ...
}
```

---

## Documentation

### Public APIs

All public APIs must have doc comments:

```cpp
/// Decodes an image file into a pixel buffer.
/// @param path Absolute path to the image file
/// @param params Decode parameters (resolution limit, color space)
/// @return Decoded image on success, DecodeError on failure
/// @thread_safety Thread-safe. Multiple decoders can run concurrently.
auto decodeImage(const FilePath& path, const DecodeParams& params)
    -> std::expected<DecodedImage, DecodeError>;
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
<type>: <short summary> (50 chars or less)

<body> (optional, wrap at 72 chars)

<footer> (optional, references)
```

### Types

| Type | Meaning |
| ------ | --------- |
| `feat` | New feature |
| `fix` | Bug fix |
| `perf` | Performance improvement |
| `refactor` | Code restructuring |
| `test` | Adding/fixing tests |
| `docs` | Documentation |
| `build` | Build system / CI |
| `style` | Formatting (no logic change) |
| `chore` | Maintenance |

### Examples

```
perf: reduce JPEG decode latency by 30% via libjpeg-turbo SIMD

Switch from libjpeg to libjpeg-turbo with SSE2/NEON SIMD paths.
Benchmarks show 3x speedup on 24MP images.

Refs: #42
```

```
fix: correct GIF frame disposal method handling

Previous implementation ignored the disposal method field,
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
