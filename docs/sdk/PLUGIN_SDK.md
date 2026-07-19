# MViewer Plugin SDK

Write an analysis plugin for MViewer in C++ and have it recognized at runtime.
This document is the **stable contract** (M13 Phase 6). It is deliberately small:
one interface, three C exports, one build rule.

> Scope note: this SDK covers **analyzer plugins** (the `Analyzer` interface).
> Command / compare / export plugins are future surface; the loader is generic
> enough to host them later without changing the C ABI below.

## 1. The interface you implement

Include `core/analyzer/Analyzer.h` and derive from `Analyzer`:

```cpp
class Analyzer {
  public:
    virtual ~Analyzer() = default;
    virtual std::string name() const = 0;            // unique id, e.g. "acme.contrast"
    virtual std::string description() const = 0;
    virtual bool analyze(const ImageFrame &frame) = 0;
    virtual bool analyzeRegion(const ImageFrame &frame,
                               const mviewer::domain::Selection &region) = 0;
    virtual std::string resultText() const;          // optional, shown in UI
    virtual AnalyzerCapability capabilities() const; // optional flags
};
```

Capabilities (`core/analyzer/AnalyzerCapability.h`): `SingleImage`,
`MultiImage`, `RegionOfInterest`, `StatsOutput`. They let the host skip
analyzers that don't apply (e.g. PSNR needs two frames).

Pixel access in `analyze`:

```cpp
const ImageBuffer v = frame.pixels().view();
const int cpp   = v.channelsPerPixel();   // 1, 3, or 4
const uint8_t *row = v.data + y * v.stride();
const uint8_t *p   = row + x * cpp;
```

`ImageData` / `ImageBuffer` are value types in `core/image`; see the example
for a complete read loop. We deliberately expose a **stable, value-based pixel
view** rather than Qt types, so plugins never depend on Qt internals.

## 2. The C ABI you export

Every plugin is a shared library exporting **exactly** these three C symbols
(no name mangling, no version suffix):

```cpp
extern "C" {
    Analyzer *createAnalyzer();          // allocate your Analyzer subclass
    void      destroyAnalyzer(Analyzer*); // delete it (called by host)
    const char *pluginName();            // matches Analyzer::name()
}
```

On Windows use `__declspec(dllexport)`; on ELF use default visibility. The
bundled `MVIEWER_PLUGIN_EXPORT` macro (see `plugins/example`) handles both.

The host (`PluginLoader` / `PluginManager`) calls `createAnalyzer()`, registers
the returned instance with `AnalyzerRegistry`, and calls `destroyAnalyzer()`
when the analyzer is no longer needed. **You own the lifetime inside the
plugin** — allocate in `createAnalyzer`, free in `destroyAnalyzer`.

## 3. ABI stability rules (MUST hold)

These are the hard rules. Break any one and the plugin will misbehave or crash
on load — there is no compatibility shim.

1. **Same toolchain.** Build the plugin with the **same compiler + version**
   as `mviewer_core` (currently MSVC 19.x / `c++20`). Mixed toolchains =
   different CRT/`std` layout = undefined behavior.
2. **Same Qt build.** `mviewer_core` links Qt 6. If your plugin touches Qt, use
   the identical Qt 6 build (same minor version). The analyzer interface itself
   is Qt-free, so a pure-pixel plugin need not link Qt — but if it does, match it.
3. **`mviewer_core` stays SHARED.** Host and plugin must share **one**
   `Analyzer` / `ImageData` vtable. A static core would give each module its
   own copy and crash on cross-module `delete`. This is enforced by
   `WINDOWS_EXPORT_ALL_SYMBOLS` on `mviewer_core` — do not change it.
4. **C surface is frozen.** `createAnalyzer` / `destroyAnalyzer` /
   `pluginName` signatures will not change within a major version. The C++
   `Analyzer` vtable may grow (new virtuals added at the **end**, with defaults)
   but existing overrides remain valid.
5. **Don't unload at runtime.** Keep plugins loaded for the app lifetime.
   Unloading a Qt-linking DLL on Windows at teardown is unsafe (CRT detach
   ordering) — the host keeps handles alive and releases them on process exit.

## 4. Build (in-tree, reference)

The bundled example is the canonical build:

```
# from repo root (after a normal Release build of MViewer)
plugins/example/  ->  example_analyzer.{dll,so,dylib}
```

`plugins/example/CMakeLists.txt` links `mviewer_core` (PRIVATE) and drops the
`.dll` next to the app binaries. See `plugins/example/README.md`.

## 5. Build (third party / out-of-tree)

A plugin author needs only the **SDK header set** + the `mviewer_core` import
library / DLL (shipped with MViewer). Minimal recipe:

```cmake
add_library(my_analyzer SHARED my_analyzer.cpp)
target_include_directories(my_analyzer PRIVATE <mviewer-sdk>/include)  # SDK headers
target_link_libraries(my_analyzer PRIVATE <mviewer-sdk>/lib/mviewer_core.lib)
target_compile_features(my_analyzer PRIVATE cxx_std_20)
# same compiler / Qt / c++20 as mviewer_core
```

The SDK header set = `core/analyzer/Analyzer.h`, `AnalyzerCapability.h`,
`core/image/ImageFrame.h`, `ImageBuffer.h`, `domain/Selection.h`,
`domain/*` (pixel/format types). A packaged `mviewer-sdk.zip` (headers +
`mviewer_core` import lib + this doc) is the distribution form; produce it from
the release pipeline (see M13.3).

## 6. Load by MViewer

Drop the plugin next to the app (or in the configured plugin directory). At
startup `PluginManager::loadDirectory()` scans, `dlopen`/`LoadLibrary`s each,
calls `pluginName()` + `createAnalyzer()`, and self-registers the analyzer with
`AnalyzerRegistry`. It then appears in the Analysis panel automatically — no
code change in MViewer.

The end-to-end contract is enforced by `ctest pluginregistry_tests`
(build → load → self-register → create → analyze → region analyze), plus
`pluginloader_tests` / `pluginmanager_tests` for the loader/manager internals.

## See also

- `plugins/example/ExampleAnalyzerPlugin.cpp` — complete working plugin.
- `docs/adr/005-why-plugin-analysis.md` — why analysis is plugin-based.
- `src/core/plugin/PluginLoader.{h,cpp}`, `PluginManager.{h,cpp}` — host side.
